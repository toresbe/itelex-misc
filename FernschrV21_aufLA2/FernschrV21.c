//================================================================
// Fernschreiber-Schnittstelle V21 für i-Telex-System
//	für ATmega168 auf Platine LeitungAnalog2 (mit veränderter Bestückung)
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

#include "73K221.h"

#include "../SvnVersion.h"


// Schalter für Code-Varianten
// ===========================

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
PROGMEM const char Identifier[] = "___itlx_FsV21-aufLA21-" PROGIDZUSATZ "___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#else
//! Marker im Code als Identifikation
PROGMEM const char Identifier[] = "___itlx_FsV21-aufLA21___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#endif

// Konstanten
// ----------

enum { LokalbetriebWahl_Std = 88 };
enum { KommendSperreWahl_Std = 0 };
enum { AutoWahlMaxZiffern = 10 };


// Eeprom-Speicher-Adressen
// ------------------------

enum {
	EEAdr_BusEigenAdresse = 0,
	EEAdr_KommendSperreWahl = 1,
    EEAdr_Sperrzeit = 2, // Beansprucht 20 Bytes
    EEAdr_TasteFunktion = 22,
    EEAdr_UmleitungAbweisen = 23,
    EEAdr_LokalbetriebWahl = 24,
    EEAdr_AutoWahlZiffern = 25,
    EEAdr_Ende = 26 // darf erhöht werden
};


// Typen
// -----

typedef enum { SperreTaste, SperreStoerung, SperreZeit, SperreWahl } TSperreGrund;

// Variablen
// ---------

bool BefehlEinschalten; //!< Fs soll laufen
bool BefehlMark; //!< Fs Schleifenstrom soll Ein sein
bool MeldungEingeschaltet; //!< Fs läuft tatsächlich
bool MeldungMark; //!< Fs Schleifenstrom ist Ein
bool SpaceSperre; 

TMsTimer AusschaltungTimer; //!< Zählt die Millisekunden von Schleifenunterbrechung bis Ausschaltung
TMsTimer EntprellungTimer; //!< Zählt die Millisekunden von Pegelwechsel am Port bis tatsächlichem Pegelwechsel


///////////////////////////////////////////////////////////////////////////////

//! bedient Hardware-IO entsprechend der aktuellen Zustände.

/*!
 * setzt FS_AKTIV und FS_AUSG entsprechend BefehlEinschalten und BefehlMark,
 * setzt MeldungEingeschaltet und MeldungMark entsprechend FS_EING,
 * steuert die Status-LEDs
 */ 

static void V21IO()
	{
	// Pegel & Polung ausgeben
	// -----------------------
	if (BefehlEinschalten && BefehlMark)
		clr_TXD();
	else
		{
		set_TXD();
		SpaceSperre = true;
		}
		
	// Schleifenstrom auswerten: Einschaltung oder nicht
	// -------------------------------------------------
	if (!get_RXD())
		{
		MeldungMark = true;
		SpaceSperre = false;
		}
	else 
		if (SpaceSperre)
			MeldungMark = true;
		else
			MeldungMark = false;
		
	if (get_RXD())
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
		
	if (BefehlEinschalten)
		bset_LEDBLAU(!BefehlMark);

	if (MeldungEingeschaltet)
		{
		if (BIT_IS_SET(Status, StatBit_AngerufenBelegt))
			bset_LEDGELB(!MeldungMark);
		else
			bset_LEDGRUEN(!MeldungMark);
		}
	
	} // V21IO
	
	
uint8_t KommendSperreWahl; //!< Welche Wahlnummer sperrt den Anschluss für ankommende Rufe

uint8_t LokalbetriebWahl; //!< Welche Wahlnummer aktiviert den simulieren Lokalbetrieb

uint8_t AutoWahlZiffern[AutoWahlMaxZiffern];
	//!< bei gehender Aktivierung wird sofort diese Nummer gewählt

typedef enum { Deaktivierung, DemoBetriebStarten } TTasteFunktion;

TTasteFunktion TasteFunktion; //!< Bisher möglich: 0 = deaktivierung, 1 = Demo-Betrieb

//////////////////////////////////////////////////////////////////

//! Einschaltung des Fs auslösen.
//-------------------------------
//! \returns Einschaltung wurde erfolgreich durch Endgerät quittiert.

static bool V21Einschalten()
	{
	TMsTimer StabilTimer;
	TMsTimer AbbruchTimer;
	
	StartTimer(&StabilTimer);
	StartTimer(&AbbruchTimer);
	do
		{
		BefehlEinschalten = true;
		BefehlMark = true;
		V21IO();
		if (!MeldungEingeschaltet)
			StartTimer(&StabilTimer);
		if (TimerVal(&AbbruchTimer) > 7000)
			{
			BefehlEinschalten = false;
			BefehlMark = true;
			V21IO();
			return false;
			}
		} while (TimerVal(&StabilTimer) < 300);
	return true;
	}
		

//////////////////////////////////////////////////////////////////

//! Ausschaltung des Fs auslösen.

static void V21Ausschalten()
	{
	TMsTimer Timer;
	
	if (MeldungEingeschaltet && !BefehlEinschalten)
		V21Einschalten(); // Rückgabewert ignorieren

	StartTimer(&Timer);
	do
		{
		BefehlEinschalten = false;
		BefehlMark = true;
		V21IO();
		if (MeldungEingeschaltet)
			StartTimer(&Timer);
		}
	while (TimerVal(&Timer) < 500);
	}
		

/////////////////////////////////////////////////////////////////////////////////////////7

void V21Init()
{
	ModemInit();
	ModemReset();
	StartV21(true); // Testen!
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
			else // Taste nicht gedrückt
				{ 
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
				} 
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
		V21IO();
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
		V21IO();
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
	V21IO();

	set_LEDGRUEN();
	set_LEDROT();
	
	if (!V21Einschalten())
		{ // Timeout...
		clr_LEDGRUEN();
		GeAusschalten(true);
		KommendSperren(SperreStoerung);
		clr_LEDROT();		
		return;
		}

	clr_LEDROT();
	
	if (GeEinschalten() != GeEinschAnrufquitt)
		{
		GeAusschalten(true);
		V21Ausschalten();
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
	
	if (!BefehlEinschalten) // offensichtlich Wählscheiben-Wahl
		{
		V21Einschalten();

		TMsTimer AnlaufTimer;
		StartTimer(&AnlaufTimer);
		while (TimerVal(&AnlaufTimer) < 500) 
			V21IO();
		}
	
	LokalTextAusgabeP(PSTR("\r\nloc\r\n"));

	while(MeldungEingeschaltet)
		{
		V21IO();
		}

	V21Ausschalten();
	
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
		V21Ausschalten();
	while (MeldungEingeschaltet)
		V21IO();
	clr_LEDROT();
	Aktivieren(true);
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
	
	if (!V21Einschalten())
		return false;

	SeriellUmsetzInit();
		
	LokalCodeAusgabe(TtyCodeZiUm);
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
		V21IO();
		
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
			else if (c != 0 && c != ' ' && c != '\r' && c != '\n')
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
			// V21Ausschalten() macht die aufrufende Routine
			return false;
			}
			
		} // while (true)
	}
	
  
/////////////////////////////////////////////////////////////

//! Wickelt ausgehende Verbdindungen vollständig ab.
//--------------------------------------------------
//! Ruft VerbindungSteht() auf. Kehrt erst nach Verbindungsabbau wieder zurück.

static void VerbindungGehend()
	{
	if (BusEigenAdresse == BusAdrUngueltig)
		{
		V21Ausschalten();
		return;
		}

	SperrzeitAussetzen();
	
	set_LEDGELB();
	
	switch (GeEinschalten())
		{ // hier nur break benutzen, wenn Einschaltung erfolgreich
		case GeEinschFehler:
			clr_LEDGELB();
			
			return; 

		case GeEinschWahl:
			if (WahlMitTastatur())
				// Einschalten ist nicht erforderlich, da schon eingeschaltet ist...
				break; // ist jetzt verbunden
				
			GeAusschalten(true);
			
			if (KommendSperreWahl != 0 && LetzteInterneWahl() == KommendSperreWahl)
				{
				clr_LEDGELB();
				V21Ausschalten();
				KommendSperren(SperreWahl);
				}
				
			else if ((LokalbetriebWahl != 0 && LetzteInterneWahl() == LokalbetriebWahl)
				|| LetzteInterneWahl() == (BusEigenAdresse >> 1))
				{
				LokalbetriebSimulieren();
					// macht auch am Ende V21Ausschalten()
				}
				
			else
				V21Ausschalten();
				
			return;

/*
		case GeEinschSofortEin:
			V21Einschalten();
			break; // ist jetzt Verbunden

		case GeEinschFremdKonfig:
			V21Einschalten();
// passt nicht mehr...			LeitungsSstKonfigurationsDialog();
			V21Ausschalten();
			GeAusschalten();
			return; // keine normale Verbindung
*/

		default:
			FehlerStop(15); // TODO
			return;
		}

	VerbindungSteht(true); // wenn keine Wählscheibe, dann automatische Kennungsgeber-Abfrage

	SperrzeitAussetzen();
	
	}


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
	bool ErsteKennungAbfrage;
	
	StartTimer(&KennungAbfrageTimer);
	ErsteKennungAbfrage = true;

	GeSendeMark(true); 

	SendeUmsetzModus = UmsetzFern;
	EmpfUmsetzModus = UmsetzFern; 

	while (true)
		{
		V21IO();
		TastePruefen();

		if (!MeldungEingeschaltet)
			{
			GeAusschalten(false);
			V21Ausschalten();
			while (!KoAusschalten())
				V21IO();
			return;
			}

		if (KoAusschalten())
			{
			V21Ausschalten();
			GeAusschalten(false); // da braucht auf nichts mehr gewartet zu werden
			return;
			}

		BefehlMark = KoEmpfMark();
	
		if (SerUmSendBitNr <= SerUmSendStart) // Start oder Warten...
			GeSendeMark(MeldungMark); // Nur Fs-Pegel direkt auf Bus, wenn nicht seriell gesendet wird...
			
		// Der Empfangspuffer wird im Regelbetrieb nicht benutzt, da die Bitwechsel direkt an das Endgeraet
		// gesendet werden. Die empfangenen Zeichen werden daher nur ausgewertet, ob die Gegenstelle schon
		// 'sinnvolle' Zeichen gesendet hat. Falls ja, braucht der Kennungsgeber nicht mehr abgefragt zu werden.
		while (!PufferLeer(&EmpfPuffer))
			{
			if (PufferAusg(&EmpfPuffer) != TtyCodeBuUm)
				AutoKennungAbfrage = false;
			}

		// Kennungsgeber alle 5 Sekunden abfragen, bis Gegenantwort kam...
		if (AutoKennungAbfrage 
			&& TimerVal(&KennungAbfrageTimer) >= (ErsteKennungAbfrage ? 500 : 5000))
			{
			PufferSpeich(&SendePuffer, TtyCodeZiUm);
			PufferSpeich(&SendePuffer, TtyCodeZiUm);
			PufferSpeich(&SendePuffer, TtyCodeZiWerDa);
			StartTimer(&KennungAbfrageTimer);
			ErsteKennungAbfrage = false;
			}
			
		// falls selber geschrieben wird, auch automatische Kennungsgeber-Abfrage
		// löschen
		if (TimerVal(&KennungAbfrageTimer) > 1000 && !MeldungMark)
			AutoKennungAbfrage = false;
			
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
	
	if (!V21Einschalten())
		return;
	
	set_LEDBLAU();
	
	p = DemoText;
	
	while (pgm_read_byte(p) != '\0')
		{
		LokalZeichenAusgabe(pgm_read_byte(p));	
			// macht intern V21IO also auch Schlusstaste-Erkennung
		p++;
		TastePruefen();
		if (Tastendruck != NichtGedr)
			break;
		if (!MeldungEingeschaltet)
			break;
		}

	Tastendruck = NichtGedr;
	V21Ausschalten();
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
	
	if (!V21Einschalten())
		return;
	
	BaudotMode_SetEmpfangen(BaudotMode); // damit auch eine BU-Umschaltung gesendet wird.
	
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR("\r\n configuration V21plus version " SVNVERSION " date " __DATE__));
#else
	LokalTextAusgabeP(PSTR("\r\n konfiguration V21plus version " SVNVERSION " datum " __DATE__));
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
		KommendSperreWahl = KommendSperreWahl_Std;
		LokalbetriebWahl = LokalbetriebWahl_Std;
		SperrzeitInit();
		TasteFunktion = 0;
		AutoWahlZiffern[0] = 255; // Ende-Kennzeichen
		LokalTextAusgabeP(PSTR("\r\n +++ \r\n\n\n\n"));
		return;
		}

	// Einschaltung der Sperre für kommende Rufe durch Wahl von...
	// -----------------------------------------------------------
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR("\r\n block incoming calls by: (cur. "));
#else
	LokalTextAusgabeP(PSTR("\r\n kommende anrufe sperren mit: (akt. "));
#endif //def SPRACHE_EN

	if (KommendSperreWahl != 0)
		LokalZahlAusgabe(KommendSperreWahl, 2);
	else
#ifdef SPRACHE_EN
		LokalTextAusgabeP(PSTR("off"));
#else
		LokalTextAusgabeP(PSTR("aus"));
#endif //def SPRACHE_EN

#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR(") new (0 = off):     "));
#else
	LokalTextAusgabeP(PSTR(") neu (0 = aus):     "));
#endif //def SPRACHE_EN

	if (LokalZahlEingabe(&KommendSperreWahl, 2) < 0)
		return;

	LokalTextAusgabeP(OkStrP);
	
	// Lokalbetrieb durch Wahl von...
	// ------------------------------
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR("\r\n local operation by number: (cur. "));
#else
	LokalTextAusgabeP(PSTR("\r\n lokalbetrieb waehlen mit: (akt. "));
#endif //def SPRACHE_EN

	if (LokalbetriebWahl != 0)
		LokalZahlAusgabe(LokalbetriebWahl, 2);
	else
#ifdef SPRACHE_EN
		LokalTextAusgabeP(PSTR("off"));
#else
		LokalTextAusgabeP(PSTR("aus"));
#endif //def SPRACHE_EN

#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR(") new (0 = off):     "));
#else
	LokalTextAusgabeP(PSTR(") neu (0 = aus):     "));
#endif //def SPRACHE_EN

	if (LokalZahlEingabe(&LokalbetriebWahl, 2) < 0)
		return;

	LokalTextAusgabeP(OkStrP);
	
	// Sperrzeiten
	// -----------
	if (!SperrzeitEingabeDialog())
		return;
	
	// Feste Verbindung
	// ----------------
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR("\r\n activate automated prefix dialing? current:   ")); 
#else	
	LokalTextAusgabeP(PSTR("\r\n automatische vorwahl aktivieren? aktuell:   ")); 
#endif //def SPRACHE_EN

	bool AutoWahlJa = AutoWahlZiffern[0] <= 9;

	LokalBoolAusgabe(AutoWahlJa);
	LokalTextAusgabeP(NeuStrP);

	if (LokalBoolEingabe(&AutoWahlJa) == 0)
		return;

	LokalTextAusgabeP(OkStrP);

	if (AutoWahlJa)
		{
#ifdef SPRACHE_EN
		LokalTextAusgabeP(PSTR("\r\n enter dialing digits, finish with + (cur.: "));
#else		
		LokalTextAusgabeP(PSTR("\r\n wahlziffern eingeben, ende mit + (akt.: "));
#endif //def SPRACHE_EN
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
					{
					LokalTextAusgabeP(OkStrP);
					return; // unverändert lassen
					}
				else
					break; // wie Ende behandeln
				}
			else if (c == '\0')
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
	V21Ausschalten();
	
	KonfigSchreibeByte(EEAdr_BusEigenAdresse, BusEigenAdresse);
	
	KonfigSchreibeByte(EEAdr_UmleitungAbweisen, UmleitungAbweisen);

	KonfigSchreibeByte(EEAdr_KommendSperreWahl, KommendSperreWahl);

	KonfigSchreibeByte(EEAdr_LokalbetriebWahl, LokalbetriebWahl);

	KonfigSchreibeByte(EEAdr_TasteFunktion, TasteFunktion);

	uint8_t i;
	for (i = 0 ; i < AutoWahlMaxZiffern ; i++)
		KonfigSchreibeByte(EEAdr_AutoWahlZiffern + i, AutoWahlZiffern[i]);
	
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
	
	if (!V21Einschalten())
		return;
	
	BaudotMode_SetEmpfangen(BaudotMode); // damit auch eine BU-Umschaltung gesendet wird.
	
	KonfigSpeicherFehlerAusgeben();
	
	V21Ausschalten();

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
			
		V21IO();
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
					break;
				}
			// else Daten anderwertig auswerten
			
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

//! Das Hauptprogramm der V21-Fernschreiber-Schnittstelle.
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
	init_TASTE();

	set_LEDROT();

	KonfigSpeicherInit();
	
	MsTimerInit();

	V21Init();
	
	SperrzeitInit();

	BusEigenAdresse = KonfigLeseByteBegrenzt(EEAdr_BusEigenAdresse, 21 << 1/*Standardwert*/, BusAdrMin, BusAdrMax) & 0xFE; // Bit 0 löschen
	BusEigenAdrMehrfach = 1;
	RundsendEmpfFreig = true;
	
	UmleitungAbweisen = KonfigLeseBool(EEAdr_UmleitungAbweisen, false);

	KommendSperreWahl = KonfigLeseByteBegrenzt(EEAdr_KommendSperreWahl, KommendSperreWahl_Std, 0, 99);

	LokalbetriebWahl = KonfigLeseByteBegrenzt(EEAdr_LokalbetriebWahl, LokalbetriebWahl_Std, 0, 99);

	SperrzeitLadeEeprom(EEAdr_Sperrzeit);

	TasteFunktion = KonfigLeseByteBegrenzt(EEAdr_TasteFunktion, 0, 0, 1); // KEIN Bool

	uint8_t i;
	for (i = 0 ; i < AutoWahlMaxZiffern ; i++)
		AutoWahlZiffern[i] = KonfigLeseByte(EEAdr_AutoWahlZiffern + i, 255); // nicht begrenzt, da alles über 9 das Endezeichen ist.
	
	BefehlEinschalten = false;
	BefehlMark = true;
	MeldungEingeschaltet = false;
	MeldungMark = true;

	KommInit();

	V21IO();
	
	TMsTimer Timer;
	StartTimer(&Timer);

	sei();
	
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
			V21IO();

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
		V21IO();

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
			if (V21Einschalten())
				{
				LokalCodeAusgabe(TtyCodeWR);
				LokalCodeAusgabe(TtyCodeZL);
				for (uint8_t i = 0 ; i < MsgLen ; i++)
					LokalCodeAusgabe(MsgBuf[i]);
				LokalCodeAusgabe(TtyCodeWR);
				LokalCodeAusgabe(TtyCodeZL);
				V21Ausschalten();
				}
			Aktivieren(true);
//:HACK */			

			switch (TasteFunktion)
				{
				case DemoBetriebStarten:
					DemoBetrieb();
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
		
		} // while (true)
	} // main()


