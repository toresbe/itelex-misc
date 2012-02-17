//================================================================
// Fernschreiber-Schnittstelle TW39 für TxP2-System
//	für ATmega8 auf Platine FernschrTW39
//================================================================
//		
//================================================================
// verwendete Pins
//================================================================
//				
//   01: C6  	Reset
//   02: D0 	bis V1.2 Ausgabe FS Daten: High = Strom ein = Mark
//				ab V1.3 Taster nach Masse
//   03: D1 	bis V1.2 Ausgabe FS Steuerung: High = Maschine ein
//				ab V1.3 LED rot: High = ein
//   04: D2  	ab V1.3 LED gelb: High = ein
//   05: D3  	ab V1.3 LED grün: High = ein
//   06: D4  	ab V1.3 LED blau: High = ein
//   07: VCC		
//   08: GND		
//   09: B6 	Quarz
//   10: B7 	Quarz
//   11: D5  	ab V1.31 Maschine 2 Eingang FS Eingang: Low = Strom ein
//   12: D6   	bis V1.2 Taster nach Masse
//				ab V1.3 Eingang FS Eingang: Low = Strom ein
//   13: D7   	bis V1.2 LED rot: High = ein
//				ab V1.3 Ausgabe FS Steuerung: High = Maschine ein
//   14: B0  	bis V1.2 LED gelb: High = ein
//				ab V1.3 Ausgabe FS Daten: High = Strom ein = Mark
//   15: B1  	bis V1.2 LED grün: High = ein
// 				ab V1.31 Maschine 2 Ausgabe FS Steuerung: High = Maschine ein
//   16: B2  	bis V1.2 LED blau: High = ein
//				ab V1.31 Maschine 2 Ausgabe FS Daten: High = Strom ein = Mark
//   17: B3 MOSI
//   18: B4 MISO
//   19: B5 SCK 
//   20: AVCC
//   21: AREF
//   22: GND
//   23: C0  					
//   24: C1  
//   25: C2  	
//   26: C3  	bis V1.2 Eingang FS Eingang: Low = Strom ein
//   27: C4 SDA	
//   28: C5 SCL	


#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/wdt.h>
#include <inttypes.h>


// Schalter für Code-Varianten
// ===========================


//#define PARALLELAUSGABE
	//!< Für Platine TW39doppel: Sendung und Empfang wird auf der zweiten 
	//!< Schnittstelle mitprotokolliert. Dann darf der Kontroller der 
	//!< zweiten Schnittstelle nicht bestückt sein.
	// 

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


#include "TwiEvents.h"
#include "Bits.h"

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

//! Marker im Code als Identifikation
const char Identifier[] PROGMEM = "___TxP2_TW39___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";


// Eeprom-Speicher
// ---------------

EEMEM uint8_t Platzhalter[4]; //!< Platzhalter, da Anfang des EEPROM gern von Störungen betroffen ist
EEMEM uint8_t BusEigenAdresse_EE = BusAdrUngueltig; //!< Eigene Busadresse auf dem I²C-Bus
EEMEM uint8_t MitWaehlscheibe_EE = 1; //!< Hat das Gerät eine Wählscheibe
EEMEM uint8_t WahlauffordImpulsLaenge_EE = 30; //!< Länge des Wahlaufforderungsimpuls in 1/100 sek
EEMEM uint8_t KommendSperreWahl_EE = 0; //!< Welche Wahlnummer sperrt den Anschluss für ankommende Rufe


// Variablen
// ---------

bool MitWaehlscheibe; //!< Gerät het eine Wählscheibe
uint8_t WahlauffordImpulsLaenge; //!< Länge des Wahlaufforderungsimpuls in 1/100 sek
uint8_t KommendSperreWahl; //!< Welche Wahlnummer sperrt den Anschluss für ankommende Rufe

bool BefehlEinschalten; //!< Fs soll laufen
bool MeldungEingeschaltet; //!< Fs läuft tatsächlich
bool BefehlMark; //!< Fs Schleifenstrom soll Ein sein
bool MeldungMark; //!< Fs Schleifenstrom ist Ein

char BuZiMode; //!< Fs ist gerade im Ziffern-Bereich

TMsTimer AusschaltungTimer; //!< Zählt die Millisekunden von Schleifenunterbrechung bis Ausschaltung
TMsTimer EntprellungTimer; //!< Zählt die Millisekunden von Pegelwechsel am Port bis tatsächlichem Pegelwechsel


///////////////////////////////////////////////////////////////////////////////

//! bedient Hardware-IO entsprechend der aktuellen Zustände.

/*!
 * setzt FS_AKTIV_PORT und FS_AUSG _PORT entsprechend BefehlEinschalten und BefehlMark,
 * setzt MeldungEingeschaltet und MeldungMark entsprechend FS_EING_IPORT,
 * steuert die Status-LEDs
 */ 

static void TW39IO()
	{
	// Pegel & Polung ausgeben
	// -----------------------
	if (BefehlEinschalten)
		{
		SET_BIT(FS_AKTIV_PORT, FS_AKTIV_BIT);
		#ifdef PARALLELAUSGABE
		SET_BIT(FS2_AKTIV_PORT, FS2_AKTIV_BIT);
		#endif //def PARALLELAUSGABE
		}
	else
		{
		CLR_BIT(FS_AKTIV_PORT, FS_AKTIV_BIT);
		#ifdef PARALLELAUSGABE
		CLR_BIT(FS2_AKTIV_PORT, FS2_AKTIV_BIT);
		#endif //def PARALLELAUSGABE
		}

	if (BefehlMark)
		{
		if (BefehlEinschalten)
			LED_AUS(BLAU);
		SET_BIT(FS_AUSG_PORT, FS_AUSG_BIT);
		#ifdef PARALLELAUSGABE
		SET_BIT(FS2_AUSG_PORT, FS2_AUSG_BIT);
		#endif //def PARALLELAUSGABE
		}
	else
		{
		if (BefehlEinschalten)
			LED_EIN(BLAU);
		CLR_BIT(FS_AUSG_PORT, FS_AUSG_BIT);
		#ifdef PARALLELAUSGABE
		CLR_BIT(FS2_AUSG_PORT, FS2_AUSG_BIT);
		#endif //def PARALLELAUSGABE
		}

#define NEU

#ifdef NEU
	// Schleifenstrom auswerten: Einschaltung oder nicht
	// -------------------------------------------------
	if (BIT_IS_SET(FS_EING_IPORT, FS_EING_BIT))
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
		if (BIT_IS_SET(FS_EING_IPORT, FS_EING_BIT))
			{ // Strom ist aus --> Space
			if (MeldungMark)
				{ // der Applikation wird noch Mark gemeldet
				if (TimerVal(&EntprellungTimer) > 3) // mindestens 3 ms konstant Space --> Space melden
					MeldungMark = false;
				}
			else // !MeldungMark
				StartTimer(&EntprellungTimer); // Regelzustand bei Space: Space wird auch gemeldet
			} // Strom ist aus
		else // !BIT_IS_SET(FS_EING_IPORT, FS_EING_BIT)
			{ // Schleifenstrom fließt
			if (!MeldungMark)
				{ // der Applikation wird noch Space gemeldet
				if (TimerVal(&EntprellungTimer) > 3) // mindestens 3 ms konstant Mark --> Mark melden
					MeldungMark = true;
				}
			else // MeldungMark
				StartTimer(&EntprellungTimer); // Regelzustand bei Mark: Mark wird auch gemeldet
			} // else !BIT_IS_SET(FS_EING_IPORT, FS_EING_BIT) == Schleifenstrom fließt
		} // else BefehlMark && MeldungEingeschaltet
	
#else
	// Schleifenstrom auswerten
	// ------------------------
	if (BefehlMark || !MeldungEingeschaltet)
		{
		if (BIT_IS_SET(FS_EING_IPORT, FS_EING_BIT))
			{ // Strom ist aus --> Space
			if (!MeldungEingeschaltet || TimerVal(&AusschaltungTimer) > 500)
				{ // mehr als 0,5 s Stromunterbrechung --> Ausschalten
				MeldungEingeschaltet = false;
				MeldungMark = true;
				StartTimer(&EntprellungTimer);
				}
			else if (MeldungMark)
				{ // der Applikation wird noch Mark gemeldet
				if (TimerVal(&EntprellungTimer) > 3)
					{ // mindestens 3 ms konstant Space --> Space melden
					MeldungMark = false;
					}
				}
			else // !MeldungMark
				{ // Regelzustand bei Space: Space wird auch gemeldet
				StartTimer(&EntprellungTimer);
				}
			} // Strom ist aus
		else // !BIT_IS_SET(FS_EING_IPORT, FS_EING_BIT)
			{ // Schleifenstrom fließt
			if (!MeldungMark)
				{ // der Applikation wird noch Space gemeldet
				if (TimerVal(&EntprellungTimer) > 3)
					{ // mindestens 3 ms konstant Mark --> Mark melden
					MeldungMark = true;
					}
				}
			else // MeldungMark
				{ // Regelzustand bei Mark: Mark wird auch gemeldet
				if (!MeldungEingeschaltet)
					{ // erst mal stabile Einschaltung abwarten...
					if (TimerVal(&EntprellungTimer) > 100)
						{
						MeldungEingeschaltet = true;
						StartTimer(&AusschaltungTimer);
						}
					else
						; // warten
					}
				else // ist schon Eingeschaltet (Meldung)
					{ 
					StartTimer(&AusschaltungTimer);
					StartTimer(&EntprellungTimer);
					}
				}
			} // else !BIT_IS_SET(FS_EING_IPORT, FS_EING_BIT) === Schleifenstrom fließt
		} // else FsSendMark --> Schleife ist Schnittstellen-Ausgabeseitig ein
	else // !BefehlMark && MeldungEingeschaltet
		{
		MeldungMark = true; // nur Simplex-Modus
		StartTimer(&EntprellungTimer);
		StartTimer(&AusschaltungTimer);
		}
#endif

	if (BIT_IS_SET(Status, StatBit_AngerufenBelegt))
		if (MeldungMark)
			LED_AUS(GELB);
		else
			LED_EIN(GELB);
	else // !BIT_IS_SET(Status, StatBit_AngerufenBelegt))
		if (MeldungMark)
			LED_AUS(GRUEN);
		else
			LED_EIN(GRUEN);
			

	} // TW39IO
	
	
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
	TWCR = (1<<TWINT) | (0<<TWEA) | (0<<TWSTA) | (1<<TWSTO) | (1<<TWEN) | (0<<TWIE);
	
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
			if (BIT_IS_SET(TAST_IPORT, TAST_BIT))
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
		    if (BIT_IS_SET(Nummer, 0)) LED_EIN(ROT);
		    if (BIT_IS_SET(Nummer, 1)) LED_EIN(GELB);
		    if (BIT_IS_SET(Nummer, 2)) LED_EIN(GRUEN);
		    if (BIT_IS_SET(Nummer, 3)) LED_EIN(BLAU);
			}
		else
			{
			LED_AUS(ROT);
			LED_AUS(GELB);
			LED_AUS(GRUEN);
			LED_AUS(BLAU);
			}
		}
	}	


/////////////////////////////////////////////////////////////

//! Liest ein Zeichen vom angeschlossenen Fs ein

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

//! Gibt ein Zeichen am angeschlossenen Fs aus

//! wandelt ASCII in Baudort und serialisiert den Code
//! \param code Zeichen im ASCII-Code

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
			
		
static void VerbindungSteht();

static void Deaktivieren(bool WegenTimeout);


static void VerbindungKommend()
	{
	TW39IO();

	LED_EIN(GRUEN);
	LED_EIN(ROT);
	
	if (!TW39Einschalten())
		{ // Timeout...
		LED_AUS(GRUEN);
		GeAusschalten(); // TODO wird von SeriellUndSpezial nicht quittiert!
		Deaktivieren(true);
		return;
		}

	LED_AUS(ROT);
	
	if (GeEinschalten() != GeEinschAnrufquitt)
		{
		GeAusschalten();
		TW39Ausschalten();
		}
	else
		VerbindungSteht();
		
	}
	

static void AbschaltungZuLangeWahlpause(bool Abschaltimpuls)
	{
	LED_EIN(ROT);
	GeAusschalten();
	Aktivieren(false);
	if (Abschaltimpuls)
		TW39Ausschalten();
	while (MeldungEingeschaltet)
		TW39IO();
	LED_AUS(ROT);
	Aktivieren(true);
	}
	
	
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

	StartTimer(&WahlendeTimer);
	EsWurdeGewaehlt = false;
	Falschziffern = 0;
	BuZiMode = '\0';

	while (true)
		{
		TW39IO();
		
		SeriellUmsetzung(MeldungMark, &BefehlMark);
		if (SerUmEmpfBitNr == SerUmEmpfFertig)
			{
			c = CodeZuZeichen(SerUmEmpfDaten, &BuZiMode);
			SerUmEmpfBitNr = SerUmEmpfWarte;
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
			GeSendeMark(true); // Erst mal einschalten...

			TMsTimer PauseTimer;
			StartTimer(&PauseTimer);
			uint8_t AnzWdh = 0;

			TW39IO();
			
			while (KoEmpfMark() && AnzWdh <= 3)
				{
				TW39IO();
				
				if (TimerVal(&PauseTimer) > 2500)
					{
					AnzWdh++;
					GeSendeCode(TtyCodeZiUm);
					GeSendeCode(TtyCodeZiUm);
					GeSendeCode(TtyCodeZiUm);
					GeSendeCode(TtyCodeZiWerDa);
					StartTimer(&PauseTimer);
					}
					
				if (SerUmSendBitNr != SerUmSendWarte)
					StartTimer(&PauseTimer); // nur bei nicht laufender Sendung warten...
				}
			
			return true;
			}

		if (TimerVal(&WahlendeTimer) > (EsWurdeGewaehlt ? 45000 : 15000)) // 15 / 45 Sekunden nicht gewählt
			{ // auf das Ausschalten durch die Schlusstaste warten
			AbschaltungZuLangeWahlpause(EsWurdeGewaehlt);
			return false;
			}

		} // while (true)
	}
	
  
static void KommendSperren();

static void VerbindungSteht();


static void VerbindungGehend()
	{
	LED_EIN(GELB);

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
			GeAusschalten();
			TW39Ausschalten();
			
			if (KommendSperreWahl != 0 && LetzteInterneWahl() == KommendSperreWahl)
				{
				LED_AUS(GELB);
				KommendSperren();
				}
				
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

	VerbindungSteht();

	}


static void VerbindungSteht()
	{
	while (true)
		{
		TW39IO();

		if (!MeldungEingeschaltet || KoAusschalten())
			{
			TW39Ausschalten();
			GeAusschalten();
			return;
			}

		BefehlMark = KoEmpfMark();
	
		if (SerUmSendBitNr <= SerUmSendStart) // Start oder Warten...
			GeSendeMark(MeldungMark); // Nur Fs-Pegel direkt auf Bus, wenn nicht seriell gesendet wird...
		}

	}


static void Konfiguration()
	{
	SeriellUmsetzInit();
	BuZiMode = '\0';
	Aktivieren(false);
	LED_EIN(ROT);
	
	if (!TW39Einschalten())
		return;
	
	LokalTextAusgabeP(PSTR("\r\n konfiguration tw39 version " SVNVERSION " datum " __DATE__));

	// Durchwahl...
	if (!KonfigurationAllgemein())
		return;

	// Wählscheibe vorhanden?
	LokalTextAusgabeP(PSTR("\r\n waehlscheibe vorhanden?      "));

	if (LokalBoolEingabe(&MitWaehlscheibe) == 0)
		return;

	if (MitWaehlscheibe != (eeprom_read_byte(&MitWaehlscheibe_EE) != 0))
		eeprom_write_byte(&MitWaehlscheibe_EE, MitWaehlscheibe ? 1 : 0);

	LokalTextAusgabeP(OkStrP);

	if (MitWaehlscheibe)
		{
		// Länge Wahlaufforderungsimpuls?
		LokalTextAusgabeP(PSTR("\r\n laenge wahlauff-imp. (akt. "));
		LokalZahlAusgabe(WahlauffordImpulsLaenge, 0);
		LokalTextAusgabeP(PSTR("/100 sek)?      "));

		if (LokalZahlEingabe(&WahlauffordImpulsLaenge, 0) == 0)
			return;

		if (WahlauffordImpulsLaenge < 1)
			WahlauffordImpulsLaenge = 1;

		if (WahlauffordImpulsLaenge != eeprom_read_byte(&WahlauffordImpulsLaenge_EE))
			eeprom_write_byte(&WahlauffordImpulsLaenge_EE, WahlauffordImpulsLaenge);

		LokalTextAusgabeP(OkStrP);
		}

	LokalTextAusgabeP(PSTR("\r\n kommend-sperre mit wahl: (akt. "));
	if (KommendSperreWahl != 0)
		LokalZahlAusgabe(KommendSperreWahl, 2);
	else
		LokalTextAusgabeP(PSTR("nein"));
	LokalTextAusgabeP(PSTR(") neu (0 = nein):     "));

	if (LokalZahlEingabe(&KommendSperreWahl, 0) == 0)
		return;

	if (KommendSperreWahl != eeprom_read_byte(&KommendSperreWahl_EE))
		eeprom_write_byte(&KommendSperreWahl_EE, KommendSperreWahl);

	LokalTextAusgabeP(OkStrP);
	
	// weitere Eingaben

	LokalTextAusgabeP(PSTR("\r\n +++ \r\n"));
	}


static void KonfigurationEnde()
	{
	TW39Ausschalten();
	Aktivieren(true);
	LED_AUS(ROT);
	}
	
	
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
			LED_EIN(BLAU);
		else
			LED_AUS(BLAU);
		}
		
	LED_AUS(BLAU);
	Aktivieren(true);
		
	}
	
	
static void Deaktivieren(bool WegenTimeout)
// wird nach kurzem Tastendruck aufgerufen
	{
	LED_EIN(BLAU);
	Aktivieren(false);

	while (Tastendruck == NichtGedr)
		TastePruefen();
	Tastendruck = NichtGedr;

	Aktivieren(true);
	LED_AUS(BLAU);
	LED_AUS(ROT);

	if (!WegenTimeout)
		KommendSperren();
		
	} // Deaktivieren


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
	PORTB = 0;
	PORTC = 0;
	PORTD = 0;
	DDRB = 0;
	DDRC = 0;
	DDRD = 0;

	SET_BIT(TAST_PORT, TAST_BIT); // Pull-Up

	LED_EIN(ROT);
	SET_BIT(LED_ROT_DDR, LED_ROT_BIT);

	LED_AUS(GELB);
	SET_BIT(LED_GELB_DDR, LED_GELB_BIT);

	LED_AUS(GRUEN);
	SET_BIT(LED_GRUEN_DDR, LED_GRUEN_BIT);

	LED_AUS(BLAU);
	SET_BIT(LED_BLAU_DDR, LED_BLAU_BIT);

	// PORTS initialisieren (Ausgabepins)
	SET_BIT(FS_AUSG_PORT, FS_AUSG_BIT);
	SET_BIT(FS_AUSG_DDR, FS_AUSG_BIT);

	CLR_BIT(FS_AKTIV_PORT, FS_AKTIV_BIT);
	SET_BIT(FS_AKTIV_DDR, FS_AKTIV_BIT);

	SET_BIT(FS_EING_PORT, FS_EING_BIT); // Pull-Up

#ifdef PARALLELAUSGABE
	SET_BIT(FS2_AUSG_PORT, FS2_AUSG_BIT);
	SET_BIT(FS2_AUSG_DDR, FS2_AUSG_BIT);

	CLR_BIT(FS2_AKTIV_PORT, FS2_AKTIV_BIT);
	SET_BIT(FS2_AKTIV_DDR, FS2_AKTIV_BIT);

	SET_BIT(FS2_EING_PORT, FS2_EING_BIT); // Pull-Up
#endif //def PARALLELAUSGABE

	// Timer initialisieren
	MsTimerInit();
	
	BusEigenAdresse = eeprom_read_byte(&BusEigenAdresse_EE) & 0xFE;
	BusEigenAdrMehrfach = 1;
	MitWaehlscheibe = eeprom_read_byte(&MitWaehlscheibe_EE) != 0;
	WahlauffordImpulsLaenge = eeprom_read_byte(&WahlauffordImpulsLaenge_EE);
	KommendSperreWahl = eeprom_read_byte(&KommendSperreWahl_EE);

	BefehlEinschalten = false;
	BefehlMark = true;
	MeldungEingeschaltet = false;

	KommInit();

	TW39IO();
	
	TMsTimer Timer;
	StartTimer(&Timer);

	TwiInit();
	
	sei();
	
	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 250)
		;
	
	// Bei Tastendruck Watchdog AUS
	if (!BIT_IS_SET(TAST_IPORT, TAST_BIT))
		{ // Gedrückt = LOW	
		wdt_disable();
		while (!BIT_IS_SET(TAST_IPORT, TAST_BIT))
			; // Warten, bis Taste wieder losgelassen
		LED_EIN(GELB);
		StartTimer(&Timer);
		}

	LED_AUS(ROT);
	LED_EIN(GELB);

	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 500)
		;

	LED_AUS(GELB);
	LED_EIN(GRUEN);

	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 750)
		;

	LED_AUS(GRUEN);
	LED_EIN(BLAU);

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

	while (1)
		{
		// aktueller Zustand: Ausgeschaltet

		if (TimerVal(&Timer) >= 1400)
			{
			LED_AUS(ROT);
			LED_AUS(GELB);
			LED_AUS(GRUEN);
			LED_AUS(BLAU);
			StartTimer(&Timer);
			}
		else if (TimerVal(&Timer) >= 1200)
			{
			if (BusEigenAdresse == BusAdrUngueltig)	
				LED_EIN(ROT);
			if (!BIT_IS_SET(Status, StatBit_Frei) || BIT_IS_SET(Status, StatBit_BusKdoEmpfangen))
				LED_EIN(GELB);
			if (!BetriebsartAusgeschaltet())
				LED_EIN(GRUEN);
			}

		TastePruefen();
		TW39IO();

		if (Tastendruck == Lang)
			{
			Tastendruck = NichtGedr;
			Konfiguration();
			KonfigurationEnde();
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


