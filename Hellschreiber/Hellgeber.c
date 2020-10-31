
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <string.h>
#include <inttypes.h>

#include "Hellgeber.h"

#include "Defports.h"


const uint16_t HellFontTab[] PROGMEM = {
#include "HellFont.h"
0 } ;

#define HELL_FONT_CHAR_SIZE 6 // 6 Einträge in der Tabelle je Zeichen
#define HELL_FONT_TAB_START 10 // Linefeed
#define HELL_FONT_TAB_END 127

#define HELL_TON 1000L // 1 kHz Sendefrequenz
#define HELL_RUF 25 // 25 Hz 
#define HELL_PIXELTAKT 600 // 600 Pixel pro Sekunde, aber mind. 2 Pixel Schwarz oder Weiß
#define HELL_STARTCODE 0x0FF0 // Startimpuls
#define HELL_PIXEL_PRO_REIHE 14 // 14 Pixel übereinander
#define HELL_REIHEN_PRO_ZEICHEN 7 // 7 Pixel nebeneinander


enum { MaxPuffer = 512 } ; // Konstanten-Krücke, muß 2-er Potenz sein

static char Puffer[MaxPuffer]; // Ringpuffer!
volatile static uint16_t PufferEin, PufferAus; // Ringpuffer!

enum { StatAus, StatRuf, StatWarteAktiv, StatSendeVerzoegerung, StatAktiv, StatAbschalt } HellStatus;
//!< Welchen Zustand soll der Hellschreiber gerade haben


// Timer 0 bestimmt den Pixeltakt

#define TIMER0_CS ((1<<CS02) | (0<<CS01) | (0<<CS00))
#define TIMER0_PRESCALER 256
#define TIMER0_TOP (F_CPU / TIMER0_PRESCALER / HELL_PIXELTAKT - 1)

volatile uint16_t Timer0Counter;


// Arbeitsvariablen für Timer 0

static volatile uint8_t HellPixelZaehler;
static volatile uint8_t HellReiheZaehler;
static volatile uint16_t HellSendeCode;
static volatile uint16_t HellSendeTabPos;

// Timer 1 bestimmt die Ausgabefrequenz (Anruf 25 Hz, Ton 1000 Hz)

#define TIMER1_TON_CS ((0<<CS12) | (0<<CS11) | (1<<CS10))
#define TIMER1_TON_PRESCALER 1
#define TIMER1_TON_PWMTOP (F_CPU / TIMER1_TON_PRESCALER / HELL_TON - 1) // 12000
#define TIMER1_TON_PWMLEVEL (TIMER1_TON_PWMTOP / 8 + 1) 

#define TIMER1_RUF_CS ((0<<CS12) | (1<<CS11) | (1<<CS10))
#define TIMER1_RUF_PRESCALER 64
#define TIMER1_RUF_PWMTOP (F_CPU / TIMER1_RUF_PRESCALER / HELL_RUF - 1) // 7500
#define TIMER1_RUF_PWMLEVEL (TIMER1_RUF_PWMTOP / 2 + 1) // 3750



// Definition der IO-Ports

DEFPORTOUT(LEDrot, C, 3)		// Puffer ist voll ODER Rufton
DEFPORTOUT(LEDgelb, C, 2)		// Puffer ist nicht leer / Sendetakt
DEFPORTOUT(LEDgruen, C, 1)		// Maschine läuft


DEFPORTIN(HellEmpfangsbereit, B, 0)

DEFPORTOUT(HellMod, B, 1) // Muss OC1A sein, sonst Code-Änderung erforderlich!

DEFPORTINPULL(Taste, B, 2)

DEFPORTINPULL(HellTest, C, 0)


static void inline HellRufTonEin()
	{
	TCCR1A |= (1<<COM1A1);
	clr_LEDgelb(); 
	}


static void inline HellRufTonAus()
	{
	TCCR1A &= ~( (1<<COM1A1) | (1<<COM1A0) );
	clr_HellMod();
	set_LEDgelb();
	}

	
static bool HellschreiberLaeuft()
	{
	bool Res = !get_HellEmpfangsbereit(); // aktiv LOW
	bset_LEDgruen(Res);
	return Res;
	}
	

// Timer 0: Pixeltakt

ISR(TIMER0_COMPA_vect)
	{
	Timer0Counter++;

	if (HellReiheZaehler == 0)
		return; // es läuft gerade keine Ausgabe

	if (HellSendeCode & (1<<15))
		HellRufTonEin();
	else
		HellRufTonAus(); 

	if (--HellPixelZaehler == 0)
		if (--HellReiheZaehler == 0)
			{
			HellRufTonAus(); 
			}
		else
			{
			HellSendeCode = pgm_read_word(&HellFontTab[HellSendeTabPos++]);
			HellPixelZaehler = HELL_PIXEL_PRO_REIHE;
			}
	else
		HellSendeCode <<= 1;
						
	}


static void HellRufInit()
	{
	TCCR1A = (1 << WGM11) | (0 << WGM10); // COM1Ax wird in HellRufTonEin() bearbeitet
	TCCR1B = (1 << WGM13) | (1 << WGM12) | TIMER1_RUF_CS; // Fast PWM
	TCCR1C = 0;
	OCR1A = TIMER1_RUF_PWMLEVEL;
	ICR1 = TIMER1_RUF_PWMTOP;
	}


static void HellTonInit()
	{
	TCCR1A = (1 << WGM11) | (0 << WGM10); // COM1Ax wird in HellRufTonEin() bearbeitet
	TCCR1B = (1 << WGM13) | (1 << WGM12) | TIMER1_TON_CS; // Fast PWM
	TCCR1C = 0;
	OCR1A = TIMER1_TON_PWMLEVEL;
	ICR1 = TIMER1_TON_PWMTOP;
	}


void HellInit()
	{
	init_LEDrot();
	init_LEDgelb();
	init_LEDgruen();
	init_HellEmpfangsbereit();
	init_HellMod();
	init_HellTest();
	init_Taste();

	HellTonInit();
	PufferEin = 0;
	PufferAus = 0;
	HellReiheZaehler = 0;
	HellPixelZaehler = 0;

	TCCR0A = (1 << WGM01) | (0 << WGM00); // CTC-Mode
	TCCR0B = (0 << WGM02) | TIMER0_CS;
	OCR0A = TIMER0_TOP;
	TIMSK0 = (1 << OCIE0A);
	}


void HellAusg(char c)
	{
	switch (c)
		{
		case 'ä': HellAusg('a'); HellAusg('e'); return;
		case 'ö': HellAusg('o'); HellAusg('e'); return;
		case 'ü': HellAusg('u'); HellAusg('e'); return;
		case 'Ä': HellAusg('A'); HellAusg('E'); return;
		case 'Ö': HellAusg('O'); HellAusg('E'); return;
		case 'Ü': HellAusg('U'); HellAusg('E'); return;
		case 'ß': HellAusg('s'); HellAusg('s'); return;
		}

	if (c <= HELL_FONT_TAB_START)
		c = ' ';
	else if (c >= HELL_FONT_TAB_END)
		c = '?';

	uint16_t n = (PufferEin + 1) & (MaxPuffer - 1);
	if (n != PufferAus)
		{
		Puffer[PufferEin] = c;
		PufferEin = n;
		}

	set_LEDgelb();

	}


static void TestSendenZ(char z)
	{
	uint8_t i;

	for (i = 0 ; i < 20 ; i++)
		HellAusg(z);
	HellAusg(' ');
	HellAusg(' ');
	}


static void TestSenden()
	{
	PufferAus = 0;
	strcpy_P(Puffer, PSTR("USB2HELL " __DATE__ " " __TIME__ "  "));
	PufferEin = strlen(Puffer);
	TestSendenZ('L');
	TestSendenZ('.');
	TestSendenZ('W');
	TestSendenZ('/');
	TestSendenZ('E');
	TestSendenZ('=');
	TestSendenZ('4');
	}


void HellPoll()
	{
	switch (HellStatus)
		{
		case StatAus:
			if (PufferEin != PufferAus)
				{ // Einschalten, damit auch was gedruckt werden kann
				HellRufInit();
				HellRufTonEin();
				set_LEDrot();
				Timer0Counter = 0;
				HellStatus = StatRuf;
				}
			else if (!get_HellTest()) // Aktiv LOW
				{
				TestSenden();
				}
			else
				{
				clr_LEDgelb();
				if (HellschreiberLaeuft())
					{ // warum auch immer das Teil eingeschaltet ist
					Timer0Counter = 0;
					HellStatus = StatSendeVerzoegerung;
					}
				}
			break;

		case StatRuf:
			if (Timer0Counter >= 1 * HELL_PIXELTAKT) // Rufsignal nur 1 Sekunde
				{
				HellRufTonAus();
				if (!HellPuffervoll())
					clr_LEDrot();
				Timer0Counter = 0;
				HellStatus = StatWarteAktiv;
				}
				
			break;

		case StatWarteAktiv:
			if (Timer0Counter >= 8 * HELL_PIXELTAKT) // Rufsignal nach 8 Sekunden wiederholen
				{
				HellRufTonEin();
				set_LEDrot();
				Timer0Counter = 0;
				HellStatus = StatRuf;
				}
			else if (Timer0Counter >= 1 * HELL_PIXELTAKT && HellschreiberLaeuft() /*invertiert!*/ ) 
				{ // Schleife geschlossen (wegen potenzieller Störung durch das Rufsignal 1 Sekunden warten
				HellRufTonAus();
				HellTonInit();
				if (!HellPuffervoll())
					clr_LEDrot();
				Timer0Counter = 0;
				HellStatus = StatSendeVerzoegerung;
				}
				
			break;

		case StatSendeVerzoegerung:
			if (Timer0Counter >= 2 * HELL_PIXELTAKT) // 2 Sekunden Vorwärmung des Schreibverstärkers
				{
				Timer0Counter = 0;
				HellStatus = StatAktiv;
				}
			break;

		case StatAktiv:
			if (!HellschreiberLaeuft() && HellReiheZaehler == 0)
				{ // plötzliche Abschaltung, letztes Zeichen wurde vollständig ausgegeben
				HellRufTonAus();
				HellStatus = StatAus;
				PufferAus = PufferEin; // bewirkt Abbruch der Ausgabe
				break;
				}

			if (PufferEin != PufferAus)
				{ // es gibt noch Zeichen zu senden
				if (HellReiheZaehler == 0)
					{ // es darf ein neues Zeichen angefangen werden
					HellSendeCode = HELL_STARTCODE;
					HellPixelZaehler = HELL_PIXEL_PRO_REIHE;
					HellReiheZaehler = HELL_REIHEN_PRO_ZEICHEN;
					HellSendeTabPos = (Puffer[PufferAus++] - HELL_FONT_TAB_START) * HELL_FONT_CHAR_SIZE;
					PufferAus &= (MaxPuffer-1); // Ringpuffer
					TCNT0 = 0; // damit ein voller Pixel ausgegeben wird
					}
				Timer0Counter = 0;
				}
			else // nichts mehr zu senden
				{
				clr_LEDgelb();
				if (Timer0Counter > 20 * HELL_PIXELTAKT)
					{
					HellTonInit();
					HellRufTonEin(); // Dauerton für Abschaltung
					HellStatus = StatAbschalt;
					Timer0Counter = 0;
					}
				}
			break;

		case StatAbschalt:
			if (!HellschreiberLaeuft())
				{
				HellRufTonAus();				
				clr_LEDrot();
				clr_LEDgelb();
				HellStatus = StatAus;
				}

			if (Timer0Counter > 30 * HELL_PIXELTAKT)
				{
				HellRufTonAus(); // Dauerton aus, da Abschaltung defekt zu sein scheint.
				}

			break;

		} // switch HellStatus

	if (!get_Taste()) // LOW = gedrückt!
		{ 
		// Puffer löschen und stillsetzen
		PufferEin = 0;
		PufferAus = 0;

		if (HellStatus == StatWarteAktiv)
			{
			clr_LEDgelb();
			HellStatus = StatAus;
			}
		// alle Anderen Statuswechsel über den leeren Puffer
		} // if Taste
		
	} // HellPoll()



bool HellPuffervoll()
	{
	static bool IstVoll;
	int16_t Fuell;

	Fuell = PufferEin - PufferAus;
	if (Fuell < 0)
		Fuell += MaxPuffer; // Ringpuffer!
		
	if (Fuell >= MaxPuffer * 2/3)
		IstVoll = true;
	else if (Fuell <= MaxPuffer / 5)
		IstVoll = false;
	// sonst bleibt es unverändert

	return IstVoll;
	}

