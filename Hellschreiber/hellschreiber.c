//================================================================
// Fernschreiber-Schnittstelle für TxP2-System
//	für ATmega168 auf Platine TODO
//================================================================
//				
//  
//================================================================
// verwendete Pins siehe Ports.h
//================================================================

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/wdt.h>
#include <inttypes.h>

#include "bits.h"
#include "EepromTools.h"

#include "TwiEvents.h"

#include "TxP2-Defs.h"
#include "Taste.h"
#include "TxP2-Endgeraet.h"
#include "MsTimer.h"
#include "BaudotCode.h"
#include "Ports.h"
//#include "LokalAusgabe.h"
#include "BusKomm.h"
//#include "FifoPuffer.h"

#include "../SvnVersion.h"


// Schalter für Code-Varianten
// ===========================


//#define TWI_DEBUG
	//!< TWI-Ereignisse werden protokolliert

//#define BUSFEHLER_ABBRUCH
	//!< bei Bus-Fehlern Abbruch der Verbindung

#define FALSCHKDO_FEHLERSTOP
	//!< Unpassende Kommandos auf dem I²C-Bus werden mit Fehlerstop quittiert

#define WIEDERHOLUNGSSENDUNGEN
	//!< Status Mark / Space regelmäßig senden 

//#define LEDROT_BEI_UNERWARTETWDH
	//!< LED rot wird eingeschaltet, wenn BusKdoSpaceWdh oder BusKdoMarkWdh empfangen wird, ohne
	//!< das entsprechendes "Haupt-Kommando" empfangen wurde

//#define NOWATCHDOG
	//!< Watchdog abgeschaltet


//! Identifikation im Programmspeicher
const PROGMEM char Identifier[] = "___itlx_Hellschreiber___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";


#include "timercs.h"


// sonstige Konstanten
// -------------------

#define KENNUNG_MAXLEN 20 //!< maximale Länge der Kennungsgeber-Texte.

typedef char TKennung[KENNUNG_MAXLEN];


#define BUS_MEHRFACH_ADR 1
	// muss Zehnerpozenz von 2 sein

#if ((BUS_MEHRFACH_ADR - 1) & BUS_MEHRFACH_ADR) != 0
#error BUS_MEHRFACH_ADR muss 1, 2, 4, 8 ... sein
#endif


// interner Eeprom-Speicher
// ------------------------

uint8_t BusEigenAdresse_EE EEMEM = 77 * 2; 
	//!< Eigene TWI-Adresse, nur Basisteil! (4 Sub-Adressen)

EEMEM TKennung Kennung_EE = "\r\ntxp2-hell";

// Kennung und Kennwort

TKennung Kennung; //!< Eigene Kennungen, da kein echter Fernschreiber angeschlossen.



// Einstell-Modus
bool WarteKonfig;


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
			if (!get_TASTE())
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


//! Schaltet LED entspechend der Status-Bits an.
static void LEDAktualisieren()
	{
	if (BIT_IS_SET(Status, StatBit_AngerufenBelegt))
		if (BIT_IS_SET(Status, StatBit_FsMeldEin))
			LED_AUS(GELB);
		else
			LED_EIN(GELB);
	else // !BIT_IS_SET(Status, StatBit_AngerufenBelegt))
		if (BIT_IS_SET(Status, StatBit_FsMeldEin))
			LED_AUS(GRUEN);
		else
			LED_EIN(GRUEN);
			
	if (BIT_IS_SET(Status, StatBit_FsBefEin)) // komme ich anders nicht dran...
		LED_AUS(BLAU);
	else
		LED_EIN(BLAU);
	}
	
	
//! Wartet einen kurzen moment.	
void KurzePause() // = 500 ms
	{
	TMsTimer MessTimer;
	
	StartTimer(&MessTimer);
	while (TimerVal(&MessTimer) < 500)
		;
	}
		

//! Wartet einen langen Moment.		
void LangePause() // = 2 sek
	{
	TMsTimer MessTimer;
	
	StartTimer(&MessTimer);
	while (TimerVal(&MessTimer) < 500)
		;
	}
		

#define MAXPUFFER 500
	//!< Größe des Universal-Zwischenspeichers.

char Puffer[MAXPUFFER+1];

	
static void GeSendeText(char* s)
	{
	while (!GeSendePufferLeer())
		;
	GeSendeCode(TtyCodeBuUm); // für definierte Verhältnisse...
	while (*s != '\0')
		{
		GeSendeZeichen(*s);
		s++;
		}
	}
	

//! Behandelt die Funktion des Moduls als Lochstreifen-Bildlocher.
static void VerbindungHellschreiber()
	{
	TMsTimer WarteTimer;
	StartTimer(&WarteTimer);

	uint16_t PufferPosEin, PufferPosAus;
	bool EmpfZifferMode = false;

	PufferPosEin = 0;
	PufferPosAus = 0;

	while (true)
		{
		uint8_t c;

		if (KoEmpfCode(&c))
			{
			if (c == TtyCodeZiUm)
				EmpfZifferMode = true;
			else if (c == TtyCodeBuUm)
				EmpfZifferMode = false;

			if (c == TtyCodeZiWerDa && EmpfZifferMode)
				GeSendeText(Kennung);
			else
				{ // im Puffer ablegen
				if (PufferPosEin < MAXPUFFER)
					Puffer[PufferPosEin++] = c;
				// StartTimer(&WarteTimer);
				}
			}

		if (KoAusschalten())
			{
			GeAusschalten(false); // da braucht auf nichts mehr gewartet zu werden
			break;
			}

		if (PufferPosEin > 0)
			{
			if (PufferPosAus >= PufferPosEin)
				PufferPosEin = PufferPosAus = 0;
			else if (TimerVal(&WarteTimer) > 3000 && GeSendePufferLeer())
				{
				// TODO Puffer auf Hellschreiber ausgeben.
				} // Sendepuffer leer und Wartezeit für Echo abgelaufen
			} // if PufferPosEin > 0

		LEDAktualisieren();
		}

	}


//! Bearbeitet alle kommenden Verbindungen.
static void VerbindungKommend()
	{
	LED_EIN(GRUEN);
	
	if (!GeEinschaltQuittung())
		{
		GeAusschalten(true);
		}
	else
		{
		VerbindungHellschreiber();
		}

	}
	

//! wird nach kurzem Tastendruck aufgerufen
static void Deaktivieren()
	{
	LED_EIN(BLAU);
	Aktivieren(false);

	while (Tastendruck == NichtGedr)
		TastePruefen();
	Tastendruck = NichtGedr;

	Aktivieren(true);
	
	LED_AUS(BLAU);

	} // Deaktivieren


//! wird nach langem Tastendruck aufgerufen
static void Konfiguration()
	{
	if (!WarteKonfig)
		{
		WarteKonfig = true; //! \todo dies hat noch gar keine Auswirkung...
		LED_EIN(ROT);
		}
	else
		{
		WarteKonfig = false;
		LED_AUS(ROT);
		}
	}


//! Das Hauptprogramm des Messgerät-Moduls.
//---------------------------------------------------------
int main()
	{
	// WD ein
#ifndef NOWATCHDOG
	wdt_enable(WDTO_2S);
#endif //NOWATCHDOG

	// nur für den Simulator:
	PINB = 0xFF;
	PINC = 0xFF;
	PIND = 0xFF;

	// PORTS initialisieren (Ausgabepins)
	PORTB = 0;
	PORTC = 0;
	PORTD = 0;
	DDRB = 0;
	DDRC = 0;
	DDRD = 0;

	init_TASTE();

	LED_EIN(ROT);
	SET_BIT(LED_ROT_DDR, LED_ROT_BIT);

	LED_AUS(GELB);
	SET_BIT(LED_GELB_DDR, LED_GELB_BIT);

	LED_AUS(GRUEN);
	SET_BIT(LED_GRUEN_DDR, LED_GRUEN_BIT);

	LED_AUS(BLAU);
	SET_BIT(LED_BLAU_DDR, LED_BLAU_BIT);

	// Timer initialisieren
	MsTimerInit();

	TMsTimer Timer;
	StartTimer(&Timer);

	BusEigenAdresse = eeprom_read_byte(&BusEigenAdresse_EE) & 0xFE;
	BusEigenAdrMehrfach = 1;

	eeprom_read_string(Kennung, Kennung_EE);

	UmleitungAbweisen = true;
	
	KommInit();

	HellInit();

	sei();

	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 250)
		;
		
	LED_AUS(ROT);
	LED_EIN(GELB);

	TwiInit();

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

	WarteKonfig = false;
	
	while (true)
		{
		// aktueller Zustand: Ausgeschaltet
		LED_AUS(GELB);
		LED_AUS(GRUEN);
		LED_AUS(BLAU);

		TastePruefen();
		
		if (KoEinschalten())
			{
			VerbindungKommend();
			}
		
		if (Tastendruck == Lang)
			{
			Tastendruck = NichtGedr;
			Konfiguration();
			}

		else if (Tastendruck == Kurz)
			{
			Tastendruck = NichtGedr;
			Deaktivieren();
			}
		
		eeprom_write_string_noblock(Kennung_EE, Kennung);

		} // while (1)
	} // main()


