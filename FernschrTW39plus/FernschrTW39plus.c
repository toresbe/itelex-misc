//================================================================
// Fernschreiber-Schnittstelle TW39 für TxP2-System
//	für ATmega168 auf Platine FernschrTW39
//================================================================
//				
//  
//================================================================
// verwendete Pins siehe Ports.h
//================================================================

#include <stddef.h>
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

#ifdef V10

#ifdef PROGIDZUSATZ
//! Marker im Code als Identifikation
PROGMEM const char Identifier[] = "___itlx_V10-" PROGIDZUSATZ "___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#else
//! Marker im Code als Identifikation
PROGMEM const char Identifier[] = "___itlx_V10___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#endif

#else // not V10

#ifdef DOPPELSTROM

#ifdef PROGIDZUSATZ
//! Marker im Code als Identifikation
PROGMEM const char Identifier[] = "___itlx_Doppelstrom-" PROGIDZUSATZ "___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#else
//! Marker im Code als Identifikation
PROGMEM const char Identifier[] = "___itlx_Doppelstrom___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#endif

#else // not DOPPELSTROM

#ifdef PROGIDZUSATZ
//! Marker im Code als Identifikation
PROGMEM const char Identifier[] = "___itlx_TW39plus-" PROGIDZUSATZ "___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#else
//! Marker im Code als Identifikation
PROGMEM const char Identifier[] = "___itlx_TW39plus___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#endif

#endif // not DOPPELSTROM

#endif // not V10


#ifdef SPRACHE_EN
#define Sprachwahl(de, en) en
#else
#define Sprachwahl(de, en) de 
#endif


// Konstanten
// ----------

enum { LokalbetriebWahl_Std = 88 };
enum { KommendSperreWahl_Std = 0 };
enum { AutoWahlMaxZiffern = 10 }; // bei Änderung ist das EEPROM-Layout kompromittiert.
enum { AnrufAbbruchZeit_Std = 7 }; // sekunden
enum { StartQuittVerzoegerung_Std = 5 }; // x/10 sekunden
enum { StartQuittVerzoegerung_Min = 2 }; // x/10 sekunden


// Eeprom-Speicher-Adressen
// ------------------------

enum {
	EEAdr_BusEigenAdresse = 0,
    EEAdr_MitWaehlscheibe = 1,
    EEAdr_WahlauffordImpulsLaenge = 2,
    EEAdr_KommendSperreWahl = 3,
    EEAdr_Sperrzeit = 4, // Beansprucht 20 Bytes
    EEAdr_TasteFunktion = 24,
    EEAdr_UmleitungAbweisen = 25,
    EEAdr_LokalbetriebWahl = 26,
    EEAdr_AutoWahlZiffern = 27,
	EEAdr_AnrufAbbruchZeit = 37,
	EEAdr_StartQuittVerz = 38,
	EEAdr_SimulierteKennung = 39, 
	EEAdr_Ende = 70 // Platz für neue Werte, darf erhöht werden	
};


// Typen
// -----

typedef enum { SperreTaste, SperreStoerung, SperreZeit, SperreWahl } TSperreGrund;


// Konfigurations-Variablen
// ====================


bool MitWaehlscheibe; //!< Gerät het eine Wählscheibe

uint8_t WahlauffordImpulsLaenge; //!< Länge des Wahlaufforderungsimpuls in 1/100 sek

uint8_t AnrufAbbruchZeit; //!< Maximale Zeit zwichen Aktivierung Anrufsignal und Ende des Hochlaufs des Fernschreibers

uint8_t StartQuittVerzoegerung; 
	//!< zusätzliche Zeit nach Empfang der Betriebsbereitschaft des 
	//!< Fernschreibers bis zur Meldung "Betriebsbereit" an den Verbindungspartner.

uint8_t KommendSperreWahl; //!< Welche Wahlnummer sperrt den Anschluss für ankommende Rufe

uint8_t LokalbetriebWahl; //!< Welche Wahlnummer aktiviert den simulieren Lokalbetrieb

uint8_t AutoWahlZiffern[AutoWahlMaxZiffern];
	//!< bei gehender Aktivierung wird sofort diese Nummer gewählt

typedef enum { Deaktivierung, DemoBetriebStarten, ExtStromEinschalten } TTasteFunktion;

TTasteFunktion TasteFunktion; //!< Bisher möglich: 0 = deaktivierung, 1 = Demo-Betrieb, 2 = Ausgang zum externen Schalter aktivieren

// Arbeits-Variablen 
// =======================

bool BefehlEinschalten; //!< Fs soll laufen (TW39) bzw. 'echt' Mark / Space bei Doppelstrom
bool BefehlMark; //!< Fs Schleifenstrom soll Ein sein
bool MeldungEingeschaltet; //!< Fs läuft tatsächlich
bool MeldungMark; //!< Fs Schleifenstrom ist Ein

TMsTimer AusschaltungTimer; //!< Zählt die Millisekunden von Schleifenunterbrechung bis Ausschaltung
TMsTimer EntprellungTimer; //!< Zählt die Millisekunden von Pegelwechsel am Port bis tatsächlichem Pegelwechsel
TMsTimer NachlaufTimer; //!< Steuert nur den Ausgang für den externen SV-Schalter



// Schnittstellen-Spezifische Variablen
// ====================================

// keine
enum { MaxCodefolgeLaenge = 30 }; //!< Maximale Länge von #SimulierteKennung

char SimulierteKennung[MaxCodefolgeLaenge+1];
	//!< Text des eigenen Kennungsgeber-Simulators
	

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
#ifdef DOPPELSTROM

	// der Ausgang FS_AKTIV wird separat gesteuert...

	bset_FS_AUSG(BefehlEinschalten && BefehlMark);
	#ifdef PARALLELAUSGABE
		bset_FS2_AUSG(BefehlEinschalten && BefehlMark);
	#endif //def PARALLELAUSGABE

#else // TW39

	// Bei Ein- oder Ausschaltung die Schleife auch kurz unterbrechen
	if (BefehlEinschalten != get_FS_AKTIV())
		{
		// zur Schonung des Relais den Schleifenstrom jetzt unterbrechen
		clr_FS_AUSG(); // Low -> Optokoppler durchgeschaltet -> Gate auf 0 -> trennung
		#ifdef PARALLELAUSGABE
			clr_FS2_AUSG(BefehlMark);
		#endif //def PARALLELAUSGABE

		TMsTimer Timer;

		StartTimer(&Timer);
		while (TimerVal(&Timer) < 15) // Strom abklingen lassen
			;

		bset_FS_AKTIV(BefehlEinschalten); // Relais schalten
		#ifdef PARALLELAUSGABE
			bset_FS2_AKTIV(BefehlEinschalten);
		#endif //def PARALLELAUSGABE

		StartTimer(&Timer);
		while (TimerVal(&Timer) < 15) // Schaltzeit des Relais abwarten
			;

		// das Wiedereinschalten der Schleife folgt gleich.
		}

	bset_FS_AUSG(BefehlMark);
	#ifdef PARALLELAUSGABE
		bset_FS2_AUSG(BefehlMark);
	#endif //def PARALLELAUSGABE

#endif

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
#ifdef DOPPELSTROM
	if (!MeldungEingeschaltet)
#else // TW39
	if (!BefehlMark || !MeldungEingeschaltet)
#endif
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

	if (BefehlEinschalten)
		bset_LEDBLAU(!BefehlMark);

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
	
	set_SV_EIN(); // externen Schalter der Energieversorgung des Fernschreibers einschalten
	StartTimer(&NachlaufTimer);

	if (BefehlEinschalten) // ist schon an, dann nicht verzögern
		WarteQuittVerz = false;
		
	do
		{
#ifdef DOPPELSTROM
		set_FS_AKTIV();
		#ifdef PARALLELAUSGABE
			set_FS2_AKTIV();
		#endif //def PARALLELAUSGABE
#endif
		BefehlEinschalten = true;
		BefehlMark = true;
		FernschrIO();
		if (!MeldungEingeschaltet)
			StartTimer(&StabilTimer);
		if (TimerVal(&AbbruchTimer) > AnrufAbbruchZeit * 1000)
			{ // Fernschreiber hat auf Einschaltkommando nicht reagiert -> wieder Ausschalten
			BefehlEinschalten = false;
			BefehlMark = true;
#ifdef DOPPELSTROM
			StartTimer(&AbbruchTimer);
			while (TimerVal(&AbbruchTimer) < 2000)
				FernschrIO();
			clr_FS_AKTIV();
			#ifdef PARALLELAUSGABE
				clr_FS2_AKTIV();
			#endif //def PARALLELAUSGABE
#endif
			FernschrIO();
			StartTimer(&NachlaufTimer);
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

	BefehlEinschalten = false;
	BefehlMark = true;

#ifdef DOPPELSTROM
	StartTimer(&Timer);
	do
		{
		FernschrIO();
		if (MeldungEingeschaltet)
			StartTimer(&Timer);
		} while (TimerVal(&Timer) < 1000);

	clr_FS_AKTIV(); // Ausgabe "weak space"
	#ifdef PARALLELAUSGABE
		clr_FS2_AKTIV();
	#endif //def PARALLELAUSGABE

#else // TW39

	StartTimer(&Timer);
	do
		{
		FernschrIO();
		if (MeldungEingeschaltet)
			StartTimer(&Timer);
		} while (TimerVal(&Timer) < 500);
	
#endif // TW39
	
	StartTimer(&NachlaufTimer); 
	
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

#ifdef DOPPELSTROM
	set_FS_AKTIV();
	#ifdef PARALLELAUSGABE
		set_FS2_AKTIV();
	#endif //def PARALLELAUSGABE
#endif

	BefehlEinschalten = false;
	BefehlMark = true;

	FernschrIO(); // damit der Fernschreiber abgeschaltet wird.
	clr_SV_EIN();
	
	uint8_t TasteZ = 0;
	bool TasteWirk = false;
	TMsTimer TasteTimer;
	StartTimer(&TasteTimer);
	while (true)
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
						while (true)
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
		TastePruefen();
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
		if (Tastendruck != NichtGedr)
			{
			Tastendruck = NichtGedr;
			return '\0'; // TODO prüfen ob die Abschaltung auch richtig funktioniert.
			}
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
			
		
///////////////////////////////////////////////////////////////

//! Dialog-Abfrage für einen Text der als 5-Bit-Code abgespeichert wird.
//----------------------------------------------------------------------
//! neuer Text muss durch druckbare Begrenzungszeichen eingeschlossen werden. z.B. xhallox für hallo
//! . (Punkt) als einziges Zeichen = alten Wert behalten.
//! \param[out] buf Puffer des eingegebenen Textes. Hinweis: Ende-Markierung ist 0x00, Bit 5 wird für alle Werte gesetzt!
//! \param[in] maxcodes Anzahl erlaubter codes bei der Eingabe, auch Puffergröße.
//! \retval 0 abbruch
//! \retval 1 unverändert
//! \retval 2 eingabe erfolgt
//! \todo mal nach KonfigDialog verschieben, da aber LokalZeichenLesen nicht verwendet werden kann, muss eine größere Umstellung gemacht werden.


uint8_t LokalCodefolgeEingabe(PGM_P Prompt, uint8_t* buf, uint8_t maxcodes)
	{
	uint8_t Pos = 0; 
	char TrennZeichen; // Zeichen für noch nicht belegt.

	if (Prompt != NULL)
		LokalTextAusgabeP(Prompt);
	
	TrennZeichen = '\0';

	while (true)
		{ // Schleifendurchlauf einmal je Taste
		uint8_t code;
		char zeichen; 
		
		while (true)
			{ // Schleifendurchlauf bis ein Zeichen eingegeben oder Abbruch
			FernschrIO(true);
			SeriellUmsetzung(MeldungMark, &BefehlMark);
			if (SerUmEmpfBitNr == SerUmEmpfFertig)
				{
				code = SerUmEmpfDaten;
				SerUmEmpfBitNr = SerUmEmpfWarte;
				break;
				}
			}

		zeichen = CodeZuZeichen(code, &BaudotMode);
		
		if (TrennZeichen != '\0')
			{ // Zeichenfolge wurde bereits begonnen.
			if (zeichen == TrennZeichen)
				{
				if (Pos < maxcodes-1)
					buf[Pos] = 0;
				LokalTextAusgabeP(OkStrP);
				return 2;
				}
			else
				{
				if (Pos < maxcodes-1)
					buf[Pos++] = code | (1<<5);
				}
			} // if TrennZeichen != '\0'
		else // TrennZeichen == '\0'
			{ // Trennzeichen wurde noch nicht wirksam eingegebenen
			if (zeichen == '.')
				{ // vorhandenen Wert beibehalten
				LokalTextAusgabeP(OkStrP);
				return 1;
				}
			else if (zeichen != '#' && zeichen > ' ') // nicht ungültig und kein Leerzeichen
				TrennZeichen = zeichen;
			} // else Trennzeichen == '\0'

		} // while true
	} // LokalCodefolgeEingabe

		
static void VerbindungSteht(bool AutoKennungAbfrage);

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
		VerbindungSteht(false); // Keine automatische Kennungsgeber-Abfrage
		
	}
	

	
/////////////////////////////////////////////////////////////

//! Wird aufgerufen, wenn durch Wahl der entsprechenden Nummer oder
//! durch Buchstabe "L" bei Tastaturwahl ein Lokalbetrieb laufen soll.

static void LokalbetriebSimulieren()
	{
	set_LEDROT();
	Aktivieren(false);
	
	BefehlMark = true;
	
	if (MitWaehlscheibe)
		{
		FernschrEinschalten(true);
		}
		// sonst ist der Fs schon eingeschaltet.
	
	LokalTextAusgabeP(PSTR("\r\nloc\r\n"));

	while (MeldungEingeschaltet)
		{
		FernschrIO();
		}

	FernschrAusschalten();

	Aktivieren(true);
	clr_LEDROT();
	}


/////////////////////////////////////////////////////////////

//! Wird aufgerufen, wenn bei gehender Verbindung zu lange nicht gewählt wird.
//----------------------------------------------
//! \param Abschaltimpuls soll ein Schlusszeichen an das Endgerät gesendet werden?

static void AbschaltungZuLangeWahlpause(bool Abschaltimpuls)
	{
	set_LEDROT();
	GeAusschalten(true);
	Aktivieren(false);
	
	if (Abschaltimpuls)
		FernschrAusschalten();
	else
		StartTimer(&NachlaufTimer);
	
	while (MeldungEingeschaltet)
		FernschrIO();
	
	clr_LEDROT();
	Aktivieren(true);
	}
	
	
/////////////////////////////////////////////////////////////

//! Wickelt die gehende Wahl ab bei vorhandener Wählscheibe.
//----------------------------------------------
//! \retval true bei erfolgreichem Verbindungsaufbau.

static bool WahlMitWaehlscheibe()
	{
	uint8_t Wahlziffer;
	TMsTimer WahlendeTimer;
	bool EsWurdeGewaehlt;

#ifdef DOPPELSTROM
	set_FS_AKTIV();
	#ifdef PARALLELAUSGABE
		set_FS2_AKTIV();
	#endif //def PARALLELAUSGABE
#endif

	BefehlEinschalten = false;
	BefehlMark = true;

	// kurzzeitiger Missbrauch von WahlendeTimer für...
	// Pause vor dem Wahlaufforderungsimpuls

	StartTimer(&WahlendeTimer);
	EsWurdeGewaehlt = false;
	while (TimerVal(&WahlendeTimer) < 700)
		FernschrIO();

	// jetzt der wirkliche Wahlaufforderungsimpuls
#ifdef DOPPELSTROM
	BefehlEinschalten = true; // Mark-Signal
#else // TW39
	BefehlMark = false; // Schleifenunterbrechung bei Ruhepolarität
#endif
	StartTimer(&WahlendeTimer);
	while (TimerVal(&WahlendeTimer) < 10 * WahlauffordImpulsLaenge) 
		FernschrIO();
		
#ifdef DOPPELSTROM
	BefehlEinschalten = false; // wieder Space
#else // TW39
	BefehlMark = true; // Schleife ein mit weiter Ruhepolarität
#endif

	// AutoWahlZiffern vorweg in den Wählpuffer schreiben
	for (Wahlziffer = 0 ; Wahlziffer < AutoWahlMaxZiffern ; Wahlziffer++)
		// Wahlziffer wird als Index missbraucht
		if (AutoWahlZiffern[Wahlziffer] <= 9)
			GeWaehlen(AutoWahlZiffern[Wahlziffer]);
		else
			break;
		
	Wahlziffer = 0;
	StartTimer(&WahlendeTimer); // der Timer prüft auch, ob überhaupt gewählt wird...
	while (true)
		{
		FernschrIO();
		if (!MeldungMark)
			{ // Pause durch Wählscheibe
			Wahlziffer++;
			do
				FernschrIO();
			while (!MeldungMark && MeldungEingeschaltet);
			StartTimer(&WahlendeTimer);
			}
		
		if (!MeldungEingeschaltet || KoAusschalten())
			return false;
			
		if (Wahlziffer > 0 && TimerVal(&WahlendeTimer) > 200)
			{
			if (Wahlziffer > 9)
				GeWaehlen(0);
			else
				GeWaehlen(Wahlziffer);
			Wahlziffer = 0;
			EsWurdeGewaehlt = true;
			}
			
		if (KoEinschalten())
			return true;

		if (TimerVal(&WahlendeTimer) > (EsWurdeGewaehlt ? 45000 : 15000)) // 15 / 45 Sekunden nicht gewählt
			{ // auf das Ausschalten durch die Schlusstaste warten
			AbschaltungZuLangeWahlpause(EsWurdeGewaehlt);
			return false;
			}
		} // while (true)
	}
	
	
/////////////////////////////////////////////////////////////

//! Wickelt die gehende Wahl ab bei nicht vorhandener Wählscheibe.
//----------------------------------------------------------------
//! Tastaturwahl. 
//! \retval true bei erfolgreichem Verbindungsaufbau.

static bool WahlMitTastatur()
	{
	char c;
	TMsTimer WahlendeTimer;
	bool EsWurdeGewaehlt;
	int Falschziffern;
	
	if (!FernschrEinschalten(false))
		return false;

	SeriellUmsetzInit();

	BaudotMode_SetZiffern(BaudotMode);

	// AutoWahlZiffern vorweg in den Wählpuffer schreiben
	uint8_t i;
	for (i = 0 ; i < AutoWahlMaxZiffern ; i++)
		// c wird als Index missbraucht
		if (AutoWahlZiffern[i] <= 9)
			GeWaehlen(AutoWahlZiffern[i]);
		else
			break;

	StartTimer(&WahlendeTimer);
	EsWurdeGewaehlt = false;
	Falschziffern = 0;

	while (true)
		{
		FernschrIO();
		
		SeriellUmsetzung(MeldungMark, &BefehlMark);

		if (SerUmEmpfBitNr == SerUmEmpfFertig)
			{
			c = CodeZuZeichen(SerUmEmpfDaten, &BaudotMode);
			SerUmEmpfBitNr = SerUmEmpfWarte;
			if (c >= '0' && c <= '9')
				{
				GeWaehlen(c - '0');
				EsWurdeGewaehlt = true;
				}
			else if (c == 'l' && !EsWurdeGewaehlt)
				{ 
				LokalbetriebSimulieren();
				return false;
				}
			else if (c != 0 && c != ' ' && c != '+' && c != '\r' && c != '\n')
				{
				Falschziffern++;
				}

			StartTimer(&WahlendeTimer);
			}

		while (Falschziffern > 0 && TimerVal(&WahlendeTimer) >= 200)
			{
			LokalZeichenAusgabe('?');
			Falschziffern--;
			}

		if (KoEinschalten())
			{
			return true;
			}

		if (TimerVal(&WahlendeTimer) > (EsWurdeGewaehlt ? 45000 : 15000)) // 15 / 45 Sekunden nicht gewählt
			{ // auf das Ausschalten durch die Schlusstaste warten
			AbschaltungZuLangeWahlpause(EsWurdeGewaehlt);
			return false;
			}

		if (!MeldungEingeschaltet || KoAusschalten())
			{
			// FernschrAusschalten() macht die aufrufende Routine
			return false;
			}
			
		} // while (true)
	}
	
  
/////////////////////////////////////////////////////////////

//! Wickelt ausgehende Verbindungen vollständig ab.
//--------------------------------------------------
//! Ruft VerbindungSteht() auf. Kehrt erst nach Verbindungsabbau wieder zurück.

static void VerbindungGehend()
	{
	if (BusEigenAdresse == BusAdrUngueltig)
		{
		FernschrAusschalten();
		return;
		}

	SperrzeitAussetzen();
	
	set_LEDGELB();
	
	if (!GeAnrufBeginn())
		{
		clr_LEDGELB();
		return; 
		}

	bool Verbunden = false;
	
	if (MitWaehlscheibe)
		{
		Verbunden = WahlMitWaehlscheibe();
		if (Verbunden)
			FernschrEinschalten(true);
		}
	else // ohne Waehlscheibe
		{
		Verbunden = WahlMitTastatur();
		}

	if (!Verbunden)
		{
		GeAusschalten(true);
		
		if (KommendSperreWahl != 0 && LetzteInterneWahl() == KommendSperreWahl)
			{
			clr_LEDGELB();
			FernschrAusschalten();
			KommendSperren(SperreWahl);
			}
			
		else if ((LokalbetriebWahl != 0 && LetzteInterneWahl() == LokalbetriebWahl)
			|| LetzteInterneWahl() == (BusEigenAdresse >> 1))
			{
			LokalbetriebSimulieren();
				// macht auch am Ende FernschrAusschalten()
			}
			
		else
			FernschrAusschalten();
		}
	else // Verbunden = true
		{ 
		if (GeEinschaltQuittung())  // endgültige Einschaltung bestätigen
			VerbindungSteht(!MitWaehlscheibe); // wenn keine Wählscheibe, dann automatische Kennungsgeber-Abfrage
		else
			{ // Fehler
			GeAusschalten(true);
			FernschrAusschalten();
			}
		}
		
	SperrzeitAussetzen();
	
	} // VerbindungGehend()


/////////////////////////////////////////////////////////////

//! Behandelt alle Ereignisse, wenn die Verbindung erfolgreich aufgebaut wurde.
//-----------------------------------------------------------------------------
//! Arbeitet sowohl bei gehender, als auch bei kommender Verbindung.
//! \param AutoKennungAbfrage true, wenn automatisch die Kennung der Gegenstelle 
//! abgerufen werden soll. Abfrage wird solange wiederholt, bis eine lesbare Antwort 
//! eintrifft.

static void VerbindungSteht(bool AutoKennungAbfrage)
	{
	TMsTimer KennungAbfrageTimer;
	uint8_t KennungAbfrageZaehler;
	
	bool Bit5unterdruecken; // sperrt WerDa und F auf der Ziffernseite
	bool ZiffernEbene;
	
	StartTimer(&KennungAbfrageTimer);
	KennungAbfrageZaehler = 0;

	GeSendeMark(true); 

	SendeUmsetzModus = UmsetzFern; // Für das Senden von WerDa.
	EmpfUmsetzModus = UmsetzFern; 
	Bit5unterdruecken = false;
	ZiffernEbene = true; // sicherheitshalber

	while (true)
		{
		FernschrIO();
		TastePruefen();

		if (!MeldungEingeschaltet)
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
			
		// Der Empfangspuffer wird im Regelbetrieb nicht benutzt, da die Bitwechsel direkt an das Endgeraet
		// gesendet werden. Die empfangenen Zeichen werden daher nur ausgewertet, ob die Gegenstelle schon
		// 'sinnvolle' Zeichen gesendet hat. Falls ja, braucht der Kennungsgeber nicht mehr abgefragt zu werden.
		while (!PufferLeer(&EmpfPuffer))
			{
			uint8_t code = PufferAusg(&EmpfPuffer);
			
			if (code != TtyCodeBuUm)
				AutoKennungAbfrage = false;
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
				if (SimulierteKennung[0] != '\0' && code == TtyCodeZiWerDa && ZiffernEbene)
					{
					SendeUmsetzModus = UmsetzLokalUndFern;
					GeSendeCode(TtyCodeBuUm);
					GeSendeCode(TtyCodeWR);
					GeSendeCode(TtyCodeZL);
					for (uint8_t i = 0 ; i < MaxCodefolgeLaenge && SimulierteKennung[i] != '\0'; i++)
						GeSendeZeichen(SimulierteKennung[i]);
					}
				}
			}
			
		// Kennungsgeber alle 5 Sekunden abfragen, bis Gegenantwort kam...
		if (AutoKennungAbfrage 
			&& TimerVal(&KennungAbfrageTimer) >= ((KennungAbfrageZaehler == 0) ? 500 : 5000))
			{
			SendeUmsetzModus = UmsetzFern;
			PufferSpeich(&SendePuffer, TtyCodeZiUm);
			PufferSpeich(&SendePuffer, TtyCodeZiUm);
			PufferSpeich(&SendePuffer, TtyCodeZiWerDa);
			StartTimer(&KennungAbfrageTimer);
			KennungAbfrageZaehler++;
			if (KennungAbfrageZaehler >= 5)
				AutoKennungAbfrage = false;
			}
			
		// falls selber geschrieben wird, auch automatische Kennungsgeber-Abfrage
		// löschen
		if (TimerVal(&KennungAbfrageTimer) > 1000 && !MeldungMark)
			AutoKennungAbfrage = false;

		// Verbotene Codes unterdrücken:
		if (!Bit5unterdruecken && ZiffernEbene && SerUmEmpfBitNr == 6 /*5.Datenbit*/ && SerUmEmpfDaten == (TtyCodeZiWerDa >> 1) && SimulierteKennung[0] != '\0') 
			Bit5unterdruecken = true;
		if (Bit5unterdruecken && (SerUmEmpfBitNr == SerUmEmpfFertig || SerUmEmpfBitNr == SerUmEmpfWarte))
			Bit5unterdruecken = false;

		// Test:
		// bset_LEDROT(AutoKennungAbfrage);
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
	
#ifdef DOPPELSTROM
	LokalTextAusgabeP(PSTR(Sprachwahl("\r\n konfiguration doppelstrom version " SVNVERSION " datum " __DATE__,
									  "\r\n configuration double current version " SVNVERSION " date " __DATE__)));
#else
	LokalTextAusgabeP(PSTR(Sprachwahl("\r\n konfiguration tw39plus version " SVNVERSION " datum " __DATE__,
 									  "\r\n configuration tw39plus version " SVNVERSION " date " __DATE__)));
#endif //ndef DOPPELSTROM

	// Vorab die Frage nach "Expertenfunktionen"
	// -----------------------------------------
	LokalTextAusgabeP(PSTR(Sprachwahl("\r\n einfache konfiguration?    ", "\r\n simple configuration?    "))); 

	if (LokalBoolEingabe(&NoExpertSettings) == 0)
		return;

	LokalTextAusgabeP(OkStrP);

	// Durchwahl und co.
	// -----------------
	Abbruch = !KonfigurationAllgemein(); 
	if (Abbruch) 
		return;
	
	// Wählscheibe vorhanden?
	// ----------------------
	LokalTextAusgabeP(PSTR(Sprachwahl("\r\n waehlscheibe vorhanden? aktuell: ", "\r\n has rotary dial? current: "))); 

	LokalBoolAusgabe(MitWaehlscheibe);
	LokalTextAusgabeP(NeuStrP);

	if (LokalBoolEingabe(&MitWaehlscheibe) == 0)
		return;

	LokalTextAusgabeP(OkStrP);

	if (MitWaehlscheibe)
		{
		// Länge Wahlaufforderungsimpuls?
		LokalTextAusgabeP(PSTR(Sprachwahl("\r\n laenge wahlauff-imp.: akt. ", "\r\n duration dial proceed pulse: cur. ")));
		LokalZahlAusgabe(WahlauffordImpulsLaenge, 0);
		LokalTextAusgabeP(PSTR(Sprachwahl("/100 sek, ", "/100 sec, ")));
		LokalTextAusgabeP(NeuStrP);

		if (LokalZahlEingabe(&WahlauffordImpulsLaenge, 0) < 0)
			return;

		if (WahlauffordImpulsLaenge < 1)
			WahlauffordImpulsLaenge = 1;

		LokalTextAusgabeP(OkStrP);
		}

	// jetzt bei einfacher Konfiguration abbrechen
	// -------------------------------------------
	if (NoExpertSettings)
		{
		KommendSperreWahl = KommendSperreWahl_Std;
		LokalbetriebWahl = LokalbetriebWahl_Std;
		AnrufAbbruchZeit = AnrufAbbruchZeit_Std;
		StartQuittVerzoegerung = StartQuittVerzoegerung_Std;
		SperrzeitInit();
		TasteFunktion = 0;
		AutoWahlZiffern[0] = 255; // Ende-Kennzeichen
		LokalTextAusgabeP(PSTR("\r\n +++ \r\n\n\n\n"));
		SimulierteKennung[0] = '\0';
		return;
		}

	// Einschaltung der Sperre für kommende Rufe durch Wahl von...
	// -----------------------------------------------------------
	LokalTextAusgabeP(PSTR(Sprachwahl("\r\n kommende anrufe sperren mit: (akt. ", 
									  "\r\n block incoming calls by: (cur. ")));

	if (KommendSperreWahl != 0)
		LokalZahlAusgabe(KommendSperreWahl, 2);
	else
		LokalTextAusgabeP(PSTR(Sprachwahl("aus", "off")));

	LokalTextAusgabeP(PSTR(Sprachwahl(") neu (0 = aus):     ", ") new (0 = off):     ")));

	if (LokalZahlEingabe(&KommendSperreWahl, 2) < 0)
		return;

	LokalTextAusgabeP(OkStrP);

	// Lokalbetrieb durch Wahl von...
	// ------------------------------
	LokalTextAusgabeP(PSTR(Sprachwahl("\r\n lokalbetrieb waehlen mit: (akt. ",
									  "\r\n local operation by number: (cur. ")));

	if (LokalbetriebWahl != 0)
		LokalZahlAusgabe(LokalbetriebWahl, 2);
	else
		LokalTextAusgabeP(PSTR(Sprachwahl("aus", "off")));

	LokalTextAusgabeP(PSTR(Sprachwahl(") neu (0 = aus):     ", ") new (0 = off):     ")));

	if (LokalZahlEingabe(&LokalbetriebWahl, 2) < 0)
		return;

	LokalTextAusgabeP(OkStrP);
	
	// Sperrzeiten
	// -----------
	if (!SperrzeitEingabeDialog())
		return;
	
	// Feste Verbindung
	// ----------------
	LokalTextAusgabeP(PSTR(Sprachwahl("\r\n automatische vorwahl aktivieren? aktuell:   ",
									  "\r\n activate automated prefix dialing? current:   "))); 

	bool AutoWahlJa = AutoWahlZiffern[0] <= 9;

	LokalBoolAusgabe(AutoWahlJa);
	LokalTextAusgabeP(NeuStrP);

	if (LokalBoolEingabe(&AutoWahlJa) == 0)
		return;

	LokalTextAusgabeP(OkStrP);

	if (AutoWahlJa)
		{
		LokalTextAusgabeP(PSTR(Sprachwahl("\r\n wahlziffern eingeben, ende mit + (akt.: ",
										  "\r\n enter dialing digits, finish with + (cur.: ")));

		uint8_t i;
		
		for (i = 0 ; i < AutoWahlMaxZiffern ; i++)
			if (AutoWahlZiffern[i] <= 9)
				LokalZahlAusgabe(AutoWahlZiffern[i], 0);
			else
				break;
			
		LokalTextAusgabeP(PSTR("+)\r\n  "));
		LokalTextAusgabeP(NeuStrP);

		i = 0;
		while (i < AutoWahlMaxZiffern - 1)
			{
			char c = LokalZeichenLesen();
			if (c >= '0' && c <= '9')
				AutoWahlZiffern[i++] = c - '0';
			else if (c == '+')
				break; // Eingabe beenden, setzt auch Ende-Zeichen
			else if (c == '=' || c == '.' || c == '/')
				{ 
				if (i == 0)
					i = AutoWahlMaxZiffern - 1; // damit nichts geändet wird
				break; // wie Ende behandeln
				}
			else if (c == '\0') // abbruch
				{
				if (i != 0)
					AutoWahlZiffern[i] = 255; // Ende-Zeichen
				// sonst unverändert lassen
				return; 
				}
			}
		AutoWahlZiffern[i] = 255; // Ende-Zeichen
		LokalTextAusgabeP(OkStrP);
		}
	else // not AutoWahlJa
		{
		AutoWahlZiffern[0] = 255;
		}
		
	// Timeout beim Anruf
	// ------------------
	LokalTextAusgabeP(PSTR(Sprachwahl("\r\n maximale hochlauf-zeit in sekunden (3-25, akt. ",
								      "\r\n timeout for incoming calls in seconds (3-25, cur. ")));

	LokalZahlAusgabe(AnrufAbbruchZeit, 0);

	LokalTextAusgabeP(PSTR(Sprachwahl(") neu:     ", ") new:     ")));

	if (LokalZahlEingabe(&AnrufAbbruchZeit, 0) < 0)
		return;
	if (AnrufAbbruchZeit < 3)
		AnrufAbbruchZeit = 3;
	else if (AnrufAbbruchZeit > 25)
		AnrufAbbruchZeit = 25;

	LokalTextAusgabeP(OkStrP);
		
	// Verzögerung der Rückmeldung des Starts des Fernschreibers
	// ---------------------------------------------------------
	LokalTextAusgabeP(PSTR(Sprachwahl("\r\n verzoegerung rueckmeldung fs-anlauf in /10 sekunden\r\n (3-200, akt. ",
								      "\r\n delay confirmation of startup in /10 seconds\r\n (3-200, cur. ")));

	LokalZahlAusgabe(StartQuittVerzoegerung, 0);

	LokalTextAusgabeP(PSTR(Sprachwahl(") neu:     ", ") new:     ")));

	if (LokalZahlEingabe(&StartQuittVerzoegerung, 0) < 0)
		return;
	if (StartQuittVerzoegerung < StartQuittVerzoegerung_Min)
		StartQuittVerzoegerung = StartQuittVerzoegerung_Min;
	else if (StartQuittVerzoegerung > 200)
		StartQuittVerzoegerung = 200;

	LokalTextAusgabeP(OkStrP);
		
	// Modus für Tastendruck
	// ---------------------
	LokalTextAusgabeP(PSTR(Sprachwahl("\r\n funktion taste am modul (akt. ",
	                                  "\r\n module button function (cur. ")));
	LokalZahlAusgabe(TasteFunktion, 0);
	LokalTextAusgabeP(PSTR(Sprachwahl(") neu:     ", ") new:     ")));

	if (LokalZahlEingabe(&TasteFunktion, 0) < 0)
		return;

	LokalTextAusgabeP(OkStrP);

	// Simulierte Kennung
	// ---------------------
	LokalTextAusgabeP(PSTR(Sprachwahl("\r\n kennungsgeber simulieren? aktuell:   ",
									  "\r\n activate simulated answerback? current:   "))); 

	bool SimulKennungJa = SimulierteKennung[0] != '\0';

	LokalBoolAusgabe(SimulKennungJa);
	LokalTextAusgabeP(NeuStrP);

	if (LokalBoolEingabe(&SimulKennungJa) == 0)
		return;

	LokalTextAusgabeP(OkStrP);

	if (SimulKennungJa)
		{
		LokalTextAusgabeP(PSTR(Sprachwahl("\r\n simulierter kennungsgeber (akt. ", 
										  "\r\n answerback simulation (cur. ")));
		LokalTextAusgabe(SimulierteKennung);
		LokalTextAusgabeP(PSTR(Sprachwahl(") neu:     ", ") new:     ")));
		if (LokalTextEingabe(SimulierteKennung, MaxCodefolgeLaenge) == 0)
			return;
		LokalTextAusgabeP(PSTR("\r\n"));
		LokalTextAusgabeP(OkStrP);
		}
	else
		SimulierteKennung[0] = '\0';
	
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

	KonfigSchreibeBool(EEAdr_MitWaehlscheibe, MitWaehlscheibe);

	KonfigSchreibeByte(EEAdr_WahlauffordImpulsLaenge, WahlauffordImpulsLaenge);

	KonfigSchreibeByte(EEAdr_KommendSperreWahl, KommendSperreWahl);

	KonfigSchreibeByte(EEAdr_LokalbetriebWahl, LokalbetriebWahl);

	KonfigSchreibeByte(EEAdr_TasteFunktion, TasteFunktion);

	KonfigSchreibeByte(EEAdr_AnrufAbbruchZeit, AnrufAbbruchZeit);

	KonfigSchreibeByte(EEAdr_StartQuittVerz, StartQuittVerzoegerung);
	
	uint8_t i;
	for (i = 0 ; i < AutoWahlMaxZiffern ; i++)
		KonfigSchreibeByte(EEAdr_AutoWahlZiffern + i, AutoWahlZiffern[i]);
	
	SperrzeitSpeicherEeprom(EEAdr_Sperrzeit);
	
	KonfigSchreibeString(EEAdr_SimulierteKennung, SimulierteKennung, MaxCodefolgeLaenge + 1);
	
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

//! Das Hauptprogramm der TW39-Fernschreiber-Schnittstelle.
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
#ifdef PARALLELAUSGABE
	init_FS2_AUSG();
	init_FS2_AKTIV();
	init_FS2_EING(); 
#endif //def PARALLELAUSGABE
	init_TASTE();
	init_SV_EIN();
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
	
	MitWaehlscheibe = KonfigLeseBool(EEAdr_MitWaehlscheibe, true);
	WahlauffordImpulsLaenge = KonfigLeseByteBegrenzt(EEAdr_WahlauffordImpulsLaenge, 3, 1, 200);

	UmleitungAbweisen = KonfigLeseBool(EEAdr_UmleitungAbweisen, false);

	KommendSperreWahl = KonfigLeseByteBegrenzt(EEAdr_KommendSperreWahl, KommendSperreWahl_Std, 0, 99);

	TasteFunktion = KonfigLeseByteBegrenzt(EEAdr_TasteFunktion, 0, 0, 2); // KEIN Bool

	SperrzeitLadeEeprom(EEAdr_Sperrzeit);

	LokalbetriebWahl = KonfigLeseByteBegrenzt(EEAdr_LokalbetriebWahl, LokalbetriebWahl_Std, 0, 99);
	
	StartQuittVerzoegerung = KonfigLeseByteBegrenzt(EEAdr_StartQuittVerz, StartQuittVerzoegerung_Std, StartQuittVerzoegerung_Min, 200);

	KonfigLeseString(EEAdr_SimulierteKennung, SimulierteKennung, MaxCodefolgeLaenge + 1, PSTR(""));

	uint8_t i;
	for (i = 0 ; i < AutoWahlMaxZiffern ; i++)
		AutoWahlZiffern[i] = KonfigLeseByte(EEAdr_AutoWahlZiffern + i, 255); // nicht begrenzt, da alles über 9 das Endezeichen ist.
	
	AnrufAbbruchZeit = KonfigLeseByteBegrenzt(EEAdr_AnrufAbbruchZeit, AnrufAbbruchZeit_Std, 3, 25);

#ifdef DOPPELSTROM
	set_FS_AKTIV();
	#ifdef PARALLELAUSGABE
		set_FS2_AKTIV();
	#endif //def PARALLELAUSGABE
#endif
		
	BefehlEinschalten = false;
	BefehlMark = true;

	MeldungEingeschaltet = false;
	MeldungMark = true;

	KommInit();
	
	TMsTimer Timer;
	StartTimer(&Timer);

	sei();
	FernschrIO(); 
	clr_LEDBLAU(); // Bei Doppelstrom wird mit Ausschaltsequenz initialisiert, dies schaltet die blaue LED ein.
	
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

#ifdef DOPPELSTROM
	clr_FS_AKTIV();
	#ifdef PARALLELAUSGABE
		clr_FS2_AKTIV();
	#endif //def PARALLELAUSGABE
#endif

	BefehlEinschalten = false;
	BefehlMark = true;
	FernschrIO();

	// TWI nochmal resetten
	TWCR = (1<<TWINT) | (0<<TWEA) | (0<<TWSTA) | (1<<TWSTO) | (0<<TWEN) | (0<<TWIE);

	while (TimerVal(&Timer) < 760)
		;
		
	TWCR = (1<<TWINT) | (1<<TWEA) | (0<<TWSTA) | (0<<TWSTO) | (1<<TWEN) | (1<<TWIE);

	while (TimerVal(&Timer) < 1000 + BusEigenAdresse)
		;

	if (SelbsttestAusfuehen)
		{
		BefehlEinschalten = false;
		MeldungEingeschaltet = false;
		BefehlMark = false;
		MeldungMark = false;

		StartTimer(&Timer);
		while (true)
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

	StartTimer(&NachlaufTimer);

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
					set_SV_EIN();
					StartTimer(&NachlaufTimer);
					break;
					
				default:
					Deaktivieren();
					break;
				}	
				
			} // if (Tastendruck == Kurz)

		if (MeldungEingeschaltet)
			{
			VerbindungGehend();
			}
			
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
			
		if (get_TASTEEXT())
			{
			set_SV_EIN();
			StartTimer(&NachlaufTimer);
			}
			
		if (TimerVal(&NachlaufTimer) > 60000)
			clr_SV_EIN();
		
		} // while (true)
	} // main()


