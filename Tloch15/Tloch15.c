//================================================================
// Fernschreiber-Schnittstelle TW39 für TxP2-System
//	für ATmega168 auf Platine FernschrTW39
//================================================================
//				
//  
//================================================================
// verwendete Pins siehe Ports.h
//================================================================

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <inttypes.h>


#include "TwiEvents.h"
#include "Bits.h"
#include "timercs.h"

#include "TxP2-Defs.h"
#include "MsTimer.h"
#include "BusKomm.h"
#include "TxP2-Endgeraet.h"
#include "Taste.h"
#include "Ports.h"
#include "SeriellUmsetz.h"
#include "BaudotCode.h"
#include "KonfigDialog.h"
#include "KonfigSpeicher.h"
#include "LokalAusgabe.h"
#include "LokalUhr.h"
#include "Zeitsperre.h"

#include "../SvnVersion.h"


// Schalter für Code-Varianten
// ===========================

//#define PARALLELAUSGABE
	//!< Für Platine TW39doppel: Sendung und Empfang wird auf der zweiten 
	//!< Schnittstelle mitprotokolliert. Dann darf der Kontroller der 
	//!< zweiten Schnittstelle nicht bestückt sein.

//#define FALSCHKDO_FEHLERSTOP
	//!< Unpassende Kommandos auf dem I²C-Bus werden mit Fehlerstop quittiert.

//#define TWI_DEBUG
	//!< TWI-Ereignisse werden protokolliert. 

#define BUSFEHLER_ABBRUCH
	//!< bei Bus-Fehlern Abbruch der Verbindung.

#ifndef TWI_DEBUG
#define WIEDERHOLUNGSSENDUNGEN
	//!< Status Mark / Space regelmäßig senden.
#endif //TWI_DEBUG


// #define LEDROT_BEI_UNERWARTETWDH
	//!< LED rot wird eingeschaltet, wenn BusKdoSpaceWdh oder BusKdoMarkWdh empfangen wird, ohne
	//!< das entsprechendes "Haupt-Kommando" empfangen wurde.


//#define NOWATCHDOG
	//!< Watchdog abgeschaltet


#ifdef PROGIDZUSATZ
//! Marker im Code als Identifikation
PROGMEM const char Identifier[] = "___itlx_Tloch15-" PROGIDZUSATZ "___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#else
//! Marker im Code als Identifikation
PROGMEM const char Identifier[] = "___itlx_Tloch15___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#endif

// Konstanten
// ----------

enum { AnrufAbbruchZeit_Std = 7 }; // sekunden
enum { BusEigenAdresse_Std = 15 };
enum { StartQuittVerzoegerung_Min = 2 }; // x/10 sekunden
enum { StartQuittVerzoegerung_Std = 7 }; // x/10 sekunden

// Eeprom-Speicher-Adressen
// ------------------------

enum {
	EEAdr_BusEigenAdresse = 0,
    EEAdr_Sperrzeit = 4, // Beansprucht 20 Bytes
    EEAdr_TasteFunktion = 24,
    EEAdr_UmleitungAbweisen = 25,
	EEAdr_AnrufAbbruchZeit = 37,
	EEAdr_StartQuittVerz = 38,
	EEAdr_EigeneKennung = 39,
	EEAdr_Ende = 70 // darf erhöht werden
}; // MaxIndex < BankOffset = 160


// Typen
// -----

typedef enum { SperreTaste, SperreStoerung, SperreZeit, SperreWahl } TSperreGrund;


// Konfigurations-Variablen
// ====================

uint8_t AnrufAbbruchZeit; //!< Maximale Zeit zwichen Aktivierung Anrufsignal und Ende des Hochlaufs des Fernschreibers

uint8_t StartQuittVerzoegerung; 
	//!< zusätzliche Zeit nach Empfang der Betriebsbereitschaft des 
	//!< Fernschreibers bis zur Meldung "Betriebsbereit" an den Verbindungspartner.

typedef enum { Deaktivierung, DemoBetriebStarten, ExtStromEinschalten } TTasteFunktion;

TTasteFunktion TasteFunktion; //!< Bisher möglich: 0 = deaktivierung, 1 = Demo-Betrieb, 2 = Ausgang zum externen Schalter aktivieren

// Arbeits-Variablen 
// =======================

bool BefehlEinschalten; //!< Fs soll laufen
bool BefehlMark; //!< Fs Schleifenstrom soll Ein sein
bool MeldungEingeschaltet; //!< Fs läuft tatsächlich
bool MeldungMark; //!< Fs Schleifenstrom ist Ein

TMsTimer AusschaltungTimer; //!< Zählt die Millisekunden von Schleifenunterbrechung bis Ausschaltung
TMsTimer EntprellungTimer; //!< Zählt die Millisekunden von Pegelwechsel am Port bis tatsächlichem Pegelwechsel


// Schnittstellen-Spezifische Variablen
// ====================================

TMsTimer RuheTimer; //!< Läuft, wenn weder gedruckt noch geschrieben wird

enum { MaxCodefolgeLaenge = 30 }; //!< Maximale Länge von #AusschaltZeichen, #Wahlaufforderung, #VerbindungHergestelltZeichen, #EigeneKennung

	
uint8_t EigeneKennung[MaxCodefolgeLaenge+1];
	//!< Text des eigenen Kennungsgeber-Simulators
	
PROGMEM const uint8_t EigeneKennungDefault[] = { TtyCodeBuUm, TtyCodeWR, TtyCodeZL, TtyCodeZiUm, 29, 1, TtyCodeBuUm, 1, 9, 3, 14, 5 } ;
	//!< Standardwert für #EigeneKennung
	// Manuell prüfen, dass es nicht mehr als MaxCodefolgeLaenge Zeichen sind!
	
// 14, 3, 6, wäre CON	

// Schnittstellen-Spezifische Funktionen
// =====================================
	
static inline bool ValidCode(uint8_t c) { return c >= (1<<5) && c < (1<<6); }

//! bedient Hardware-IO entsprechend der aktuellen Zustände.

/*!
 * setzt FS_AKTIV und FS_AUSG entsprechend BefehlEinschalten und BefehlMark,
 * setzt MeldungEingeschaltet und MeldungMark entsprechend FS_EING,
 * steuert die Status-LEDs
 */ 

static void FernschrIO()
	{
	// Pegel & Polung ausgeben
	// -----------------------
	bset_FS_AKTIV(BefehlEinschalten);

	if (BefehlEinschalten)
		bset_LEDBLAU(!BefehlMark);
	bset_FS_AUSG(BefehlMark);

	// Schleifenstrom auswerten: Einschaltung oder nicht
	// -------------------------------------------------
	if (get_FS_EING())
		{ // Schleifenstrom ist aus (negierter Eingang)
		if (MeldungEingeschaltet)
			{
			if (TimerVal(&AusschaltungTimer) > 800) // mehr als 800 ms kein Strom --> aus
				MeldungEingeschaltet = false;
			} 
		else
			StartTimer(&AusschaltungTimer); // Meldung und tatsächlicher Zustand stimmen überein
		}
	else
		{ // Schleifenstrom ist ein
		if (!MeldungEingeschaltet)
			{
			if (TimerVal(&AusschaltungTimer) > 5) // mehr als 5 ms Strom --> ein
				MeldungEingeschaltet = true;
			} 
		else
			StartTimer(&AusschaltungTimer); // Meldung und tatsächlicher Zustand stimmen überein
		} // else Schleifenstrom ist ein
		
	// Schleifenstrom auswerten: Mark / Space
	// --------------------------------------
	if (!BefehlMark || !MeldungEingeschaltet)
		{
		MeldungMark = true; // Beim Senden von Space ist kein Empfang möglich --> Grundstellung
		StartTimer(&EntprellungTimer);
		}
	else
		{ // sinnvolle Auswertung des Schleifenstroms möglich
		if (get_FS_EING())
			{ // Strom ist aus --> Space
			if (MeldungMark)
				{ // der Applikation wird noch Mark gemeldet
				if (TimerVal(&EntprellungTimer) >= 3) // mindestens 3 ms konstant Space --> Space melden
					MeldungMark = false;
				}
			else // !MeldungMark
				StartTimer(&EntprellungTimer); // Regelzustand bei Space: Space wird auch gemeldet
			} // Strom ist aus
		else // !get_FS_EING()
			{ // Schleifenstrom fließt
			if (!MeldungMark)
				{ // der Applikation wird noch Space gemeldet
				if (TimerVal(&EntprellungTimer) >= 3) // mindestens 3 ms konstant Mark --> Mark melden
					MeldungMark = true;
				}
			else // MeldungMark
				StartTimer(&EntprellungTimer); // Regelzustand bei Mark: Mark wird auch gemeldet
			} // else !get_FS_EING() == Schleifenstrom fließt
		} // else BefehlMark && MeldungEingeschaltet
	
	if (BIT_IS_SET(Status, StatBit_AngerufenBelegt))
		bset_LEDGELB(!MeldungMark);
	else // !BIT_IS_SET(Status, StatBit_AngerufenBelegt))
		bset_LEDGRUEN(!MeldungMark);

	} // FernschrIO
	
	
//////////////////////////////////////////////////////////////////

// Allgemeine Funktionen
// =====================
	

//! Einschaltung des Fs auslösen.
//-------------------------------
//! \returns Einschaltung wurde erfolgreich durch Endgerät quittiert.

static bool FernschrEinschalten(bool WarteQuittVerz)
	{
	TMsTimer StabilTimer;
	TMsTimer AbbruchTimer;
	
	StartTimer(&StabilTimer);
	StartTimer(&AbbruchTimer);
	
	if (BefehlEinschalten) // ist schon an, dann nicht verzögern
		WarteQuittVerz = false;
		
	do
		{
		BefehlEinschalten = true;
		BefehlMark = true;
		FernschrIO();
		if (!MeldungEingeschaltet)
			StartTimer(&StabilTimer);
		if (TimerVal(&AbbruchTimer) > AnrufAbbruchZeit * 1000)
			{
			BefehlEinschalten = false;
			BefehlMark = true;
			FernschrIO();
			return false;
			}
		} while (TimerVal(&StabilTimer) < 300);
		
	if (WarteQuittVerz)
		{
		StartTimer(&StabilTimer);
		while (TimerVal(&StabilTimer) < StartQuittVerzoegerung * 100) // StartQuittVerzoegerung ist in 1/10 sekunden
			FernschrIO();
		}
		
	return true;
	}
		

//////////////////////////////////////////////////////////////////

//! Ausschaltung des Fs auslösen.

static void FernschrAusschalten()
	{
	TMsTimer Timer;
	
	if (MeldungEingeschaltet && !BefehlEinschalten)
		FernschrEinschalten(false); // Rückgabewert ignorieren

	StartTimer(&Timer);
	do
		{
		BefehlEinschalten = false;
		BefehlMark = true;
		FernschrIO();
		if (MeldungEingeschaltet)
			StartTimer(&Timer);
		}
	while (TimerVal(&Timer) < 500);
	
	}
		

/////////////////////////////////////////////////////////////////////////////////////////7

//! Modul / Schnittstelle irreversibel stoppen.
//---------------------------------------------
//! Nur Reset befreit, ein Tastendruck löst einen Reset aus.

__attribute__ ((noreturn)) void FehlerStop(int Nummer /*!< Fehlercode wird mit den LED angezeigt, Rot = Bit 0 */ ) 
	// Fehler-Codes: 
	// 1: Bus-Empfang trotz Sperre
	// 2: General Call ohne entsprechende Freigabe
	// 3: Kommando über I²C in falschem Kontext
	// 7: Interner Fehler bei FsBetriebsart
	// 8: unerlaubte Einschaltung
	// 9: unerlaubte Wahl
	// 10: unerlaubte Aktivierung / Deaktivierung
	{
	TWCR = (1<<TWINT) | (0<<TWEA) | (0<<TWSTA) | (0<<TWSTO) | (0<<TWEN) | (0<<TWIE);

	BefehlEinschalten = false;
	BefehlMark = true;
	FernschrIO(); // damit der Fernschreiber abgeschaltet wird.
	
	uint8_t TasteZ = 0;
	bool TasteWirk = false;
	TMsTimer TasteTimer;
	StartTimer(&TasteTimer);
	while (1)
		{
		wdt_reset();

		if (TimerVal(&TasteTimer) > 400)
			{
			StartTimer(&TasteTimer);
			if (get_TASTE())
				{ // Taste gedrückt
				if (TasteZ < 5)
					TasteZ++;
				else
					TasteWirk = true;
				}
			else
				{ // Taste nicht gedrückt
				if (TasteZ > 0)
					{
					TasteZ--;
					if (TasteZ == 0 && TasteWirk)
						{
						cli();
						wdt_enable(WDTO_1S);
						while (1)
							;
						}
					}
				} // Taste nicht gedrückt
			}
		else if (!TasteWirk && TimerVal(&TasteTimer) > 200)
		    {
		    bset_LEDROT(BIT_IS_SET(Nummer, 0));
			bset_LEDGELB(BIT_IS_SET(Nummer, 1));
		    bset_LEDGRUEN(BIT_IS_SET(Nummer, 2));
		    bset_LEDBLAU(BIT_IS_SET(Nummer, 3));
			}
		else
			{
			clr_LEDROT();
			clr_LEDGELB();
			clr_LEDGRUEN();
			clr_LEDBLAU();
			}
		}
	}	


/////////////////////////////////////////////////////////////

//! Liest ein Zeichen vom angeschlossenen Fs ein.
//-----------------------------------------------
//! Serialisiert die ankommenden Impulse und wandelt BAUDOT in ASCII
//! \return Zeichen im ASCII-Code

char LokalZeichenLesen()
	{
	char c;

	EmpfUmsetzModus = UmsetzLokal; // sicherheitshalber
	while (true)
		{
		FernschrIO();
		SeriellUmsetzung(MeldungMark, &BefehlMark);
		if (SerUmEmpfBitNr == SerUmEmpfFertig)
			{
			c = CodeZuZeichen(SerUmEmpfDaten, &BaudotMode);
			SerUmEmpfBitNr = SerUmEmpfWarte;
			if (c != '\0')
				return c;
			}
		if (!MeldungEingeschaltet)
			return '\0';
		}
	}


/////////////////////////////////////////////////////////////

//! Gibt ein Zeichen am angeschlossenen Fs aus.
//----------------------------------------------
//! serialisiert den Code und gibt ihn auf dem Endgerät aus.
//! \param code Zeichen im Baudot-Code

static void LokalCodeAusgabe(uint8_t code)
	{
	SendeUmsetzModus = UmsetzLokal; // sicherheitshalber
	SerUmSendDaten = code;
	SerUmSendBitNr = SerUmSendStart;
	while (SerUmSendBitNr != SerUmSendWarte)
		{
		SeriellUmsetzung(MeldungMark, &BefehlMark);
		FernschrIO();
		}
	}


/////////////////////////////////////////////////////////////

//! Gibt ein Zeichen am angeschlossenen Fs aus.
//----------------------------------------------
//! wandelt ASCII in Baudort und serialisiert den Code
//! \param c Zeichen im ASCII-Code

void LokalZeichenAusgabe(char c)
	{
	uint8_t code1, code2;
	
	// Umwandlung von GROSS in klein macht ZeichenZuCode
	if (!ZeichenZuCode2(c, &BaudotMode, &code1, &code2))
		return;
	LokalCodeAusgabe(code1);
	if (code2 != 255)
		LokalCodeAusgabe(code2);
	}
			
		
static void VerbindungSteht();

static void KommendSperren(TSperreGrund Grund);


/////////////////////////////////////////////////////////////

//! Wickelt eine kommende Verbindung ab.
//----------------------------------------------
//! Schaltet das Endgerät ein, wartet auf Einschalt-Quittung 
//! und bestätigt den erfolgreichen Aufbau. Ruft seinerseits VerbindungSteht()
//! auf und kehrt erst nach Verbindungsabbau zurück.

static void VerbindungKommend()
	{
	FernschrIO();

	set_LEDGRUEN();
	set_LEDROT();
	
	if (!FernschrEinschalten(true))
		{ // Timeout...
		clr_LEDGRUEN();
		GeAusschalten(true);
		KommendSperren(SperreStoerung);
		clr_LEDROT();		
		return;
		}

	clr_LEDROT();
	
	if (!GeEinschaltQuittung())
		{
		GeAusschalten(true);
		FernschrAusschalten();
		}
	else
		VerbindungSteht(); 
		
	}
	

	
	


/////////////////////////////////////////////////////////////

//! Behandelt alle Ereignisse, wenn die Verbindung erfolgreich aufgebaut wurde.
//-----------------------------------------------------------------------------
//! Arbeitet sowohl bei gehender, als auch bei kommender Verbindung.
//! \param AutoKennungAbfrage true, wenn automatisch die Kennung der Gegenstelle 
//! abgerufen werden soll. Abfrage wird solange wiederholt, bis eine lesbare Antwort 
//! eintrifft.

static void VerbindungSteht()
	{
	uint8_t KennungAusgabePhase;
	bool Bit5unterdruecken; // sperrt WerDa und F auf der Ziffernseite
	bool ZiffernEbene;
	
	KennungAusgabePhase = 0;

	GeSendeMark(true); 
	BefehlMark = true;

	SendeUmsetzModus = UmsetzLokalUndFern; // Für Sendung der simulierten Kennung 
	EmpfUmsetzModus = UmsetzFern; // Für Empfang von "Antworten" 
	KennungAusgabePhase = 0;
	Bit5unterdruecken = false;
	ZiffernEbene = true; // sicherheitshalber

	while (true)
		{
		FernschrIO();
		TastePruefen();

		if (!MeldungEingeschaltet) // sollte eigentlich nicht passieren
			{
			GeAusschalten(false);
			FernschrAusschalten();
			while (!KoAusschalten())
				FernschrIO();
			return;
			}

		if (KoAusschalten())
			{
			FernschrAusschalten();
			GeAusschalten(false); // da braucht auf nichts mehr gewartet zu werden
			return;
			}

		BefehlMark = KoEmpfMark() || Bit5unterdruecken; // damit kann aus einer 0 eine 1 gemacht werden.
	
		if (SerUmSendBitNr <= SerUmSendStart) // Start oder Warten...
			GeSendeMark(MeldungMark); // Nur Fs-Pegel direkt auf Bus, wenn nicht seriell gesendet wird...
			
		// Auswertung des Empfangspuffers: 
		// a) ???
		// b) Internen Kennungsgeber-'Simulator' ansteuern
		while (!PufferLeer(&EmpfPuffer))
			{
			uint8_t code = PufferAusg(&EmpfPuffer);
			if (code == TtyCodeBuUm)
				{
				ZiffernEbene = false;
				}
			else if (code == TtyCodeZiUm)
				{
				ZiffernEbene = true;
				}
			else
				{
				if (code == TtyCodeZiWerDa && ZiffernEbene)
					KennungAusgabePhase = 2;
				else if (KennungAusgabePhase == 2)
					KennungAusgabePhase = 1; 
						// jedes andere Zeichen schaltet 'anstehende' Kennungsausgabe wieder ab.
				}
			}
			
		// Verbotene Codes unterdrücken:
		if (!Bit5unterdruecken && ZiffernEbene && SerUmEmpfBitNr == 6 /*5.Datenbit*/ && (SerUmEmpfDaten == (TtyCodeZiWerDa >> 1) || SerUmEmpfDaten == (22 >> 1))) // 22 = Code 'F'
			Bit5unterdruecken = true;
		if (Bit5unterdruecken && (SerUmEmpfBitNr == SerUmEmpfFertig || SerUmEmpfBitNr == SerUmEmpfWarte))
			Bit5unterdruecken = false;

		// Kennungsgeber-Simulator bearbeiten:
		if (!MeldungMark) 
			KennungAusgabePhase = 0; 
				// sobald selbst geschrieben wird wird Kennungsgeber-Ausgabe wieder in Grundstellung
				// gesetzt.
				
		if (KennungAusgabePhase == 2 && TimerVal(&RuheTimer) > 800)
			{
			for (uint8_t i = 0 ; i < MaxCodefolgeLaenge && ValidCode(EigeneKennung[i]); i++)
				PufferSpeich(&SendePuffer, EigeneKennung[i] & 0x1F);
			KennungAusgabePhase = 0;
			}

		}

	}


/////////////////////////////////////////////////////////////

//! Demo-Betrieb
// ----------------------------------------------------------
//! Ein fester Text wird gedruckt, bis die Taste gedrückt wird
//! oder der Fs mit der Schlusstaste abgeschaltet wird.

PROGMEM const char DemoText[] = 
#include "DemoText.h"
;

static void DemoBetrieb()
	{
	PGM_P p;
	
	SeriellUmsetzInit();
	Aktivieren(false);
	
	if (!FernschrEinschalten(true))
		return;
	
	set_LEDBLAU();
	
	p = DemoText;
	
	while (pgm_read_byte(p) != '\0')
		{
		LokalZeichenAusgabe(pgm_read_byte(p));	
			// macht intern FernschrIO also auch Schlusstaste-Erkennung
		p++;
		TastePruefen();
		if (Tastendruck != NichtGedr)
			break;
		if (!MeldungEingeschaltet)
			break;
		}

	Tastendruck = NichtGedr;
	FernschrAusschalten();
	Aktivieren(true);
	clr_LEDBLAU();
	
	}


/////////////////////////////////////////////////////////////

//! Behandelt die Selbstkonfiguration des Moduls.
//-----------------------------------------------
//! Arbeitet mit dem angeschlossenen Endgerät zusammen.
//! Alle 'Aufräumarbeiten' macht KonfigurationEnde()

static void Konfiguration()
	{
	bool Abbruch;
	bool NoExpertSettings;
	
	SeriellUmsetzInit();
	Aktivieren(false);
	set_LEDROT();
	
	if (!FernschrEinschalten(true))
		return;
	
	BaudotMode_SetEmpfangen(BaudotMode); // damit auch eine BU-Umschaltung gesendet wird.
	
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR("\r\n configuration Tloch15 version " SVNVERSION " date " __DATE__));
#else
	LokalTextAusgabeP(PSTR("\r\n konfiguration Tloch15 version " SVNVERSION " datum " __DATE__));
#endif //def SPRACHE_EN

	// Vorab die Frage nach "Expertenfunktionen"
	// -----------------------------------------
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR("\r\n simple configuration?    ")); 
#else	
	LokalTextAusgabeP(PSTR("\r\n einfache konfiguration?    ")); 
#endif //def SPRACHE_EN

	if (LokalBoolEingabe(&NoExpertSettings) == 0)
		return;

	LokalTextAusgabeP(OkStrP);

	// Durchwahl und co.
	// -----------------
	Abbruch = !KonfigurationAllgemein(); 
	if (Abbruch) 
		return;
	
	// jetzt bei einfacher Konfiguration abbrechen
	// -------------------------------------------
	if (NoExpertSettings)
		{
		AnrufAbbruchZeit = AnrufAbbruchZeit_Std;
		StartQuittVerzoegerung = StartQuittVerzoegerung_Std;
		SperrzeitInit();
		TasteFunktion = 0;
		LokalTextAusgabeP(PSTR("\r\n +++ \r\n\n\n\n"));
		return;
		}

	// Sperrzeiten
	// -----------
	if (!SperrzeitEingabeDialog())
		return;
		
	// Timeout beim Anruf
	// ------------------
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR("\r\n timeout for incoming calls in seconds (3-25, cur. "));
#else
	LokalTextAusgabeP(PSTR("\r\n maximale hochlauf-zeit in sekunden (3-25, akt. "));
#endif //def SPRACHE_EN

	LokalZahlAusgabe(AnrufAbbruchZeit, 0);

#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR(") new:     "));
#else
	LokalTextAusgabeP(PSTR(") neu:     "));
#endif //def SPRACHE_EN

	if (LokalZahlEingabe(&AnrufAbbruchZeit, 0) < 0)
		return;
	if (AnrufAbbruchZeit < 3)
		AnrufAbbruchZeit = 3;
	else if (AnrufAbbruchZeit > 25)
		AnrufAbbruchZeit = 25;

	LokalTextAusgabeP(OkStrP);
		
	// Verzögerung der Rückmeldung des Starts des Fernschreibers
	// ---------------------------------------------------------
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR("\r\n delay confirmation of startup in /10 seconds\r\n (3-200, cur. "));
#else
	LokalTextAusgabeP(PSTR("\r\n verzoegerung rueckmeldung fs-anlauf in /10 sekunden\r\n (3-200, akt. "));
#endif //def SPRACHE_EN

	LokalZahlAusgabe(StartQuittVerzoegerung, 0);

#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR(") new:     "));
#else
	LokalTextAusgabeP(PSTR(") neu:     "));
#endif //def SPRACHE_EN

	if (LokalZahlEingabe(&StartQuittVerzoegerung, 0) < 0)
		return;
	if (StartQuittVerzoegerung < StartQuittVerzoegerung_Min)
		StartQuittVerzoegerung = StartQuittVerzoegerung_Min;
	else if (StartQuittVerzoegerung > 200)
		StartQuittVerzoegerung = 200;

	LokalTextAusgabeP(OkStrP);
		
	// Modus für Tastendruck
	// ---------------------
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR("\r\n module button function (cur. "));
#else
	LokalTextAusgabeP(PSTR("\r\n funktion taste am modul (akt. "));
#endif //def SPRACHE_EN

	LokalZahlAusgabe(TasteFunktion, 0);

#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR(") new:     "));
#else
	LokalTextAusgabeP(PSTR(") neu:     "));
#endif //def SPRACHE_EN

	if (LokalZahlEingabe(&TasteFunktion, 0) < 0)
		return;

	LokalTextAusgabeP(OkStrP);
	
	// weitere Eingaben
	// ----------------

	LokalTextAusgabeP(PSTR("\r\n +++ \r\n\n\n\n"));
	}


/////////////////////////////////////////////////////////////

//! Beendet die Selbstkonfiguration des Moduls.
//-----------------------------------------------
//! Arbeitet mit dem angeschlossenen Endgerät zusammen.

static void KonfigurationEnde()
	{
	FernschrAusschalten();
	
	KonfigSchreibeByte(EEAdr_BusEigenAdresse, BusEigenAdresse);
	
	KonfigSchreibeByte(EEAdr_UmleitungAbweisen, UmleitungAbweisen);

	KonfigSchreibeByte(EEAdr_TasteFunktion, TasteFunktion);

	KonfigSchreibeByte(EEAdr_AnrufAbbruchZeit, AnrufAbbruchZeit);

	KonfigSchreibeByte(EEAdr_StartQuittVerz, StartQuittVerzoegerung);
	
	SperrzeitSpeicherEeprom(EEAdr_Sperrzeit);

	Aktivieren(true);

	clr_LEDROT();
	}
	
	
/////////////////////////////////////////////////////////////

//! Fehlertext-Ausgabe

static void FehlermeldungDrucken()
	{
	SeriellUmsetzInit();
	Aktivieren(false);
	set_LEDROT();
	
	if (!FernschrEinschalten(true))
		return;
	
	BaudotMode_SetEmpfangen(BaudotMode); // damit auch eine BU-Umschaltung gesendet wird.
	
	KonfigSpeicherFehlerAusgeben();
	
	FernschrAusschalten();

	Aktivieren(true);

	clr_LEDROT();
	}


/////////////////////////////////////////////////////////////

//! Schaltet das Modul in einen Modus, der keine kommenden Verbindungen zulässt.
//------------------------------------------------------------------------------
//! Kann durch Wahl einer entsprechenden Ziffernfolge aufgerufen werden oder
//! durch Tastendruck an der Platine oder durch die Zeitsperre oder durch eine 
//! Nichterreichbarkeit des Geräts
	
static void KommendSperren(TSperreGrund Grund)
	{
	TMsTimer BlinkTimer;
	uint8_t BlinkTaktFaktor;
	
	StartTimer(&BlinkTimer);
	Aktivieren(false);
	
	if (Grund == SperreTaste || Grund == SperreWahl)
		BlinkTaktFaktor = 2;
	else if (Grund == SperreZeit)
		BlinkTaktFaktor = 4;
	else
		BlinkTaktFaktor = 1;
	
	while (true)
		{
		TastePruefen();
		if (Tastendruck != NichtGedr)
			{
			Tastendruck = NichtGedr;
			SperrzeitAussetzen();
			break;
			}
			
		FernschrIO();
		if (MeldungEingeschaltet)
			break;
			
		if (TimerVal(&BlinkTimer) > 500 * BlinkTaktFaktor)
			StartTimer(&BlinkTimer);
		else if (TimerVal(&BlinkTimer) > 300 * BlinkTaktFaktor)
			set_LEDBLAU();
		else
			clr_LEDBLAU();
		
		if (RundsendAnzDaten > 0)
			{
			if (LokalUhrPruefeRundsendung(RundsendDaten, RundsendAnzDaten))
				{
				if (Grund == SperreZeit && !SperrzeitAktiv())
					{
					RundsendAnzDaten = 0;
					break;
					}
				}
			// else Daten anderwertig auswerten
			else
				RundsendAnzDaten = 0; // ungültige Daten loeschen
			
			RundsendAnzDaten = 0;
			}
		
		}
		
	clr_LEDBLAU();
	Aktivieren(true);
		
	}
	
	
/////////////////////////////////////////////////////////////

//! Schaltet das Modul in einen Modus, der keine kommenden und keine gehenden 
//! Verbindungen zulässt.
//------------------------------------------------------------------------------
//! Kann nur durch Tastendruck an der Platine aktiviert werden.
	
static void Deaktivieren()
// wird nach kurzem Tastendruck aufgerufen
	{
	set_LEDBLAU();
	Aktivieren(false);

	while (Tastendruck == NichtGedr)
		TastePruefen();
	Tastendruck = NichtGedr;

	Aktivieren(true);
	clr_LEDBLAU();
	clr_LEDROT();

	KommendSperren(SperreTaste);
		
	} // Deaktivieren

	
/////////////////////////////////////////////////////////////

//! Liest aus dem EEPROM einen Datenblock als Codefolge, prüft ob dieser Block
//! korrekt ist (nur Werte von 32 bis 63 und 0) und initialisiert
//! ggf. ungültige Codefolgen
//------------------------------------------------------------

void CodefolgeLadenPruefenInitialisieren(uint8_t* cf, uint8_t size, uint8_t cf_eep_adr, const uint8_t* cf_default, uint8_t def_size)
	{
	uint8_t i;

	for (i = 0 ; i < size ; i++)
		{
		cf[i] = KonfigLeseByte(cf_eep_adr + i, 0xFF); // 0xFF als Kennzeichen, dass tatsächlich was schiefgegangen ist.
		if (cf[i] == 0)
			return; // 0 heißt wie beim String "Ende".
		else if (cf[i] == 0x5A) // historische Ende-Marke.
			{
			cf[i] = 0;
			return; // alles ist schön
			}
		else if (ValidCode(cf[i]))
			;					// 32 bis 63: neue Version des Konfig-Speicher-Inhalts. 
		else if (cf[i] <= 0x1F) // 1 bis 31: alte Version des Konfig-Speicher-Inhalts.
			cf[i] |= (1<<5); // neu mit gesetztem Bit 5
		else // alles andere: Müll -> Initialisieren
			{
			for (i = 0 ; i < def_size ; i++)
				cf[i] = pgm_read_byte(cf_default + i) | (1<<5);
			cf[def_size] = 0;
			return;
			}
		}
	} // CodefolgeLadenPruefenInitialisieren()


/////////////////////////////////////////////////////////////

//! Das Hauptprogramm der Fernschreiber-Schnittstelle.
//---------------------------------------------------------


int main()
	{
#ifndef NOWATCHDOG
	wdt_enable(WDTO_2S);
#endif //NOWATCHDOG

	// Ports initialisieren
	init_LEDROT();
	init_LEDGELB();
	init_LEDGRUEN();
	init_LEDBLAU();
	init_FS_AUSG();
	init_FS_AKTIV();
	init_FS_EING(); 
	init_TASTE();
	init_TASTEEXT();
	
	//init_TASTE2();

	set_LEDROT();

	KonfigSpeicherInit();
	
	MsTimerInit();

	SperrzeitInit();

	pgm_read_byte(Identifier); // Dummy read to force the identifier to be placed in the FLASH.

	BusEigenAdresse = KonfigLeseByteBegrenzt(EEAdr_BusEigenAdresse, 11 << 1/*Standardwert*/, BusAdrMin, BusAdrMax) & 0xFE; // Bit 0 löschen
	BusEigenAdrMehrfach = 1;
	RundsendEmpfFreig = true;
	
	UmleitungAbweisen = KonfigLeseBool(EEAdr_UmleitungAbweisen, false);

	TasteFunktion = KonfigLeseByteBegrenzt(EEAdr_TasteFunktion, 0, 0, 2); // KEIN Bool

	SperrzeitLadeEeprom(EEAdr_Sperrzeit);

	StartQuittVerzoegerung = KonfigLeseByteBegrenzt(EEAdr_StartQuittVerz, StartQuittVerzoegerung_Std, StartQuittVerzoegerung_Min, 200);

	CodefolgeLadenPruefenInitialisieren(EigeneKennung, sizeof(EigeneKennung), EEAdr_EigeneKennung, EigeneKennungDefault, sizeof(EigeneKennungDefault));
	
	AnrufAbbruchZeit = KonfigLeseByteBegrenzt(EEAdr_AnrufAbbruchZeit, AnrufAbbruchZeit_Std, 3, 25);
		
	BefehlEinschalten = false;
	BefehlMark = true;
	MeldungEingeschaltet = false;
	MeldungMark = true;

	KommInit();
	
	TMsTimer Timer;
	StartTimer(&Timer);

	sei();
	FernschrIO();
	
	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 250)
		;
	
	// Bei Tastendruck Selbsttest
	bool SelbsttestAusfuehen = get_TASTE();

	/*/ Bei Tastendruck Watchdog AUS
	if (get_TASTE()) 
		{ 
		wdt_disable();
		set_LEDGELB();
		StartTimer(&Timer);
		}
	//*/

	clr_LEDROT();
	set_LEDGELB();

	TwiInit();

	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 500)
		;

	clr_LEDGELB();
	set_LEDGRUEN();

	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 750)
		;

	clr_LEDGRUEN();
	set_LEDBLAU();

	// TWI nochmal resetten
	TWCR = (1<<TWINT) | (0<<TWEA) | (0<<TWSTA) | (1<<TWSTO) | (0<<TWEN) | (0<<TWIE);

	while (TimerVal(&Timer) < 760)
		;
		
	TWCR = (1<<TWINT) | (1<<TWEA) | (0<<TWSTA) | (0<<TWSTO) | (1<<TWEN) | (1<<TWIE);

	while (TimerVal(&Timer) < 1000 + 20 * BusEigenAdresse)
		;

	if (SelbsttestAusfuehen)
		{
		BefehlEinschalten = false;
		MeldungEingeschaltet = false;
		BefehlMark = false;
		MeldungMark = false;

		StartTimer(&Timer);
		while (1)
			{
			if (get_TASTE())
				{ // gedrückt
				BefehlMark = false;
				}
			else
				{ // nicht gedrückt
				BefehlMark = true;
				StartTimer(&Timer);
				}
			FernschrIO();

			bset_LEDROT(BefehlEinschalten);
			bset_LEDGELB(MeldungEingeschaltet);
			bset_LEDGRUEN(BefehlMark);
			bset_LEDBLAU(MeldungMark);

			if (TimerVal(&Timer) > 1000)
				{
				BefehlEinschalten = !BefehlEinschalten;
				StartTimer(&Timer);
				}

			}
		} // if SelbsttestAusfuehren

	BusEigenAdressePruefenUndSetzen(BusEigenAdresse);

	BefehlEinschalten = false;
	BefehlMark = true;

	while (true)
		{
		// aktueller Zustand: Ausgeschaltet
		if (TimerVal(&Timer) <= 1200)
			clr_LEDROT();
		else if (TimerVal(&Timer) <= 1400)
			bset_LEDROT(BusEigenAdresse == BusAdrUngueltig);
		else
			StartTimer(&Timer);

		clr_LEDGELB();
		clr_LEDGRUEN();
		clr_LEDBLAU();

		TastePruefen();
		
		FernschrIO();

		if (Tastendruck == Lang)
			{
			Tastendruck = NichtGedr;
			Konfiguration();
			KonfigurationEnde();
			Tastendruck = NichtGedr;
			}

		if (Tastendruck == Kurz)
			{
			Tastendruck = NichtGedr;
			
/*/HACK:
			uint8_t MsgBuf[25];
			uint8_t MsgLen;
			MsgLen = LokalUhrBaudotAusgabe(MsgBuf);

			Aktivieren(false);
			if (FernschrEinschalten())
				{
				LokalCodeAusgabe(TtyCodeWR);
				LokalCodeAusgabe(TtyCodeZL);
				for (uint8_t i = 0 ; i < MsgLen ; i++)
					LokalCodeAusgabe(MsgBuf[i]);
				LokalCodeAusgabe(TtyCodeWR);
				LokalCodeAusgabe(TtyCodeZL);
				FernschrAusschalten();
				}
			Aktivieren(true);
//:HACK */			

			switch (TasteFunktion)
				{
				case DemoBetriebStarten:
					DemoBetrieb();
					break;
					
				case ExtStromEinschalten:
					break;
					
				default:
					Deaktivieren();
					break;
				}	
				
			} // if (Tastendruck == Kurz)

		if (KoEinschalten())
			{
			VerbindungKommend();
			}

		if (KonfigSpeicherFehlercode(false) != KonfigSpeicherOK)
			FehlermeldungDrucken();
		
		// Rundsendedaten auswerten:
		if (RundsendAnzDaten > 0)
			{
			if (LokalUhrPruefeRundsendung(RundsendDaten, RundsendAnzDaten))
				{
				if (SperrzeitAktiv())
					KommendSperren(SperreZeit);
				}
			else
				; // keine Ahnung, was hier gesendet wurde, ist aber auch egal...
				
			RundsendAnzDaten = 0;
			}
			
		} // while (true)
	} // main()


