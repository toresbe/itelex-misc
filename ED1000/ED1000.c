//================================================================
// Fernschreiber-Schnittstelle ED1000 für TxP2-System
//	für ATmega168 auf Platine ED1000
//================================================================

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

#include "WaveTab.h"

#include "../SvnVersion.h"


// Schalter für Code-Varianten
// ===========================


//#define FALSCHKDO_FEHLERSTOP
	// Unpassende Kommandos auf dem I²C-Bus werden mit Fehlerstop quittiert

//#define TWI_DEBUG
	// TWI-Ereignisse werden protokolliert. 

#define BUSFEHLER_ABBRUCH
	// bei Bus-Fehlern Abbruch der Verbindung

#ifndef TWI_DEBUG
#define WIEDERHOLUNGSSENDUNGEN
	// Status Mark / Space regelmäßig senden 
#endif //TWI_DEBUG


// #define LEDROT_BEI_UNERWARTETWDH
	// LED rot wird eingeschaltet, wenn BusKdoSpaceWdh oder BusKdoMarkWdh empfangen wird, ohne
	// das entsprechendes "Haupt-Kommando" empfangen wurde


//#define NOWATCHDOG
	// Watchdog abgeschaltet


#define V21
	// macht andere Frequenzen
	

#ifdef V21	
const PROGMEM char Identifier[] = "___TxP2_V21___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#else
const PROGMEM char Identifier[] = "___TxP2_ED1000___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#endif 


// Einstellungen für Timer 1: Sinus-Ausgabe und ADC-Start und Empfangsfilterung
// ----------------------------------------------------------------------------

#define TIMER1_PRESCALER 8
#define TIMER1_CS TCCR_DIV(1, 8)
#define TIMER1_FREQ (F_CPU / TIMER1_PRESCALER)

#ifdef V21
	#define TIMER1_OCR (147 - 1)
	#define TIMER1_OCFREQ (TIMER1_FREQ / (TIMER1_OCR + 1))
#else
	#define TIMER1_OCFREQ 12800
	#define TIMER1_OCR (TIMER1_FREQ / TIMER1_OCFREQ - 1)
#endif

// Sendefrequenzen
// ----------------
#ifdef V21
	#define SEND_MARK_FAKTOR 10
	#define SEND_SPACE_FAKTOR 12
#else
	#define SEND_MARK_FAKTOR 7
	#define SEND_SPACE_FAKTOR 5
#endif

// Eeprom-Speicher
// ---------------

uint8_t Platzhalter[4] EEMEM; // Anfang des EEPROM ist gern von Störungen betroffen
uint8_t BusEigenAdresse_EE EEMEM = BusAdrUngueltig;
uint8_t KommendSperreWahl_EE EEMEM = 0;


// Variablen
// ---------

static volatile uint8_t SinAusgI;

static volatile uint8_t SinAusgInc;

#define EMPFBUFSIZE (1<<5)

static volatile int8_t EmpfBuf[EMPFBUFSIZE];

static volatile uint8_t EmpfBufSchreibI;

static uint8_t EmpfBufLeseI;


void InitTimer()
	{
	SinAusgI = 0;
	SinAusgInc = 0;
	EmpfBufLeseI = 0;
	EmpfBufSchreibI = 0;
	TCCR1A = (0<<WGM11) | (0<<WGM10); // CTC Mode
	TCCR1B = (0<<WGM13) | (1<<WGM12) | TIMER1_CS; // CTC Mode
	TCCR1C = 0;
	OCR1A = TIMER1_OCR; 
	SET_BIT(TIMSK1, OCIE1A);
	}


void InitADC()
	{
	ADMUX = (0<<REFS1) | (1<<REFS0) | (1<<ADLAR) | (0b0000<<MUX0);

	ADCSRA = (1<<ADEN) | (1<<ADSC) | (0<<ADATE) | (0<<ADIE) | (1<<ADPS2) | (1<<ADPS1) | (0<<ADPS0);

	while (BIT_IS_SET(ADCSRA, ADSC))
		;

	SET_BIT(ADCSRA, ADIF);
	//SET_BIT(ADCSRA, ADIE);

	DIDR0 = 1; // Digital-Eingabg Bit 0 deaktivieren

	}

	
ISR(TIMER1_COMPA_vect)
	{
	// letztes AD-Ergebnis retten
	EmpfBuf[EmpfBufSchreibI++] = ADCH - 128;
	EmpfBufSchreibI &= (EMPFBUFSIZE-1);

	SinAusgI += SinAusgInc;
	SinAusgI &= 127;

	PORTB = pgm_read_byte(&Sinus63[SinAusgI]);

	SET_BIT(ADCSRA,	ADSC); // AD-Wandler starten (dabei wird ADIF mit gelöscht!)
 
	}


bool BefehlEinschalten;
bool BefehlMark;

bool EmpfangMark; // ist false, wenn Endgerät ausgeschaltet oder Endgerät Space sendet
bool MeldungEingeschaltet; // false bei ausgeschaltetem Endgerät
bool MeldungMark; // false bei Space vom Endgerät
bool SpaceSperre;

int8_t x0, x1, x2, ym0, ym1, ym2, ys0, ys1, ys2;

uint8_t PegelGlaettZaehl;

uint16_t EinAusschaltZaehl;


#define EINSCHALT_VERZ (TIMER1_OCFREQ / 10) // 1/10 sek. Mark = ein

#define AUSSCHALT_VERZ (TIMER1_OCFREQ / 2) // 1/2 sek. Space = aus

#define PEGEL_GLAETT 50


static void ED1000Init()
	{
	x0 = x1 = x2 = ym0 = ym1 = ym2 = ys0 = ys1 = ys2 = 0;
	EmpfangMark = false;
	BefehlEinschalten = false;
	MeldungEingeschaltet = false;
	BefehlMark = true;
	MeldungMark = true;
	}
	

static void ED1000IO()
	{
	if (BefehlEinschalten && BefehlMark)
		SinAusgInc = SEND_MARK_FAKTOR;
	else
		{
		SinAusgInc = SEND_SPACE_FAKTOR;
		SpaceSperre = true;
		}
		
	// Messungen auswerten
	uint8_t AnzMesswerte = 0;
	
	while (EmpfBufLeseI != EmpfBufSchreibI)
		{
		x0 = EmpfBuf[EmpfBufLeseI++];
		EmpfBufLeseI &= (EMPFBUFSIZE-1);

		// Filterung: Parameter wurden mit dem Programm WinFilter errechnet.

#ifdef V21
		// MARK: 
		// Einstellungen: Samplerate = 12539, IIR Bandpass Butterworth  f1 = 1680, f2 = 1750, Order = 1, 8 Bit
		// Ergebnis aus generiertem C-Code: 
		//		__int8 ACoef[NCoef+1] = {  73,   0, -73 };
		//		__int8 BCoef[NCoef+1] = {  64, -82,  61 };
		// --> ym0 = (73 * (x0 - x2) + 82 * ym1 - 102 * ym2) / 64;
		//           A0;A2            -B1         -B2          B0
		// Zur Vermeidung von Überlauf bei 8-Bit-Berechnungen alle A-Koeffizienten / 3 und alle B-Koeffizienten * 4
		// --> ys0 = (24 * (x0 - x2) + 328 * ym1 - 244 * ym2) / 256;
		//           A0;A2/3          -B1*4       -B2*4       B0*4     
		//                      328 = 256 + 72
		int16_t h =  (24 * (x0 - x2) +  72 * ym1 - 244 * ym2);
		ym0 = (h >> 8) + ym1; // <- hier kommen die fehlenden 256 * ym1 aus der Berechnung von h nachträglich dazu.
		
		// SPACE: 
		// Einstellungen: Samplerate = 12539, IIR Bandpass Butterworth  f1 = 1900, f2 = 1950, Order = 1, 8 Bit
		// Ergebnis aus generiertem C-Code: 
		//		__int8 ACoef[NCoef+1] = {  95,   0, -95 };
		//		__int8 BCoef[NCoef+1] = {  64, -72,  62	};
		// --> ys0 = (95 * (x0 - x2) + 72 * ys1 - 62 * ys2) / 64;
		//           A0/A2            -B1        -B2          B0
		// Zur Vermeidung von Überlauf bei 8-Bit-Berechnungen alle A-Koeffizienten / 3 und alle B-Koeffizienten * 4
		// --> ys0 = (32 * (x0 - x2) + 288 * ys1 - 248 * ys2) / 256;
		//           A0;A2/3          -B1*4       -B2*4         B0*4
		//                      288 = 256 + 32
		         h = (32 * (x0 - x2)  + 32 * ys1 - 248 * ys2);
		ys0 = (h >> 8) + ys1; // <- hier kommen die fehlenden 256 * ys1 aus der Berechnung von h nachträglich dazu.
		
#else		
		// SPACE: (Berechnung heißt Mark???)
		// Einstellungen: Samplerate = 12800, IIR Bandpass Butterworth  f1 = 2150, f2 = 2650, Order = 1, 8 Bit
		// Ergebnis aus generiertem C-Code: 
		//		__int8 ACoef[NCoef+1] = {  67,   0, -67 };
		//		__int8 BCoef[NCoef+1] = { 128, -87,  99	};
		// --> ys0 = (67 * (x0 - x2) + 87 * ys1 - 99 * ys2) / 128;
		//           A0/A2            -B1        -B2           B0
		int16_t h = (67 * (x0 - x2) + 87 * ys1 - 99 * ys2);
		ys0 = h >> 7;

		// MARK: Berechnung heißt Space???
		// Einstellungen: Samplerate = 12800, IIR Bandpass Butterworth  f1 = 3150, f2 = 3600, Order = 1, 8 Bit
		// Ergebnis aus generiertem C-Code: 
		//		__int8 ACoef[NCoef+1] = {  80,   0, -80 };
		//		__int8 BCoef[NCoef+1] = { 128,  19, 102 };
		// --> ym0 = (80 * (x0 - x2) - 19 * ym1 - 102 * ym2) / 128;
		//           A0/A2            -B1        -B2           B0
		h = (80 * (x0 - x2) - 19 * ym1 - 102 * ym2);
		ym0 = h >> 7;
		
#endif //ndef V21

		x2 = x1; x1 = x0;
		ym2 = ym1; ym1 = ym0;
		ys2 = ys1; ys1 = ys0;

		// Gleichrichten:
		if (ym0 < 0) ym0 = -ym0;
		if (ys0 < 0) ys0 = -ys0;

		if (ym0 > ys0)
			// Mark über Space
			if (PegelGlaettZaehl < PEGEL_GLAETT)
				PegelGlaettZaehl++;
			else
				EmpfangMark = true;
		else
			// Space über Mark
			if (PegelGlaettZaehl > 0)
				PegelGlaettZaehl--;
			else
				EmpfangMark = false;
				
		AnzMesswerte++;
		}
		
	if (EmpfangMark)
		{
		MeldungMark = true;
		SpaceSperre = false;
		}
	else if (SpaceSperre)
		MeldungMark = true;
	else
		MeldungMark = false;
		
	// Entscheidung Aus oder Ein (unabhängig von SpaceSperre)
	if (EmpfangMark)
		{ // kann nur bei Einschaltung anstehen
		if (MeldungEingeschaltet)
			EinAusschaltZaehl = 0; // ist schon an, nix tun...
		else if (EinAusschaltZaehl + AnzMesswerte >= EINSCHALT_VERZ)
			{
			MeldungEingeschaltet = true;
			EinAusschaltZaehl = 0;
			}
		else
			EinAusschaltZaehl += AnzMesswerte;
		}
	else
		{ // Kann Space oder Ausschaltung sein
		if (!MeldungEingeschaltet)
			EinAusschaltZaehl = 0; // ist schon aus, nix tun...
		else if (EinAusschaltZaehl + AnzMesswerte >= AUSSCHALT_VERZ)
			{
			MeldungEingeschaltet = false;
			EinAusschaltZaehl = 0;
			}
		else
			EinAusschaltZaehl += AnzMesswerte;
		}

	if (BefehlEinschalten)
		bset_LEDBLAU(!BefehlMark);

	if (MeldungEingeschaltet)
		{
		if (BIT_IS_SET(Status, StatBit_AngerufenBelegt))
			bset_LEDGELB(!MeldungMark);
		else
			bset_LEDGRUEN(!MeldungMark);
		}
	}

	
uint8_t KommendSperreWahl;

char BuZiMode;


static bool ED1000Einschalten()
	{
	TMsTimer StabilTimer;
	TMsTimer AbbruchTimer;
	
	StartTimer(&StabilTimer);
	StartTimer(&AbbruchTimer);
	do
		{
		BefehlEinschalten = true;
		BefehlMark = true;
		ED1000IO();
		if (!MeldungEingeschaltet)
			StartTimer(&StabilTimer);
		if (TimerVal(&AbbruchTimer) > 7000)
			{
			BefehlEinschalten = false;
			BefehlMark = true;
			ED1000IO();
			return false;
			}
		} while (TimerVal(&StabilTimer) < 300);
	return true;
	}
		

static void ED1000Ausschalten()
	{
	TMsTimer Timer;
	
	if (MeldungEingeschaltet && !BefehlEinschalten)
		ED1000Einschalten(); // Rückgabewert ignorieren

	StartTimer(&Timer);
	do
		{
		BefehlEinschalten = false;
		BefehlMark = true;
		ED1000IO();
		if (MeldungEingeschaltet)
			StartTimer(&Timer);
		}
	while (TimerVal(&Timer) < 500);
	}
		

void FehlerStop(int Nummer)
// Nur ein Reset befreit
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
#ifdef TASTE_NACH_PLUS
			if (get_TASTE())
#else
			if (!get_TASTE())
#endif
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


char LokalZeichenLesen()
	{
	char c;

	while (true)
		{
		ED1000IO();
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


static void LokalCodeAusgabe(uint8_t code)
	{
	SerUmSendDaten = code;
	SerUmSendBitNr = SerUmSendStart;
	while (SerUmSendBitNr != SerUmSendWarte)
		{
		SeriellUmsetzung(MeldungMark, &BefehlMark);
		ED1000IO();
		}
	}


void LokalZeichenAusgabe(char c)
	{
	uint8_t code1, code2;
	
	if (c >= 'A' && c <= 'Z')
		c += 'a'-'A';
	if (!ZeichenZuCode2(c, &BuZiMode, &code1, &code2))
		return;
	LokalCodeAusgabe(code1);
	if (code2 != 255)
		LokalCodeAusgabe(code2);
	}
			
		
static void VerbindungSteht();

static void Deaktivieren(bool WegenTimeout);


static void VerbindungKommend()
	{
	ED1000IO();

	set_LEDGRUEN();
	set_LEDROT();
	
	if (!ED1000Einschalten())
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
		ED1000Ausschalten();
		}
	else
		VerbindungSteht();
		
	}
	

static void AbschaltungZuLangeWahlpause(bool Abschaltimpuls)
	{
	set_LEDROT();
	GeAusschalten();
	Aktivieren(false);
	if (Abschaltimpuls)
		ED1000Ausschalten();
	while (MeldungEingeschaltet)
		ED1000IO();
	clr_LEDROT();
	Aktivieren(true);
	}
	
	
static bool WahlMitTastatur()
	{
	char c;
	TMsTimer WahlendeTimer;
	bool EsWurdeGewaehlt;
	int Falschziffern;
	
	if (!ED1000Einschalten())
		return false;

	SeriellUmsetzInit();
		
	LokalCodeAusgabe(TtyCodeZiUm);

	StartTimer(&WahlendeTimer);
	EsWurdeGewaehlt = false;
	Falschziffern = 0;
	BuZiMode = '\0';

	while (true)
		{
		ED1000IO();
		
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
			// ED1000Ausschalten() macht die aufrufende Routine
			return false;
			}
			
		if (KoEinschalten())
			{
			TMsTimer KurzePause;

			GeSendeMark(true); // Erst mal einschalten...

			StartTimer(&KurzePause);
			while (TimerVal(&KurzePause) < 300)
				ED1000IO();
				
			GeSendeCode(TtyCodeZiUm);
			GeSendeCode(TtyCodeZiUm);
			GeSendeCode(TtyCodeZiUm);
			GeSendeCode(TtyCodeZiWerDa);
			
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
	set_LEDGELB();

	if (BusEigenAdresse == BusAdrUngueltig)
		{
		ED1000Ausschalten();
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
			ED1000Ausschalten();
			
			if (KommendSperreWahl != 0 && LetzteInterneWahl() == KommendSperreWahl)
				{
				clr_LEDGELB();
				KommendSperren();
				}
				
			return;

/*
		case GeEinschSofortEin:
			ED1000Einschalten();
			break; // ist jetzt Verbunden

		case GeEinschFremdKonfig:
			ED1000Einschalten();
// passt nicht mehr...			LeitungsSstKonfigurationsDialog();
			ED1000Ausschalten();
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
		ED1000IO();

		if (!MeldungEingeschaltet || KoAusschalten())
			{
			ED1000Ausschalten();
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
	set_LEDROT();
	
	if (!ED1000Einschalten())
		return;
	
	LokalTextAusgabeP(PSTR("\r\n konfiguration ed1000 version " SVNVERSION " datum " __DATE__));
	
	// Durchwahl...
	if (!KonfigurationAllgemein())
		return;

	// Einschaltung der Sperre für kommende Rufe durch Wahl von...

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
	ED1000Ausschalten();
	Aktivieren(true);
	clr_LEDROT();
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
			
		ED1000IO();
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


//! Das Hauptprogramm der ED1000-Fernschreiber-Schnittstelle.
//----------------------------------------------------------
int main()
	{
#ifndef NOWATCHDOG
	wdt_enable(WDTO_2S);
#endif //NOWATCHDOG

	// Ports Initialisieren
	init_LEDROT();
	init_LEDGELB();
	init_LEDGRUEN();
	init_LEDBLAU();
	init_SIGAUS0();
	init_SIGAUS1();
	init_SIGAUS2();
	init_SIGAUS3();
	init_SIGAUS4();
	init_SIGAUS5();
	init_SIGEIN();
	init_TASTE();
	//init_TASTE2();

	set_LEDROT();

	InitADC();
	MsTimerInit();
	ED1000Init();
	InitTimer();
	
	BusEigenAdresse = eeprom_read_byte(&BusEigenAdresse_EE) & 0xFE;
	BusEigenAdrMehrfach = 1;
	KommendSperreWahl = eeprom_read_byte(&KommendSperreWahl_EE);

	BefehlEinschalten = false;
	BefehlMark = true;
	MeldungEingeschaltet = false;
	MeldungMark = true;

	KommInit();

// Test der Berechnungsalgorithmen
	EmpfBuf[EmpfBufSchreibI++] = 88;
	EmpfBuf[EmpfBufSchreibI++] = 120;
	EmpfBuf[EmpfBufSchreibI++] = 74;
	EmpfBuf[EmpfBufSchreibI++] = -20;
	EmpfBuf[EmpfBufSchreibI++] = -100;
	EmpfBuf[EmpfBufSchreibI++] = -116;
	EmpfBuf[EmpfBufSchreibI++] = -57;
	EmpfBuf[EmpfBufSchreibI++] = 39;
	EmpfBuf[EmpfBufSchreibI++] = 110;
	EmpfBuf[EmpfBufSchreibI++] = 110;
	ED1000IO();
// Ende Test der Berechnungsalgorithmen */
	
	TMsTimer Timer;
	StartTimer(&Timer);

	ED1000IO();
	
	sei();
	
	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 250)
		;
		
	clr_LEDROT();
	set_LEDGELB();

#ifdef TASTE_NACH_PLUS
	bool SelbsttestAusfuehen = get_TASTE();
#else
	bool SelbsttestAusfuehen = !get_TASTE();
#endif

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
	
	BefehlEinschalten = false;
	MeldungEingeschaltet = false;
	BefehlMark = false;
	MeldungMark = false;

	if (SelbsttestAusfuehen)
		{
		StartTimer(&Timer);
		while (1)
			{
#ifdef TASTE_NACH_PLUS
			if (get_TASTE())
				// Gedrückt = HIGH
#else
			if (!get_TASTE())
				// Gedrückt = LOW
#endif
				{ // gedrückt
				BefehlMark = false;
				}
			else
				{ // nicht gedrückt
				BefehlMark = true;
				StartTimer(&Timer);
				}
				
			ED1000IO();

			bset_LEDROT(BefehlEinschalten);
			bset_LEDGELB(MeldungEingeschaltet);
			bset_LEDGRUEN(BefehlMark);
			bset_LEDBLAU(MeldungMark);

			BefehlEinschalten = MeldungEingeschaltet || (TimerVal(&Timer) > 1000);

			}
		} // if SelbsttestAusfuehren

	BefehlEinschalten = false;
	BefehlMark = true;

	while (true)
		{
		// aktueller Zustand: Ausgeschaltet

		if (TimerVal(&Timer) <= 800)
			clr_LEDROT();
		else if (TimerVal(&Timer) <= 1000)
			bset_LEDROT(BusEigenAdresse == BusAdrUngueltig);
		else
			StartTimer(&Timer);

		clr_LEDGELB();
		clr_LEDGRUEN();
		clr_LEDBLAU();

		TastePruefen();
		ED1000IO();

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
		
		}
	}



