//================================================================
// Fernschreiber-Schnittstelle "Wahlbrücke" für TxP2-System
//  Komponente einer Relaisstation von TelexPhone auf i-Telex
//	für ATmega8 auf Platine FernschrTW39
//================================================================
//		
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
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
#include "LokalAusgabe.h"

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


//! Marker im Code als Identifikation
PROGMEM const char Identifier[] = "___Wahlbruecke___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";


// Eeprom-Speicher
// ---------------

EEMEM uint8_t Platzhalter[4]; //!< Platzhalter, da Anfang des EEPROM gern von Störungen betroffen ist
EEMEM uint8_t BusEigenAdresse_EE = 89 << 1; //!< Eigene Busadresse auf dem I²C-Bus


// Variablen
// ---------

bool BefehlEinschalten; //!< Fs soll laufen
bool BefehlMark; //!< Fs Schleifenstrom soll Ein sein
bool MeldungEingeschaltet; //!< Fs läuft tatsächlich
bool MeldungMark; //!< Fs Schleifenstrom ist Ein

TMsTimer AusschaltungTimer; //!< Zählt die Millisekunden von Schleifenunterbrechung bis Ausschaltung


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
	bset_FS_AUSG(BefehlEinschalten && BefehlMark);

	if (BefehlEinschalten)
		bset_LEDBLAU(!BefehlMark);

	// Schleifenstrom auswerten: Einschaltung oder nicht
	// -------------------------------------------------
	if (!get_FS_EING())
		{ // Schleifenstrom ist aus
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
	if (!MeldungEingeschaltet)
		{
		MeldungMark = true; // Beim Senden von Space ist kein Empfang möglich --> Grundstellung
		}
	else
		{ // sinnvolle Auswertung des Schleifenstroms möglich
		MeldungMark = get_FS_EING();
		}

	if (BIT_IS_SET(Status, StatBit_AngerufenBelegt))
		bset_LEDGELB(!MeldungMark);
	else // !BIT_IS_SET(Status, StatBit_AngerufenBelegt))
		bset_LEDGRUEN(!MeldungMark);

	} // TW39IO
	
	
char BuZiMode; //!< Marker für Buchstaben-Ziffern-Umschaltung.


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
	do
		{
		BefehlEinschalten = true;
		BefehlMark = true;
		TW39IO();
		if (!MeldungEingeschaltet)
			StartTimer(&StabilTimer);
		if (TimerVal(&AbbruchTimer) > 7000)
			{
			BefehlEinschalten = false;
			BefehlMark = true;
			TW39IO();
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
	}
		

/////////////////////////////////////////////////////////////////////////////////////////7

//! Modul / Schnittstelle irreversibel stoppen.
//---------------------------------------------
//! Nur Reset befreit, ein Tastendruck löst einen Reset aus.

void FehlerStop(int Nummer /*!< Fehlercode wird mit den LED angezeigt, Rot = Bit 0 */ )
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
			else
				{ // Taste gedrückt
				if (TasteZ < 5)
					TasteZ++;
				else
					TasteWirk = true;
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

	while (true)
		{
		TW39IO();
		SeriellUmsetzung(MeldungMark, &BefehlMark);
		if (SerUmEmpfBitNr == SerUmEmpfFertig)
			{
			c = CodeZuZeichen(SerUmEmpfDaten, &BuZiMode);
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
	uint8_t code;
	
	if (c >= 'A' && c <= 'Z')
		c += 'a'-'A';

	if ((code = ZeichenZuCode(c, BuZiMode)) != 255)
		{
		LokalCodeAusgabe(code);
		}
	else if ((code = ZeichenZuCode(c, BuMode)) != 255)
		{
		LokalCodeAusgabe(TtyCodeBuUm);
		LokalCodeAusgabe(code);
		BuZiMode = BuMode;
		}
	else if ((code = ZeichenZuCode(c, ZiMode)) != 255)
		{
		LokalCodeAusgabe(TtyCodeZiUm);
		LokalCodeAusgabe(code);
		BuZiMode = ZiMode;
		}
	}
			
		
static void VerbindungSteht(bool AutoKennungAbfrage);

static void Deaktivieren(bool WegenTimeout);


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
		GeAusschalten(); // TODO wird von SeriellUndSpezial nicht quittiert!
		Deaktivieren(true);
		return;
		}

	clr_LEDROT();
	
	if (GeEinschalten() != GeEinschAnrufquitt)
		{
		GeAusschalten();
		TW39Ausschalten();
		}
	else
		VerbindungSteht(false); // Keine automatische Kennungsgeber-Abfrage
		
	}
	

/////////////////////////////////////////////////////////////

//! Wird aufgerufen, wenn bei gehender Verbindung zu lange nicht gewählt wird.
//----------------------------------------------
//! \param Abschaltimpuls soll ein Schlusszeichen an das Endgerät gesendet werden?

static void AbschaltungZuLangeWahlpause(bool Abschaltimpuls)
	{
	set_LEDROT();
	GeAusschalten();
	Aktivieren(false);
	if (Abschaltimpuls)
		TW39Ausschalten();
	while (MeldungEingeschaltet)
		TW39IO();
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
	TMsTimer BeginnTimer; 
		// filtert alle empfangenen Zeichen aus, die vor dem 'ga' empfangen werden, 
		// wie z.B. automatische datums-ausgabe der vermittlung.
	bool WahlFreigabe;
	bool EsWurdeGewaehlt;
	int Falschziffern;
	
	if (!TW39Einschalten())
		return false;

	SeriellUmsetzInit();
	
	StartTimer(&WahlendeTimer);
	StartTimer(&BeginnTimer);
	EsWurdeGewaehlt = false;
	WahlFreigabe = false;
	Falschziffern = 0;
	BuZiMode = '\0';

	while (true)
		{
		TW39IO();
		
		SeriellUmsetzung(MeldungMark, &BefehlMark);
		if (SerUmEmpfBitNr == SerUmEmpfFertig)
			{
			SerUmEmpfBitNr = SerUmEmpfWarte;
			if (WahlFreigabe)
				{
				c = CodeZuZeichen(SerUmEmpfDaten, &BuZiMode);
				if (c >= '0' && c <= '9')
					{
					GeWaehlen(c - '0');
					EsWurdeGewaehlt = true;
					}
				else if (c != 0 && c != ' ' && c != '\r' && c != '\n')
					{
					Falschziffern++;
					}
				StartTimer(&WahlendeTimer);
				} // if (WahlFreigabe)
			else 
				{ // ignorieren
				StartTimer(&BeginnTimer);
				StartTimer(&WahlendeTimer);
				}

			}

		while (Falschziffern > 0 && TimerVal(&WahlendeTimer) >= 100)
			{
			LokalZeichenAusgabe('?');
			Falschziffern--;
			}

		if (!MeldungEingeschaltet || KoAusschalten())
			{
			// TW39Ausschalten() macht die aufrufende Routine
			return false;
			}
			
		if (KoEinschalten())
			{
			return true;
			}

		if (!WahlFreigabe && TimerVal(&BeginnTimer) >= 2000) // 2 Sekunden Ruhe nach Verbindungsaufbau
			{
			LokalCodeAusgabe(TtyCodeBuUm);
			LokalCodeAusgabe(TtyCodeBuUm);
			LokalCodeAusgabe(TtyCodeBuUm);
			LokalCodeAusgabe(TtyCodeWR);
			LokalCodeAusgabe(TtyCodeZL);
			LokalZeichenAusgabe('g');
			LokalZeichenAusgabe('a');
			LokalCodeAusgabe(TtyCodeWR);
			LokalCodeAusgabe(TtyCodeZL);
			LokalCodeAusgabe(TtyCodeZiUm);
			WahlFreigabe = true;
			}
			
		if (TimerVal(&WahlendeTimer) > (EsWurdeGewaehlt ? 45000 : 15000)) // 15 / 45 Sekunden nicht gewählt
			{ // auf das Ausschalten durch die Schlusstaste warten
			AbschaltungZuLangeWahlpause(EsWurdeGewaehlt);
			return false;
			}

		} // while (true)
	}
	
  
static void KommendSperren();


/////////////////////////////////////////////////////////////

//! Wickelt ausgehende Verbdindungen vollständig ab.
//--------------------------------------------------
//! Ruft VerbindungSteht() auf. Kehrt erst nach Verbindungsabbau wieder zurück.

static void VerbindungGehend()
	{
	set_LEDGELB();

	if (BusEigenAdresse == BusAdrUngueltig)
		{
		TW39Ausschalten();
		return;
		}
	
	switch (GeEinschalten())
		{ // hier nur break benutzen, wenn Einschaltung erfolgreich
		case GeEinschFehler:
			return; 

		case GeEinschWahl:
			if (WahlMitTastatur())
				// Einschalten ist nicht erforderlich, da schon eingeschaltet ist...
				break; // ist jetzt verbunden

			GeAusschalten();
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

	VerbindungSteht(false); // keine automatische Kennungsgeber-Abfrage

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

	while (true)
		{
		TW39IO();
		TastePruefen();

		if (!MeldungEingeschaltet || KoAusschalten())
			{
			TW39Ausschalten();
			GeAusschalten();
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
		} // while true

	} // VerbindungSteht()


/////////////////////////////////////////////////////////////

//! Schaltet das Modul in einen Modus, der keine kommenden Verbindungen zulässt.
//------------------------------------------------------------------------------
//! Kann durch Wahl einer entsprechenden Ziffernfolge aufgerufen werden oder
//! durch Tastendruck an der Platine.
	
static void KommendSperren()
	{
	TMsTimer BlinkTimer;
	
	StartTimer(&BlinkTimer);
	Aktivieren(false);
	
	while (true)
		{
		TastePruefen();
		if (Tastendruck != NichtGedr)
			{
			Tastendruck = NichtGedr;
			break;
			}
			
		TW39IO();
		if (MeldungEingeschaltet)
			break;
			
		if (TimerVal(&BlinkTimer) > 1000)
			StartTimer(&BlinkTimer);
		else if (TimerVal(&BlinkTimer) > 500)
			set_LEDBLAU();
		else
			clr_LEDBLAU();
		}
		
	clr_LEDBLAU();
	Aktivieren(true);
		
	}
	
	
/////////////////////////////////////////////////////////////

//! Schaltet das Modul in einen Modus, der keine kommenden und keine gehenden 
//! Verbindungen zulässt.
//------------------------------------------------------------------------------
//! Kann nur durch Tastendruck an der Platine aktiviert werden.
	
static void Deaktivieren(bool WegenTimeout)
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

	if (!WegenTimeout)
		KommendSperren();
		
	} // Deaktivieren


/////////////////////////////////////////////////////////////

//! Das Hauptprogramm der TW39-Fernschreiber-Schnittstelle.
//---------------------------------------------------------

int main()
	{
#ifndef NOWATCHDOG
	wdt_enable(WDTO_2S);
#endif //NOWATCHDOG

	// nur für den Simulator:
	PINB = 0xFF;
	PINC = 0xFF;
	PIND = 0xFF;

	// Ports initialisieren
	init_LEDROT();
	init_LEDGELB();
	init_LEDGRUEN();
	init_LEDBLAU();
	init_FS_AUSG();
	init_FS_EING(); 
	init_TASTE();
	//init_TASTE2();

	set_LEDROT();
	

	// Timer initialisieren
	MsTimerInit();
	
	BusEigenAdresse = eeprom_read_byte(&BusEigenAdresse_EE) & 0xFE;
	BusEigenAdrMehrfach = 1;

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
	
	// Bei Tastendruck Watchdog AUS
	if (!get_TASTE())
		{ // Gedrückt = LOW	
		wdt_disable();
		while (!get_TASTE())
			; // Warten, bis Taste wieder losgelassen
		set_LEDGELB();
		StartTimer(&Timer);
		}

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

	while (TimerVal(&Timer) < 1000 + BusEigenAdresse)
		;

	BusEigenAdressePruefenUndSetzen(BusEigenAdresse);

/*/ Selbsttest

	BefehlEinschalten = false;
	MeldungEingeschaltet = false;
	BefehlMark = false;
	MeldungMark = false;

	while (1)
		{
		BefehlMark = BIT_IS_SET(TAST_IPORT, TAST_BIT);
			// Gedrückt = LOW

		TW39IO();

		if (BefehlEinschalten) 		LED_EIN(ROT); 	else LED_AUS(ROT);
		if (MeldungEingeschaltet) 	LED_EIN(GELB); 	else LED_AUS(GELB);
		if (BefehlMark) 			LED_EIN(GRUEN); else LED_AUS(GRUEN);
		if (MeldungMark)			LED_EIN(BLAU); 	else LED_AUS(BLAU);

		BefehlEinschalten = MeldungEingeschaltet;

		}

// Selbsttest Ende */

	BefehlEinschalten = false;
	BefehlMark = true;

	while (1)
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
			//Konfiguration();
			//KonfigurationEnde();
			}

		if (Tastendruck == Kurz)
			{
			Tastendruck = NichtGedr;
			Deaktivieren(false);
			}

		if (MeldungEingeschaltet)
			{
			VerbindungGehend();
			}
			
		if (KoEinschalten())
			{
			VerbindungKommend();
			}
		
		} // while (1)
	} // main()


