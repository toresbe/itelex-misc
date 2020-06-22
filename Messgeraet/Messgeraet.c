//================================================================
// Fernschreiber-Schnittstelle für TxP2-System
//	für ATmega168 auf Platine Spezial (unterer Teil, eigentlich TW39)
//================================================================
//				
//  
//================================================================
// verwendete Pins
//================================================================
//				
//   01: C6  	Reset
//   02: D0 	Taster nach Masse
//   03: D1 	LED rot: High = ein
//   04: D2  	LED gelb: High = ein
//   05: D3  	LED grün: High = ein
//   06: D4  	LED blau: High = ein
//   07: VCC		
//   08: GND		
//   09: B6 	Quarz
//   10: B7 	Quarz
//   11: D5  	
//   12: D6   	Eingang FS Eingang: Low = Strom ein
//   13: D7   	Ausgabe FS Steuerung: High = Maschine ein
//   14: B0  	Ausgabe FS Daten: High = Strom ein = Mark
//   15: B1  	
//   16: B2  	
//   17: B3 MOSI
//   18: B4 MISO
//   19: B5 SCK 
//   20: AVCC
//   21: AREF
//   22: GND
//   23: C0  					
//   24: C1  
//   25: C2  	
//   26: C3  	
//   27: C4 SDA	
//   28: C5 SCL	


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


#ifdef PROGIDZUSATZ
//! Identifikation im Programmspeicher
const PROGMEM char Identifier[] = "___itlx_Messgeraet-" PROGIDZUSATZ "___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#else
//! Identifikation im Programmspeicher
const PROGMEM char Identifier[] = "___itlx_Messgeraet___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#endif


#include "timercs.h"

// Timer 1: frei
// ------------
/*
#define TIMER1_OCFREQ 10 // pro Minute!!

#define TIMER1_CS TCCR_DIV(1, 1024)
#define TIMER1_PRESCALER 1024
#define TIMER1_FREQ (F_CPU / TIMER1_PRESCALER)
#define TIMER1_OCRA (TIMER1_FREQ * 60 / TIMER1_OCFREQ)
	// das ist gleichzeitig der MAX-Wert
#define TIMER1_OCRB_FREQ 5000
#define TIMER1_OCRB_INC (TIMER1_FREQ / TIMER1_OCRB_FREQ + 1)
	// bestimmt die Aufruf-Frequenz von OCR1B
*/


// sonstige Konstanten
// -------------------

#define KENNUNG_MAXLEN 20 //!< maximale Länge der Kennungsgeber-Texte.
#define KENNWORT_MAXLEN 20 //!< maximale Länge des Kennwortes für die Fernabfrage.

#define STANDARD_NUMMER 80 //!< muss Vielfaches von 4 sein

typedef char TKennung[KENNUNG_MAXLEN];


#define BUS_MEHRFACH_ADR 4
	// muss Zehnerpozenz von 2 sein

#if ((BUS_MEHRFACH_ADR - 1) & BUS_MEHRFACH_ADR) != 0
#error BUS_MEHRFACH_ADR muss 1, 2, 4, 8 ... sein
#endif


#define SUBADDR_MESSUNG 0
#define SUBADDR_PRUEFSEND 1
#define SUBADDR_BILDLOCH 2
#define SUBADDR_RUECKRUF 3


// interner Eeprom-Speicher
// ------------------------

uint8_t BusEigenAdresse_EE EEMEM = STANDARD_NUMMER << 1; 
	//!< Eigene TWI-Adresse, nur Basisteil! (4 Sub-Adressen)

TKennung Kennung_EE[BUS_MEHRFACH_ADR] EEMEM = 
    { "\r\ntxp2-mess", "\r\ntxp2-pruefsend", "\r\ntxp2-bildloch", "\r\ntxp2-rueckruf"} ; //!< Kopie von #Kennung im EEPROM

char Kennwort_EE[KENNWORT_MAXLEN] EEMEM = "kennwort"; //!< Kennwort für Spezialfunktionen.

uint8_t BusEigenAdressePruef_EE EEMEM = ~STANDARD_NUMMER; 
	//!< Prüfwert Eigene TWI-Adresse: Muss gleich Komplement von (BusEigenAdresse_EE >> 1) sein


// normales RAM
// ------------
// Kennung und Kennwort

TKennung Kennung[BUS_MEHRFACH_ADR]; //!< Eigene Kennungen, da kein echter Fernschreiber angeschlossen.

char Kennwort[KENNWORT_MAXLEN]; //!< Kennwort für Fernabfrage des Anrufspeichers.



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
		    if (BIT_IS_SET(Nummer, 0)) set_LED_ROT();
		    if (BIT_IS_SET(Nummer, 1)) set_LED_GELB();
		    if (BIT_IS_SET(Nummer, 2)) set_LED_GRUEN();
		    if (BIT_IS_SET(Nummer, 3)) set_LED_BLAU();
			}
		else
			{
			clr_LED_ROT();
			clr_LED_GELB();
			clr_LED_GRUEN();
			clr_LED_BLAU();
			}
		}
	}	


//! Schaltet LED entspechend der Status-Bits an.
static void LEDAktualisieren()
	{
	if (BIT_IS_SET(Status, StatBit_AngerufenBelegt))
		if (BIT_IS_SET(Status, StatBit_FsMeldEin))
			clr_LED_GELB();
		else
			set_LED_GELB();
	else // !BIT_IS_SET(Status, StatBit_AngerufenBelegt))
		if (BIT_IS_SET(Status, StatBit_FsMeldEin))
			clr_LED_GRUEN();
		else
			set_LED_GRUEN();
			
	if (BIT_IS_SET(Status, StatBit_FsBefEin)) // komme ich anders nicht dran...
		clr_LED_BLAU();
	else
		set_LED_BLAU();
	}
	
	
//! Schaltet LED entspechend der Status-Bits an.
static void GeSendeText(char* s)
	{
	while (!GeSendePufferLeer() && !KoAusschalten())
		;
	GeSendeCode(TtyCodeBuUm); // für definierte Verhältnisse...
	while (*s != '\0')
		{
		GeSendeZeichen(*s);
		s++;
		}
	}
	

//! Sendet einen Text aus dem Programmspeicher an den Verbindungspartner.
static void GeSendeTextP(PGM_P s)
	{
	while (!GeSendePufferLeer() && !KoAusschalten())
		;
	GeSendeCode(TtyCodeBuUm); // für definierte Verhältnisse...
	while (pgm_read_byte(s) != '\0')
		{
		GeSendeZeichen(pgm_read_byte(s));
		s++;
		}
	}


//! Sendet eine Zahl (dezimal) an den Verbindungspartner.	
static void GeSendeZahl(uint8_t x)
	{
	uint8_t i;
	bool Hunderter = false;

	for (i = 0 ; x >= 100 ; x -= 100)
		i++;
	if (i > 0)
		{
		GeSendeZeichen('0' + i);
		Hunderter = true;
		}
	for (i = 0 ; x >= 10 ; x -= 10)
		i++;
	if (i > 0 || Hunderter)
		GeSendeZeichen('0' + i);
	GeSendeZeichen('0' + x);
	}


//! Sendet eine Zahl (dezimal) ggf. mit Vorzeichen an den Verbindungspartner.	
static void GeSendeVorzeichenZahl(int8_t x)
	{
	if (x < 0)
		{
		GeSendeZeichen('-');
		GeSendeZahl(-x);
		}
	else
		GeSendeZahl(x);
	}


//! Gibt die eigene Kennung beim Verbindungspartner aus und wertet eingegegeben Text
//! auf Übereinstimmung mit dem gespeicherten Kennwort aus.
static bool KennungsausgabeUndKennwortAbfrage(uint8_t Nr)
	{
	char *p;
	char c;
	
	GeSendeText(Kennung[Nr]);
	GeSendeCode(TtyCodeBuUm);

	p = Kennwort;
	while (true)
		{

		if (KoEmpfZeichen(&c))
			{
			if ((c == '\r' || c == '\n') && *p == '\0')
				{
				// HACK TEST: set_LED_ROT();
				return true;
				}
			else if (c == *p)
				p++; // erledigt gleichzeitig eine falsche Wortlänge
			else
				return false;
			}
				
		if (KoAusschalten())
			return false;
		}
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

//! Zwischenspeicher für viele Funktionen:
//! - bei Messgerät:
//!   - speichert Pegelwechsel bezogen auf erste Flanke des Start-Bits
//!   - Puffer[0] speichert Wechsel zum ersten Mark-Bit
//!   - Puffer[1] speichert Wechsel zum nächsten Space-Bit
//! - bei Rückruf:
//!   - bis zum ersten 255 die Rufnummer
//!   - bis zum zweiten 255 die Baudot-Codes
//! - bei Bilderlochen:
//!   - bis zum gespeicherten Ende die Baudot-Codes
char Puffer[MAXPUFFER+1];

	
//! Behandelt das Ignorieren von ankommenden Daten in den ersten zwei Sekunden.
//--------------------------
//! \retval true, wenn nicht abgebrochen.
static bool VerbindungIgnoriereErsteZweiSekunden(uint8_t SubAddr)
	{
	TMsTimer PauseTimer;
	
	StartTimer(&PauseTimer);
	
	while (TimerVal(&PauseTimer) < 2000)
		{
		char c;
		if (!KoEmpfMark() || !GeSendePufferLeer()) 
			StartTimer(&PauseTimer);
			
		if (KoEmpfZeichen(&c) && c == CodeChrWerDa)
			GeSendeText(Kennung[SubAddr]);

		if (KoAusschalten())
			{
			GeAusschalten(false);
			return false;
			}
		LEDAktualisieren();
		}		

	return true;
	} // VerbindungIgnoriereErsteZweiSekunden()

	
//! Behandelt die Funktion des Moduls als Messgerät für die Baudot-Zeichen.	
static void VerbindungMessgeraet()
	{
	enum { StartSperre, WarteStart, Laeuft, Ausgabe } MessPhase = StartSperre;
	bool MessungAktMark = true;
	enum { MessNeustartGrenze = 6 * 20 + 30 / 2 } ;
	TMsTimer MessTimer;
	uint16_t PufferPos = 0;
	
	KurzePause();

	GeSendeText(Kennung[SUBADDR_MESSUNG]);
	GeSendeCode(TtyCodeBuUm);
	StartTimer(&MessTimer);

	while (true)
		{
		switch (MessPhase)
			{
			case StartSperre:
				if (!KoEmpfMark() || !GeSendePufferLeer()) 
					StartTimer(&MessTimer);
				else if (TimerVal(&MessTimer) >= 1000)
					MessPhase = WarteStart;
				break;
			
			case WarteStart:
				if (!KoEmpfMark())
					{
					StartTimer(&MessTimer);
					PufferPos = 0;
					MessungAktMark = false;
					MessPhase = Laeuft;
					}
				break;

			case Laeuft:
				if (KoEmpfMark() != MessungAktMark)
					{
					uint16_t t = TimerVal(&MessTimer);
					if (t >= MessNeustartGrenze) // bin im Stoppbit...
						StartTimer(&MessTimer);
					if (PufferPos < MAXPUFFER)
						{
						if (t > 250)
							Puffer[PufferPos++] = 250;
						else
							Puffer[PufferPos++] = t;
						}
					MessungAktMark = !MessungAktMark;
					}
				else if (KoEmpfMark() && TimerVal(&MessTimer) > 5000)
					{
					Puffer[PufferPos] = 255; // Ende-Marke
					PufferPos = 0;
					MessPhase = Ausgabe;
					GeSendeCode(TtyCodeBuUm); // für definierte Verhältnisse...
					GeSendeTextP(PSTR("\r\nMessung:\r\n"));
					}
				break;

			case Ausgabe:
				if (GeSendePufferLeer())
					{
					GeSendeZeichen((PufferPos & 1) ? 'M' : 'S');
					GeSendeZeichen(' ');
					uint8_t x = Puffer[PufferPos];
					GeSendeZahl(x);
					if (x == 255)
						{
						GeSendeTextP(PSTR("\r\n Ende \r\n"));
						MessPhase = StartSperre;
						}
					else 
						{
						if (x >= MessNeustartGrenze)
							GeSendeTextP(PSTR("\r\n"));
						else
							GeSendeZeichen(' ');
						PufferPos++;
						}
					} // if GeSendePufferLeer 
				break; // case Ausgabe
			} // switch MessPhase
				
		if (KoAusschalten())
			{
			GeAusschalten(false);
			break;
			}

		LEDAktualisieren();
		} // while true
	}


//! Behandelt die Funktion des Moduls als Rückruf-Automat.
static void VerbindungRueckruf()
	{
	uint16_t PufferPos;
	TMsTimer WarteTimer;
	
	// erst mal Normal, bis das richtige Kennwort eingegegen ist...
	while (true)
		{
		char c;
		if (KoEmpfZeichen(&c)
			&& c == CodeChrWerDa
			&& KennungsausgabeUndKennwortAbfrage(SUBADDR_RUECKRUF))
			break; // schleife Beenden und Hauptteil anfangen

		if (KoAusschalten())
			{
			GeAusschalten(false);
			return;
			}
		LEDAktualisieren();
		}		

	// Nummer abfragen:
	KurzePause();
	GeSendeTextP(PSTR("\r\n Nummer:     "));
	GeSendeCode(TtyCodeZiUm);
	PufferPos = 0;
	while (true)
		{
		char c;
		if (KoEmpfZeichen(&c))
			{
			if (c >= '0' && c <= '9')
				{
				if (PufferPos < MAXPUFFER)
					{
					Puffer[PufferPos++] = c - '0';
					}
				}
			else if (c == '\r' || c == '\n')
				{
				if (PufferPos > 0)
					{
					Puffer[PufferPos++] = 255;
					break; // Nummern-Eingabe beendet
					}
				else
					; // ignorieren
				} // WR oder ZL
			} // if KoEmpfZeichen

		if (KoAusschalten())
			{
			GeAusschalten(false);
			return;
			}
		LEDAktualisieren();
		} // while true
		
	// Text abfragen:
	KurzePause();
	GeSendeTextP(PSTR("\r\nText:\r\n"));
	
	while (true)
		{
		uint8_t c;
		if (KoEmpfCode(&c))
			{
			if (PufferPos < MAXPUFFER)
				{
				Puffer[PufferPos++] = c;
				}
			} // if KoEmpfCode

		if (KoAusschalten())
			{
			Puffer[PufferPos] = 255;
			GeAusschalten(false);
			break;
			}
		LEDAktualisieren();
		} // while true
		
	Aktivieren(false);
	clr_LED_ROT();
	set_LED_BLAU();
	clr_LED_GRUEN();
	set_LED_GELB();

	StartTimer(&WarteTimer);
	while (TimerVal(&WarteTimer) < 10000)
		;
	
	Aktivieren(true);
	PufferPos = 0;
	
	switch (GeEinschalten())
		{ // hier nur break benutzen, wenn Einschaltung erfolgreich
		case GeEinschFehler:
			return; 

		case GeEinschWahl:
			while (!KoEinschalten())
				{
				if (GeSendePufferLeer() && Puffer[PufferPos] < 10)
					GeWaehlen(Puffer[PufferPos++]);
				if (KoAusschalten())
					{ // Abbruch ???
					return;
					}
				} // while !KoEinschalten()
			break; // ist jetzt Verbunden

		default:
			FehlerStop(15); // TODO
			return;
		}

	while (Puffer[PufferPos] != 255)
		PufferPos++;
	PufferPos++; // steht jetz auf Anfang des Textes...
	
	LangePause();
	StartTimer(&WarteTimer);
		
	while (true)
		{
		if (Puffer[PufferPos] == 255)
			{
			LangePause();
			break;
			}

		if (!KoEmpfMark())
			StartTimer(&WarteTimer);

		if (GeSendePufferLeer() && TimerVal(&WarteTimer) > 1500)
			GeSendeCode(Puffer[PufferPos++]);
		
		if (KoAusschalten())
			break;

		LEDAktualisieren();
		} // while true
		
	GeAusschalten(true);
	
	} // VerbindungRueckruf


////////////////////////////////////////////////////////////////////////////////

//! Für Lochstreifen-Bildlocher: Loch-Spalten je Buchstabe
enum { BildGroesse = 6 } ; // 6 Lochreihen (maximal) für jedes Zeichen

//! Für Lochstreifen-Bildlocher: Bitmuster für alle Buchstaben. Sortiert nach Baudot-Codes.
const PROGMEM uint8_t BildTab[2][32*BildGroesse] = {
/*Buchst:*/ {	0, 		255, 	255, 	255, 	255, 	255, 	
				16, 	16, 	31, 	16, 	16, 	0, 	
				0, 		0, 		0, 		255, 	255, 	255, 	
				14, 	17, 	17, 	17, 	14, 	0, 	
				0, 		0, 		0, 		255, 	255, 	255, 	
				31, 	4, 		4, 		31, 	0, 		255, 	
				31, 	8, 		4, 		31, 	0, 		255, 	
				31, 	8, 		4, 		8, 		31, 	0, 	
				0, 		0, 		0, 		255, 	255, 	255, 	
				31, 	1, 		1, 		1, 		0, 		255, 	
				31, 	20, 	22, 	9, 		0, 		255, 	
				14, 	17, 	21, 	22, 	0, 		255, 	
				17, 	31, 	17, 	0, 		255, 	255, 	
				31, 	20, 	20, 	8, 		0, 		255, 	
				14, 	17, 	17, 	10, 	0, 		255, 	
				28, 	2, 		1, 		2, 		28, 	0, 	
				31, 	21, 	21, 	21, 	0, 		255, 	
				19, 	21, 	17, 	25, 	0, 		255, 	
				31, 	17, 	17, 	14, 	0, 		255, 	
				31, 	21, 	21, 	10, 	0, 		255,
				9, 		21, 	21, 	18, 	0, 		255, 	
				16, 	8, 		7, 		8, 		16, 	0, 	
				31, 	20, 	20, 	16, 	0, 		255, 	
				17, 	10, 	4,	 	10, 	17, 	0, 	
				15, 	20, 	20, 	15, 	0,	 	255, 	
				30, 	1, 		6, 		1, 		30, 	0, 	
				2, 		1, 		1, 		30, 	0,	 	255, 	
				255, 	255, 	255, 	255, 	255, 	255,
				30, 	1, 		1, 		30, 	0, 		255, 
				14, 	17, 	21, 	18, 	13, 	0, 	
				31, 	4, 		10, 	17, 	0, 		255, 	
				255, 	255, 	255, 	255, 	255, 	255}, 
/*Ziffern:*/ {	0, 		255, 	255, 	255, 	255, 	255, 	
				29, 	21, 	21, 	18, 	0, 		255, 	
				0, 		0, 		0, 		255, 	255, 	255, 	
				9, 		21, 	21, 	14, 	0, 		255, 	
				0, 		0, 		0, 		255, 	255, 	255, 	
				255, 	255, 	255, 	255, 	255, 	255, 	
				1, 		2, 		0, 		255, 	255, 	255, 
				3, 		3, 		0, 		255, 	255, 	255, 	
				0, 		0, 		0, 		255, 	255, 	255, 	
				17, 	14, 	0, 		255, 	255, 	255, 	
				6, 		10, 	18, 	7, 		0, 		255, 	
				255, 	255, 	255, 	255, 	255, 	255, 
				10, 	21, 	21, 	10, 	0, 		255, 	
				14, 	17, 	17, 	14, 	0, 		255, 	
				18, 	18, 	0,	 	255, 	255, 	255, 	
				10, 	10, 	10, 	0,	 	255, 	255, 	
				10, 	17, 	21, 	14, 	0,	 	255, 	
				4, 		14, 	4, 		0,	 	255, 	255, 	
				255, 	255, 	255, 	255, 	255, 	255, 	
				8, 		16, 	21, 	8, 		0,	 	255, 	
				20, 	24, 	0,	 	255, 	255, 	255, 	
				14, 	21, 	21, 	18, 	0,	 	255, 	
				255, 	255, 	255, 	255, 	255, 	255, 	
				1, 		6, 		8, 		16, 	0,	 	255, 	
				4, 		4, 		4, 		0,	 	255, 	255, 	
				9, 		19, 	21, 	9, 		0,	 	255, 	
				255, 	255, 	255, 	255, 	255, 	255, 	
				255, 	255, 	255, 	255, 	255, 	255, 	
				16, 	19, 	20, 	24, 	0,	 	255, 	
				9, 		31, 	1, 		0,	 	255, 	255, 	
				14, 	17, 	0,	 	255, 	255, 	255, 	
				255, 	255, 	255, 	255, 	255, 	255} }; 


//! Behandelt die Funktion des Moduls als Lochstreifen-Bildlocher.
static void VerbindungBildlocher()
	{
	TMsTimer WarteTimer;
	StartTimer(&WarteTimer);

	uint16_t PufferPosEin, PufferPosAus;
	bool EmpfZifferMode = false;
	bool UmsetzZifferMode = false;
	uint8_t BildSpalte = 0;

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
				GeSendeText(Kennung[SUBADDR_BILDLOCH]);
			else
				{ // im Puffer ablegen
				if (PufferPosEin < MAXPUFFER)
					Puffer[PufferPosEin++] = c;
				StartTimer(&WarteTimer);
				}
			}

		if (KoAusschalten())
			{
			GeAusschalten(false);
			break;
			}

		if (PufferPosEin > 0)
			{
			if (PufferPosAus >= PufferPosEin)
				PufferPosEin = PufferPosAus = 0;
			else if (TimerVal(&WarteTimer) > 3000 && GeSendePufferLeer())
				{
				uint8_t c = Puffer[PufferPosAus];
				if (c == TtyCodeBuUm)
					{
					UmsetzZifferMode = false;
					PufferPosAus++;
					}
				else if (c == TtyCodeZiUm)
					{
					UmsetzZifferMode = true;
					PufferPosAus++;
					}
				else // doch ein Zeichen
					{
					uint8_t BildCode = pgm_read_byte(&BildTab[UmsetzZifferMode][BildGroesse * c + BildSpalte]);
					if (BildCode == 255)
						{ // aktuelles Zeichen zu ende --> nicht senden
						BildSpalte = 0;
						PufferPosAus++;
						}
					else
						{
						GeSendeCode(BildCode);
						if (BildSpalte == BildGroesse-1)
							{ // aktuelles Zeichen zu ende
							BildSpalte = 0;
							PufferPosAus++;
							}
						else
							BildSpalte++;
						}
					} // Zeichen im Puffer
				} // Sendepuffer leer und Wartezeit für Echo abgelaufen
			} // if PufferPosEin > 0

		LEDAktualisieren();
		}

	}


//! Unterfunktion für Prüfsender. Sendet ein Zeichen mit definierter Verzerrung.
//-------------------------------------------------------------------------
//! \param Funktion Welches Bit / welche Bits sind zu verzerren:
//! 1-5 = Datenbits, 6 = Startbit, 7 = Stopbit, 8 = alle, 9 = Pegelverzerrung.
//! \param Code Baudot-Code des zu sendenden Zeichens.
//! \param Zerrgrad Grad der Verzerrung (Millisekunden oder Prozent)
static void PruefSendeZeichen(uint8_t Funktion, uint8_t Code, int8_t Zerrgrad)
	{
	TMsTimer SendeTimer;
	uint8_t TimerTab[7]; 
		// Start + 5 * Daten + Stop, Abgelegt ist jeweiliges Ende des Bits in Millisekunden
	uint8_t Bit;

	for (Bit = 0 ; Bit <= 5 ; Bit++)
		TimerTab[Bit] = 20 * (Bit + 1);
	TimerTab[6] = 150; // Stop-Bit

	Code |= 0xE0;

	// Verzerrung durchführen
	switch (Funktion)
		{
		case 1 ... 5: // Verzerrte Datenbits, Zerrgrad = Abweichung in Millisekunden
			TimerTab[Funktion] += Zerrgrad;
			break;

		case 6: // verzerrtes Startbit
			for (Bit = 0 ; Bit <= 5 ; Bit++)
				TimerTab[Bit] += Zerrgrad;
			if (Zerrgrad > 0)
				TimerTab[6] += Zerrgrad; // ggf. auch Stop-Bit nach hinten verlängern, aber nicht verkürzen
			break;

		case 7: // verzerrtes Stopbit
			TimerTab[6] += Zerrgrad;
			break;

		case 8: // abweichende Baudrate
			for (Bit = 0 ; Bit <= 6 ; Bit++)
				TimerTab[Bit] += TimerTab[Bit] * Zerrgrad / 100;
			break;

		case 9: // Mark/Space-Verzerrung
			// Entgegen Wirklichkeit wird das Startbit nicht verzerrt...
			for (Bit = 0 ; Bit <= 5 ; Bit++)
				if (BIT_IS_SET(Code, Bit))
					// Bit ist eins, daher Anfang vorziehen...
					TimerTab[Bit] -= Zerrgrad / 2;
				else
					// Bit ist Null, daher Anfang verzögern...
					TimerTab[Bit] += (Zerrgrad + 1) / 2;

			// Diagramm:
			//   Original:    Start Bit 1 Bit 2 Bit 3 Bit 4 Bit 5   Stop
			//     Mark   ---+     +-----+           +-----+     +--------+
			//     Space     +-----+     +-----+-----+     +-----+        +----
			//
			// + Verzerrt:    Start Bit 1 Bit 2 Bit 3 Bit 4 Bit 5   Stop
			//     Mark   ---+    +<----->+    >    +<----->+   +<--------+
			//     Space     +----+       +-----+---+       +---+         +----
			//
			// - Verzerrt:    Start Bit 1 Bit 2 Bit 3 Bit 4 Bit 5   Stop
			//     Mark   ---+     >+---+<     <     >+---+<     >+-------+
			//     Space     +------+   +-----+-------+   +-------+       +----

			break;
		}

	// Senden

	StartTimer(&SendeTimer);
	GeSendeMark(false);
	for (Bit = 0 ; Bit <= 6 ; Bit++)
		{
		while (TimerVal(&SendeTimer) < TimerTab[Bit])
			;
		if (BIT_IS_SET(Code, Bit))
			GeSendeMark(true);
		else
			GeSendeMark(false);
			// Stop-Bit wird gesendet, weil entsprechendes Bit in Code gelöscht wurde...
			
		if (KoAusschalten())
			break;
		}
	}


//! Sendet RYRYRYRYRYRY mit definierter Verzerrung.
//-------------------------------------------------------------------------
//! \param Funktion Welches Bit / welche Bits sind zu verzerren:
//! 1-5 = Datenbits, 6 = Startbit, 7 = Stopbit, 8 = alle, 9 = Pegelverzerrung.
static void Pruefsendung(uint8_t Funktion)
	{
				// Funktionen:		0,	1,	2,	3,	4,	5,	6,	7,	8,	9 
	static int8_t ZerrgradMin[] = { 0, -10,-10,-10,-10,-10,-10,-10,-10, -5 };
	static int8_t ZerrgradMax[] = { 0,	10,	10,	10,	10,	10,	10, 10, 10,  5 };
		 
	int8_t Zerrgrad;
	uint8_t Pos;

	KurzePause();

	for (Zerrgrad = ZerrgradMin[Funktion] ; Zerrgrad <= ZerrgradMax[Funktion] ; Zerrgrad++)
		{
		if (KoAusschalten())
			break;
		
		GeSendeCode(TtyCodeWR);
		GeSendeCode(TtyCodeZL);
		GeSendeCode(TtyCodeZiUm);
		GeSendeVorzeichenZahl(Zerrgrad);
		GeSendeZeichen(':');
		GeSendeZeichen(' ');
		GeSendeCode(TtyCodeBuUm);
		
		while (!GeSendePufferLeer())
			;

		KurzePause();

		for (Pos = 0 ; Pos < 25 ; Pos++)
			{
			if (KoAusschalten())
				break;
			PruefSendeZeichen(Funktion, 0x0A, Zerrgrad); // R
			PruefSendeZeichen(Funktion, 0x15, Zerrgrad); // Y
			}

		KurzePause();

		GeSendeCode(TtyCodeBuUm);
		}
	}



//! Behandelt die Funktion des Moduls als Testsender für definiert verzerrte Zeichen.
static void VerbindungTestsender()
	{
	GeSendeTextP(PSTR("\r\nPruefsender."));
	
	while (!KoAusschalten())
		{
		char c;

		while (KoEmpfZeichen(&c))
			; // weitere empfangene Zeichen ignorieren
	
		GeSendeTextP(PSTR("\r\nFunktion waehlen:     "));

		while (!KoEmpfZeichen(&c))
			if (KoAusschalten())
				break;

		if (KoAusschalten())
			break;
		else if (c >= '0' && c <= '9')
			Pruefsendung(c - '0');
		else if (c == ' ' || c == '\r' || c == '\n')
			; // ignorieren
		else if (c == 'e')
			{
			GeAusschalten(true);
			return;
			}
		else
			{
			if (c != '?')
				GeSendeTextP(PSTR("\r\nunbekannte Funktion. Funktionsliste:"));
			GeSendeTextP(PSTR("\r\n0: ohne Verzerrung"));
			GeSendeTextP(PSTR("\r\n1..5: Verzerrte Datenbits "));
			GeSendeTextP(PSTR("(bezogen auf Bit-Ende)"));
			GeSendeTextP(PSTR("\r\n6: verzerrtes Startbit"));
			GeSendeTextP(PSTR("\r\n7: verzerrtes Stopbit"));
			GeSendeTextP(PSTR("\r\n8: abweichende Baudrate"));
			GeSendeTextP(PSTR("\r\n9: Mark/Space-Verzerrung"));
			GeSendeTextP(PSTR("\r\nE: Ende"));
			while (!GeSendePufferLeer())
				;
			}
			
		} // while (!KoAusschalten())
			
	GeAusschalten(true);
	
	} // VerbindungTestsender


//! Bearbeitet alle kommenden Verbindungen.
static void VerbindungKommend()
	{
	set_LED_GRUEN();
	
	if (GeEinschalten() != GeEinschAnrufquitt)
		{
		GeAusschalten(true);
		}
	else
		{
		if (VerbindungIgnoriereErsteZweiSekunden(KoAnwahlnummer())) // gibt bei vorzeitigem Verbindungsabbau false zurück.
			{
			switch (KoAnwahlnummer())
				{
				case SUBADDR_MESSUNG:
					VerbindungMessgeraet();
					return;

				case SUBADDR_PRUEFSEND:
					VerbindungTestsender();
					return;
				
				case SUBADDR_BILDLOCH:
					VerbindungBildlocher();
					return;

				case SUBADDR_RUECKRUF:
					VerbindungRueckruf();
					return;
			
				}
			}
		}

	// folgender Punkt wird nur bei ungültiger Anwahlnummer erreicht...
	GeAusschalten(true);
		
	}
	

//! wird nach kurzem Tastendruck aufgerufen
static void Deaktivieren()
	{
	set_LED_BLAU();
	Aktivieren(false);

	while (Tastendruck == NichtGedr)
		TastePruefen();
	Tastendruck = NichtGedr;

	Aktivieren(true);
	
	clr_LED_BLAU();

	} // Deaktivieren


//! Funktion, die während der FernDialog-Ausführung interne Aufgaben erledigt.
//----------------------------------------------------------------------------
void FernDialogCallback()
	{
	LEDAktualisieren();
	}


//! wird nach langem Tastendruck aufgerufen
static void Konfiguration()
	{
	uint8_t TestA;
	
	if (!FernDialogVerbinden(0)) // 0 = Startadresse
		{
		// Todo Aufräumen
		return;
		}

	set_LED_GRUEN();			

	if (TextAusgabeFern(PSTR("\r\n konfiguration messgeraet version " SVNVERSION " datum " __DATE__))
		&& ZahlAbfrageFern(PSTR("testabfrage zahl"), &TestA, 1)
		// && BitAbfrageFern(PSTR("feste hauptstelle"), &KonfigBits, 1 << KonfigBit_FesterHauptanschluss)
		// && (!BIT_IS_SET(KonfigBits, KonfigBit_FesterHauptanschluss) // folgende Abfrage nur bei FesterHauptanschluss
		    // || ZahlAbfrageFern(PSTR("nummer der hauptstelle"), &Hauptanschluss, 2))
		// && BitAbfrageFern(PSTR("alternativ-suche bei besetzt"), &KonfigBits, 1 << KonfigBit_SucheAlternativBeiBesetzt)
		// && BitAbfrageFern(PSTR("kommende durchwahl zulassen"), &KonfigBits, 1 << KonfigBit_DurchwahlErlaubt)
		// && (!BIT_IS_SET(KonfigBits, KonfigBit_DurchwahlErlaubt) // folgende Abfrage nur bei nicht gesperrter Durchwahl
		    // || DurchwahlenAbfrage())
		// && BitAbfrageFern(PSTR("wahlfreigabe mit waehlton"), &WaehltonErkennung, 1)
		// && (WaehltonErkennung // folgende Abfrage nur bei nicht durch Wählton erfolgende Freigabe
			// || ZahlAbfrageFern(PSTR("verzoegerung wahlfreigabe (x/10 sek)"), &WahlbeginnVerzoegerungFest, 1))
		// && ZahlAbfrageFern(PSTR("verzoegerung letzte ziffer - beginn kennton ...\r\n ... (x/10 sek)"), &VerbindungsaufbauVerzoegerung, 1)
		// && JustierWahlziffernAbfragen()
		// && ZahlAbfrageFern(PSTR("justierung verzoegerung abheben - erste ziffer ...\r\n ... (x/10 sek)"), &JustierWahlVerzoegerung, 1)
		// && ZahlAbfrageFern(PSTR("justierung verzoegerung auflegen - abheben nach taste ...\r\n ... (x/10 sek)"), &JustierNeustartPause, 1)
		&& TextAusgabeFern(PSTR("\r\n fertig +++\r\n")))
		{ // kein Abbruch, daher ordnungsgemäß abstellen
		BusSenden(BusKdoSchluss);
		WarteSchlussQuittung(2500);
		Grundstellen(false);
		}
	else
		{ // es wurde ein Kommando empfangen, welches nicht Mark oder Space befahl... Abbruch?
		// TODO Aufräumen??? 
		}

	clr_LED_GRUEN();
		
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

	init_LED_ROT(); 
	set_LED_ROT();
	init_LED_GELB();
	clr_LED_GELB();
	init_LED_GRUEN();
	clr_LED_GRUEN();
	init_LED_BLAU();
	clr_LED_BLAU();
	
	// Timer initialisieren
	MsTimerInit();

	/*/ TEST: Zeitbedarf für Berechnung der Prüfsendemuster ausprobieren...
	PruefSendeZeichen(1, 0x15, 10);
	// :TEST */

	TMsTimer Timer;
	StartTimer(&Timer);

	BusEigenAdresse = eeprom_read_byte(&BusEigenAdresse_EE) & 0xFE;
	
	if (BusEigenAdresse < BusAdrMin 
		|| BusEigenAdresse > BusAdrMax 
		|| (BusEigenAdresse & 0x07) != 0 //    ^^^^ muss durch 4 Teilbar sein, letztes Bit sowieso 0
		|| eeprom_read_byte(&BusEigenAdresse_EE) != ~(BusEigenAdresse >> 1))
											
		BusEigenAdresse = STANDARD_NUMMER << 1; // Standardwert

	BusEigenAdrMehrfach = BUS_MEHRFACH_ADR;

	for (uint8_t i = 0 ; i < BUS_MEHRFACH_ADR ; i++)
		{
		eeprom_read_string(Kennung[i], Kennung_EE[i], sizeof(Kennung[i]));
		Kennung[i][KENNUNG_MAXLEN-1] = '\0';
		}
	if (Kennung[0][0] == '\377')
		strcpy_P(Kennung[0], PSTR("\r\ntxp2-mess"));
	if (Kennung[1][0] == '\377')
		strcpy_P(Kennung[1], PSTR("\r\ntxp2-pruefsend"));
	if (Kennung[2][0] == '\377')
		strcpy_P(Kennung[2], PSTR("\r\ntxp2-bildloch"));
	if (Kennung[3][0] == '\377')
		strcpy_P(Kennung[3], PSTR("\r\ntxp2-rueckruf"));
		
	eeprom_read_string(Kennwort, Kennwort_EE, sizeof(Kennwort));
	if (Kennwort[0] == '\377')
		strcpy_P(Kennwort, PSTR("kennwort"));
	else
		Kennwort[sizeof(Kennwort)-1] = '\0'; // sicherheitshalber

	UmleitungAbweisen = true;
	
	KommInit();

	sei();

	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 250)
		;
		
	clr_LED_ROT();
	set_LED_GELB();

	TwiInit();

	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 500)
		;

	clr_LED_GELB();
	set_LED_GRUEN();

	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 750)
		;

	clr_LED_GRUEN();
	set_LED_BLAU();

	// TWI nochmal resetten
	TWCR = (1<<TWINT) | (0<<TWEA) | (0<<TWSTA) | (1<<TWSTO) | (0<<TWEN) | (0<<TWIE);

	while (TimerVal(&Timer) < 760)
		;
		
	TWCR = (1<<TWINT) | (1<<TWEA) | (0<<TWSTA) | (0<<TWSTO) | (1<<TWEN) | (1<<TWIE);

	while (TimerVal(&Timer) < 1000 + BusEigenAdresse)
		;

	BusEigenAdressePruefenUndSetzen(BusEigenAdresse);

	while (true)
		{
		// aktueller Zustand: Ausgeschaltet
		clr_LED_GELB();
		clr_LED_GRUEN();
		clr_LED_BLAU();

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
		
		for (uint8_t i = 0 ; i < BUS_MEHRFACH_ADR ; i++)
			eeprom_write_string_noblock(Kennung_EE[i], Kennung[i]);
		eeprom_write_string_noblock(Kennwort_EE, Kennwort);
	
		} // while (1)
	} // main()


