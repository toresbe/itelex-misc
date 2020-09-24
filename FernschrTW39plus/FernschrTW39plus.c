//================================================================
// Fernschreiber-Schnittstelle TW39 für TxP2-System
//	für ATmega168 auf Platine FernschrTW39
//================================================================
//		
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
PROGMEM const char Identifier[] = "___itlx_TW39plus-" PROGIDZUSATZ "___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#else
//! Marker im Code als Identifikation
PROGMEM const char Identifier[] = "___itlx_TW39plus___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#endif

// Konstanten
// ----------

enum { LokalbetriebWahl_Std = 88 };
enum { KommendSperreWahl_Std = 0 };
enum { AutoWahlMaxZiffern = 10 }; // bei Änderung ist das EEPROM-Layout kompromittiert.
enum { AnrufAbbruchZeit_Std = 7 }; // sekunden


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
	EEAdr_Ende = 38 // darf erhöht werden	
};


// Typen
// -----

typedef enum { SperreTaste, SperreStoerung, SperreZeit, SperreWahl } TSperreGrund;

// Variablen
// ---------

bool MitWaehlscheibe; //!< Gerät het eine Wählscheibe

uint8_t WahlauffordImpulsLaenge; //!< Länge des Wahlaufforderungsimpuls in 1/100 sek

uint8_t AnrufAbbruchZeit; //!< Maximale Zeit zwichen Aktivierung Anrufsignal und Ende des Hochlaufs des Fernschreibers


bool BefehlEinschalten; //!< Fs soll laufen
bool BefehlMark; //!< Fs Schleifenstrom soll Ein sein
bool MeldungEingeschaltet; //!< Fs läuft tatsächlich
bool MeldungMark; //!< Fs Schleifenstrom ist Ein

TMsTimer AusschaltungTimer; //!< Zählt die Millisekunden von Schleifenunterbrechung bis Ausschaltung
TMsTimer EntprellungTimer; //!< Zählt die Millisekunden von Pegelwechsel am Port bis tatsächlichem Pegelwechsel
TMsTimer NachlaufTimer; //!< Steuert nur den Ausgang für den externen SV-Schalter


///////////////////////////////////////////////////////////////////////////////

//! bedient Hardware-IO entsprechend der aktuellen Zustände.

/*!
 * setzt FS_AKTIV und FS_AUSG entsprechend BefehlEinschalten und BefehlMark,
 * setzt MeldungEingeschaltet und MeldungMark entsprechend FS_EING,
 * steuert die Status-LEDs
 */ 

static void TW39IO()
	{
	// Pegel & Polung ausgeben
	// -----------------------
	bset_FS_AKTIV(BefehlEinschalten);
	#ifdef PARALLELAUSGABE
		bset_FS2_AKTIV(BefehlEinschalten);
	#endif //def PARALLELAUSGABE

	if (BefehlEinschalten)
		bset_LEDBLAU(!BefehlMark);
	bset_FS_AUSG(BefehlMark);
	#ifdef PARALLELAUSGABE
		bset_FS2_AUSG(BefehlMark);
	#endif //def PARALLELAUSGABE

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
				if (TimerVal(&EntprellungTimer) > 3) // mindestens 3 ms konstant Space --> Space melden
					MeldungMark = false;
				}
			else // !MeldungMark
				StartTimer(&EntprellungTimer); // Regelzustand bei Space: Space wird auch gemeldet
			} // Strom ist aus
		else // !get_FS_EING()
			{ // Schleifenstrom fließt
			if (!MeldungMark)
				{ // der Applikation wird noch Space gemeldet
				if (TimerVal(&EntprellungTimer) > 3) // mindestens 3 ms konstant Mark --> Mark melden
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

	} // TW39IO
	
	
uint8_t KommendSperreWahl; //!< Welche Wahlnummer sperrt den Anschluss für ankommende Rufe

uint8_t LokalbetriebWahl; //!< Welche Wahlnummer aktiviert den simulieren Lokalbetrieb

uint8_t AutoWahlZiffern[AutoWahlMaxZiffern];
	//!< bei gehender Aktivierung wird sofort diese Nummer gewählt

typedef enum { Deaktivierung, DemoBetriebStarten, ExtStromEinschalten } TTasteFunktion;

TTasteFunktion TasteFunktion; //!< Bisher möglich: 0 = deaktivierung, 1 = Demo-Betrieb, 2 = Ausgang zum externen Schalter aktivieren

//////////////////////////////////////////////////////////////////

//! Einschaltung des Fs auslösen.
//-------------------------------
//! \returns Einschaltung wurde erfolgreich durch Endgerät quittiert.

static bool TW39Einschalten()
	{
	TMsTimer StabilTimer;
	TMsTimer AbbruchTimer;
	
	StartTimer(&StabilTimer);
	StartTimer(&AbbruchTimer);
	
	set_SV_EIN(); // externen Schalter der Energieversorgung des Fernschreibers einschalten
	StartTimer(&NachlaufTimer);
	
	do
		{
		BefehlEinschalten = true;
		BefehlMark = true;
		TW39IO();
		if (!MeldungEingeschaltet)
			StartTimer(&StabilTimer);
		if (TimerVal(&AbbruchTimer) > AnrufAbbruchZeit * 1000)
			{
			BefehlEinschalten = false;
			BefehlMark = true;
			TW39IO();
			StartTimer(&NachlaufTimer);
			return false;
			}
		} while (TimerVal(&StabilTimer) < 300);
	return true;
	}
		

//////////////////////////////////////////////////////////////////

//! Ausschaltung des Fs auslösen.

static void TW39Ausschalten()
	{
	TMsTimer Timer;
	
	if (MeldungEingeschaltet && !BefehlEinschalten)
		TW39Einschalten(); // Rückgabewert ignorieren

	StartTimer(&Timer);
	do
		{
		BefehlEinschalten = false;
		BefehlMark = true;
		TW39IO();
		if (MeldungEingeschaltet)
			StartTimer(&Timer);
		}
	while (TimerVal(&Timer) < 500);
	
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

	BefehlEinschalten = false;
	BefehlMark = true;
	TW39IO();
	clr_SV_EIN();
	
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
		TW39IO();
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
		TW39IO();
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
	TW39IO();

	set_LEDGRUEN();
	set_LEDROT();
	
	if (!TW39Einschalten())
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
		TW39Ausschalten();
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
		TW39Einschalten();

		TMsTimer AnlaufTimer;
		StartTimer(&AnlaufTimer);
		while (TimerVal(&AnlaufTimer) < 500) 
			TW39IO();
		}
	
	LokalTextAusgabeP(PSTR("\r\nloc\r\n"));

	while(MeldungEingeschaltet)
		{
		TW39IO();
		}

	TW39Ausschalten();

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
		TW39Ausschalten();
	else
		StartTimer(&NachlaufTimer);
	
	while (MeldungEingeschaltet)
		TW39IO();
	
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

	// kurzzeitiger Mißbrauch von WahlendeTimer für Wahlaufforderung: 0,3 Sek unterbrechung
	StartTimer(&WahlendeTimer);
	EsWurdeGewaehlt = false;
	while (TimerVal(&WahlendeTimer) < 700)
		TW39IO();

	BefehlMark = false;
	
	StartTimer(&WahlendeTimer);
	while (TimerVal(&WahlendeTimer) < 10 * WahlauffordImpulsLaenge) 
		TW39IO();
		
	BefehlMark = true;

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
		TW39IO();
		if (!MeldungMark)
			{ // Pause durch Wählscheibe
			Wahlziffer++;
			do
				TW39IO();
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
	
	if (!TW39Einschalten())
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
		TW39IO();
		
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
			// TW39Ausschalten() macht die aufrufende Routine
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
		TW39Ausschalten();
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
			if (MitWaehlscheibe)
				{
				if (WahlMitWaehlscheibe())
					{
					TW39Einschalten();
					break; // ist jetzt Verbunden
					}
				}
			else // ohne Waehlscheibe
				{
				if (WahlMitTastatur())
					// Einschalten ist nicht erforderlich, da schon eingeschaltet ist...
					break; // ist jetzt verbunden
				}
				
			GeAusschalten(true);
			
			if (KommendSperreWahl != 0 && LetzteInterneWahl() == KommendSperreWahl)
				{
				clr_LEDGELB();
				TW39Ausschalten();
				KommendSperren(SperreWahl);
				}
				
			else if ((LokalbetriebWahl != 0 && LetzteInterneWahl() == LokalbetriebWahl)
				|| LetzteInterneWahl() == (BusEigenAdresse >> 1))
				{
				LokalbetriebSimulieren();
					// macht auch am Ende TW39Ausschalten()
				}
				
			else
				TW39Ausschalten();
				
			return;

/*
		case GeEinschSofortEin:
			TW39Einschalten();
			break; // ist jetzt Verbunden

		case GeEinschFremdKonfig:
			TW39Einschalten();
// passt nicht mehr...			LeitungsSstKonfigurationsDialog();
			TW39Ausschalten();
			GeAusschalten();
			return; // keine normale Verbindung
*/

		default:
			FehlerStop(15); // TODO
			return;
		}

	VerbindungSteht(!MitWaehlscheibe); // wenn keine Wählscheibe, dann automatische Kennungsgeber-Abfrage

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
	bool ErsteKennungAbfrage;
	
	StartTimer(&KennungAbfrageTimer);
	ErsteKennungAbfrage = true;

	GeSendeMark(true); 

	SendeUmsetzModus = UmsetzFern;
	EmpfUmsetzModus = UmsetzFern; 

	while (true)
		{
		TW39IO();
		TastePruefen();

		if (!MeldungEingeschaltet)
			{
			GeAusschalten(false);
			TW39Ausschalten();
			while (!KoAusschalten())
				TW39IO();
			return;
			}

		if (KoAusschalten())
			{
			TW39Ausschalten();
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
	
	if (!TW39Einschalten())
		return;
	
	set_LEDBLAU();
	
	p = DemoText;
	
	while (pgm_read_byte(p) != '\0')
		{
		LokalZeichenAusgabe(pgm_read_byte(p));	
			// macht intern TW39IO also auch Schlusstaste-Erkennung
		p++;
		TastePruefen();
		if (Tastendruck != NichtGedr)
			break;
		if (!MeldungEingeschaltet)
			break;
		}

	Tastendruck = NichtGedr;
	TW39Ausschalten();
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
	
	if (!TW39Einschalten())
		return;
	
	BaudotMode_SetEmpfangen(BaudotMode); // damit auch eine BU-Umschaltung gesendet wird.
	
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR("\r\n configuration tw39plus version " SVNVERSION " date " __DATE__));
#else
	LokalTextAusgabeP(PSTR("\r\n konfiguration tw39plus version " SVNVERSION " datum " __DATE__));
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
	
	// Wählscheibe vorhanden?
	// ----------------------
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR("\r\n has rotary dial? current: ")); 
#else	
	LokalTextAusgabeP(PSTR("\r\n waehlscheibe vorhanden? aktuell: ")); 
#endif //def SPRACHE_EN

	LokalBoolAusgabe(MitWaehlscheibe);
	LokalTextAusgabeP(NeuStrP);

	if (LokalBoolEingabe(&MitWaehlscheibe) == 0)
		return;

	LokalTextAusgabeP(OkStrP);

	if (MitWaehlscheibe)
		{
		// Länge Wahlaufforderungsimpuls?
#ifdef SPRACHE_EN
		LokalTextAusgabeP(PSTR("\r\n duration call proceed pulse? cur. "));
#else		
		LokalTextAusgabeP(PSTR("\r\n laenge wahlauff-imp.? akt. "));
#endif //def SPRACHE_EN
		LokalZahlAusgabe(WahlauffordImpulsLaenge, 0);
		LokalTextAusgabeP(PSTR("/100 sek,"));
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
	TW39Ausschalten();
	
	KonfigSchreibeByte(EEAdr_BusEigenAdresse, BusEigenAdresse);
	
	KonfigSchreibeByte(EEAdr_UmleitungAbweisen, UmleitungAbweisen);

	KonfigSchreibeBool(EEAdr_MitWaehlscheibe, MitWaehlscheibe);

	KonfigSchreibeByte(EEAdr_WahlauffordImpulsLaenge, WahlauffordImpulsLaenge);

	KonfigSchreibeByte(EEAdr_KommendSperreWahl, KommendSperreWahl);

	KonfigSchreibeByte(EEAdr_LokalbetriebWahl, LokalbetriebWahl);

	KonfigSchreibeByte(EEAdr_TasteFunktion, TasteFunktion);

	KonfigSchreibeByte(EEAdr_AnrufAbbruchZeit, AnrufAbbruchZeit);

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
	
	if (!TW39Einschalten())
		return;
	
	BaudotMode_SetEmpfangen(BaudotMode); // damit auch eine BU-Umschaltung gesendet wird.
	
	KonfigSpeicherFehlerAusgeben();
	
	TW39Ausschalten();

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
			
		TW39IO();
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

	// Testweise umsortiert:
	TasteFunktion = KonfigLeseByteBegrenzt(EEAdr_TasteFunktion, 0, 0, 1); // KEIN Bool

	SperrzeitLadeEeprom(EEAdr_Sperrzeit);

	KommendSperreWahl = KonfigLeseByteBegrenzt(EEAdr_KommendSperreWahl, KommendSperreWahl_Std, 0, 99);

	BusEigenAdresse = KonfigLeseByteBegrenzt(EEAdr_BusEigenAdresse, 11 << 1/*Standardwert*/, BusAdrMin, BusAdrMax) & 0xFE; // Bit 0 löschen
	BusEigenAdrMehrfach = 1;
	RundsendEmpfFreig = true;
	
	UmleitungAbweisen = KonfigLeseBool(EEAdr_UmleitungAbweisen, false);

	MitWaehlscheibe = KonfigLeseBool(EEAdr_MitWaehlscheibe, true);

	WahlauffordImpulsLaenge = KonfigLeseByteBegrenzt(EEAdr_WahlauffordImpulsLaenge, 20, 1, 100);

	LokalbetriebWahl = KonfigLeseByteBegrenzt(EEAdr_LokalbetriebWahl, LokalbetriebWahl_Std, 0, 99);


	uint8_t i;
	for (i = 0 ; i < AutoWahlMaxZiffern ; i++)
		AutoWahlZiffern[i] = KonfigLeseByte(EEAdr_AutoWahlZiffern + i, 255); // nicht begrenzt, da alles über 9 das Endezeichen ist.
	
	AnrufAbbruchZeit = KonfigLeseByteBegrenzt(EEAdr_AnrufAbbruchZeit, AnrufAbbruchZeit_Std, 3, 25);
		
	BefehlEinschalten = false;
	BefehlMark = true;
	MeldungEingeschaltet = false;
	MeldungMark = true;

	KommInit();

	TW39IO();
	
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
			TW39IO();

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
		TW39IO();

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
			if (TW39Einschalten())
				{
				LokalCodeAusgabe(TtyCodeWR);
				LokalCodeAusgabe(TtyCodeZL);
				for (uint8_t i = 0 ; i < MsgLen ; i++)
					LokalCodeAusgabe(MsgBuf[i]);
				LokalCodeAusgabe(TtyCodeWR);
				LokalCodeAusgabe(TtyCodeZL);
				TW39Ausschalten();
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


