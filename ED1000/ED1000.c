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
#include "LokalUhr.h"
#include "Zeitsperre.h"

#include "WaveTab.h"

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

//#define V21
	// macht andere Frequenzen
	
//! Marker im Code als Identifikation

#ifdef PROGIDZUSATZ

#ifdef V21	
const PROGMEM char Identifier[] = "___itlx_V21-" PROGIDZUSATZ "___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#else
const PROGMEM char Identifier[] = "___itlx_ED1000-" PROGIDZUSATZ "___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#endif 

#else

#ifdef V21	
const PROGMEM char Identifier[] = "___itlx_V21___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#else
const PROGMEM char Identifier[] = "___itlx_ED1000___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#endif 

#endif //def PROGIDZUSATZ

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

EEMEM uint8_t BusEigenAdresse_EE = BusAdrUngueltig; //!< Eigene Busadresse auf dem I²C-Bus
EEMEM uint8_t KommendSperreWahl_EE = 0; //!< Welche Wahlnummer sperrt den Anschluss für ankommende Rufe
EEMEM TSperrzeitDaten Sperrzeit_EE = { 0 };


// Typen
// -----

typedef enum { SperreTaste, SperreStoerung, SperreZeit, SperreWahl } TSperreGrund;


// Variablen
// ---------

static volatile uint8_t SinAusgI; //!< Zeiger auf die Sinus-Ausgabetabelle. Wird im Timerinterrupt inkrementiert.

static volatile uint8_t SinAusgInc; 
	//!< Inkrement des Zeiger auf die Sinus-Ausgabetabelle je Timerinterrupt-Aufruf.
	//!< Bestimmt die Ausgabefrequenz: f = #TIMER1_OCFREQ * #SinAusgInc / Sinus63Len (=128)

#define EMPFBUFSIZE (1<<5)
	//!< Größe des Puffers für ADC-Messwerte. Muss Potenz von 2 sein.

static volatile int8_t EmpfBuf[EMPFBUFSIZE];
	//!< Puffer für ADC-Messwerte. 

static volatile uint8_t EmpfBufSchreibI;
	//!< Index für das Eintragen von Werten in #EmpfBuf.

static uint8_t EmpfBufLeseI;
	//!< Index für das Auslesen von Werten aus #EmpfBuf.


//! Initialisiert Zeitgeber.
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


//! Initialisiert ADC-Wandler.
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

	
//! Interrupt-Routine für TIMER1. 
//-------------------------------
//! Wird mit der Frequenz #TIMER1_OCFREQ aufgerufen.
//! Schreibt ADC-Werte in den Puffer und startet den ADC neu.
//! Errechnet neuen Index in Sinus-Ausgabetabelle und gibt ermittelten Tabelleneintrag 
//! auf der Schnittstelle aus (R2R-Netzwerk).
ISR(TIMER1_COMPA_vect)
	{
	// letztes AD-Ergebnis retten
	EmpfBuf[EmpfBufSchreibI++] = ADCH - 128;
	EmpfBufSchreibI &= (EMPFBUFSIZE-1);

	SinAusgI += SinAusgInc;
	SinAusgI &= (Sinus63Len-1);

	PORTB = pgm_read_byte(&Sinus63[SinAusgI]);

	SET_BIT(ADCSRA,	ADSC); // AD-Wandler starten (dabei wird ADIF mit gelöscht!)
 
	}


bool BefehlEinschalten; //!< Fs soll laufen
bool BefehlMark; //!< Fs Schleifenstrom soll Ein sein
bool MeldungEingeschaltet; //!< Fs läuft tatsächlich
bool MeldungMark; //!< Fs Schleifenstrom ist Ein

bool EmpfangMark; //!< ist false, wenn Endgerät ausgeschaltet oder Endgerät Space sendet
bool SpaceSperre; //!< Wird gesetzt, wenn die empfangene Space-Frequenz trotzdem als Mark gewertet werden soll.

int8_t x0, x1, x2, ym0, ym1, ym2, ys0, ys1, ys2; 
	//!< Alles Zwischenwerte für den digitalen Filter

uint8_t PegelGlaettZaehl;
	//!< Zähler zur Filterung von kurzen Mark-Space-Wechseln.

#define PEGEL_GLAETT 50
	//!< Grenzwert für #PegelGlaettZaehl.
	//!< Verursachte Verzögerung: PEGEL_GLAETT / TIMER1_OCFREQ, also 4 ms.

uint16_t EinAusschaltZaehl;
	//!< Zähler für die Ermittlung von Ein- und Ausschaltungen. Dies sind
	//!< langandauernde Wechsel der Empfangsfrequenz.

#define EINSCHALT_VERZ (TIMER1_OCFREQ / 10) 
	//!< Grenzwert für Einschaltung bei EinAusschaltZaehl. 1/10 sek. Mark-Frequenz = ein.
	
#define AUSSCHALT_VERZ (TIMER1_OCFREQ / 2) 
	//!< Grenzwert für Ausschaltung bei EinAusschaltZaehl. 1/2 sek. Space = aus.

#define UEBERSTEUER_GRENZE 90
	//!< Grenzwert der Aussteuerung (maximal möglich 127) für das Ansprechen der
	//!< roten LED bei Überlauf der digitalen Filterberechnung.

uint8_t UebersteuerWarnZaehl;
	//!< Zähler für die Verlängerung des Leuchtens der roten LED bei drohendem 
	//!< Überlauf der digitalen Filterberechnung.

#define UEBERSTEUER_ZAEHLMAX 40
	//!< Grenzwert für UebersteuerWarnZaehl. Ein Überschreiten der Amplitude (#UEBERSTEUER_GRENZE)
	//!< lässt rote LED 40 Zyklen leuchten (4 ms).

//! Initialisiert die Schnittstelle zum Endgerät.
//-----------------------------------------------
//! Initialisierung des digitalen Filters. Initialisierung der Sinus-Ausgabe.	
static void ED1000Init()
	{
	x0 = x1 = x2 = ym0 = ym1 = ym2 = ys0 = ys1 = ys2 = 0;
	EmpfangMark = false;
	BefehlEinschalten = false;
	MeldungEingeschaltet = false;
	BefehlMark = true;
	MeldungMark = true;
	}
	
	
///////////////////////////////////////////////////////////////////////////////

//! bedient Hardware-IO entsprechend der aktuellen Zustände.
//----------------------------------------------------------
//! setzt die Ausgabefrequenz entsprechend BefehlEinschalten und BefehlMark,
//! setzt MeldungEingeschaltet und MeldungMark entsprechend der empfangenen Frequenz,
//! steuert die Status-LEDs
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
		// SPACE: 
		// Einstellungen: Samplerate = 12800, IIR Bandpass Butterworth  f1 = 2150, f2 = 2650, Order = 1, 8 Bit
		// Ergebnis aus generiertem C-Code: 
		//		__int8 ACoef[NCoef+1] = {  67,   0, -67 };
		//		__int8 BCoef[NCoef+1] = { 128, -87,  99	};
		// --> ys0 = (67 * (x0 - x2) + 87 * ys1 - 99 * ys2) / 128;
		//           A0/A2            -B1        -B2           B0
		// Zur Vermeidung von Überlauf bei 8-Bit-Berechnungen alle A-Koeffizienten / 2
		// --> ys0 = (33 * (x0 - x2) + 87 * ys1 - 99 * ys2) / 128;
		//           A0/A2            -B1        -B2           B0
		int16_t h = (33 * (x0 - x2) + 87 * ys1 - 99 * ys2);
		ys0 = h >> 7;

		// MARK: 
		// Einstellungen: Samplerate = 12800, IIR Bandpass Butterworth  f1 = 3150, f2 = 3600, Order = 1, 8 Bit
		// Ergebnis aus generiertem C-Code: 
		//		__int8 ACoef[NCoef+1] = {  80,   0, -80 };
		//		__int8 BCoef[NCoef+1] = { 128,  19, 102 };
		// --> ym0 = (80 * (x0 - x2) - 19 * ym1 - 102 * ym2) / 128;
		//           A0/A2            -B1        -B2           B0
		// Zur Vermeidung von Überlauf bei 8-Bit-Berechnungen alle A-Koeffizienten / 2
		// --> ym0 = (40 * (x0 - x2) - 19 * ym1 - 102 * ym2) / 128;
		//           A0/A2            -B1        -B2           B0
		h = (40 * (x0 - x2) - 19 * ym1 - 102 * ym2);
		ym0 = h >> 7;
		
#endif //ndef V21

		if (ys0 > UEBERSTEUER_GRENZE || ys0 < -UEBERSTEUER_GRENZE 
			|| ym0 > UEBERSTEUER_GRENZE || ym0 < -UEBERSTEUER_GRENZE)
			UebersteuerWarnZaehl = UEBERSTEUER_ZAEHLMAX;
		else if (UebersteuerWarnZaehl > 0)
			UebersteuerWarnZaehl--;
		bset_LEDROT(UebersteuerWarnZaehl != 0);
			
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

	
uint8_t KommendSperreWahl; //!< Welche Wahlnummer sperrt den Anschluss für ankommende Rufe

char BuZiMode; //!< Marker für Buchstaben-Ziffern-Umschaltung.


//////////////////////////////////////////////////////////////////

//! Einschaltung des Fs auslösen.
//-------------------------------
//! \returns Einschaltung wurde erfolgreich durch Endgerät quittiert.

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
		

//////////////////////////////////////////////////////////////////

//! Ausschaltung des Fs auslösen.

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
		ED1000IO();
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
	
	if (c >= 'A' && c <= 'Z')
		c += 'a'-'A';
	if (!ZeichenZuCode2(c, &BuZiMode, &code1, &code2))
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
	ED1000IO();

	set_LEDGRUEN();
	set_LEDROT();
	
	if (!ED1000Einschalten())
		{ // Timeout...
		clr_LEDGRUEN();
		KommendSperren(SperreStoerung);
		clr_LEDROT();
		return;
		}

	clr_LEDROT();
	
	if (GeEinschalten() != GeEinschAnrufquitt)
		{
		GeAusschalten(true);
		ED1000Ausschalten(true);
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
	GeAusschalten(true);
	Aktivieren(false);
	if (Abschaltimpuls)
		ED1000Ausschalten();
	while (MeldungEingeschaltet)
		ED1000IO();
	clr_LEDROT();
	Aktivieren(true);
	}
	
	
/////////////////////////////////////////////////////////////

//! Wickelt die gehende Wahl ab. 
//----------------------------------------------
//! Tastaturwahl. 
//! \retval true bei erfolgreichem Verbindungsaufbau.

static bool WahlMitTastatur()
	{
	char c;
	TMsTimer WahlendeTimer;
	bool EsWurdeGewaehlt;
	int Falschziffern;
	bool Lokalbetrieb;
	
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
			else if (c == 'l' && !EsWurdeGewaehlt)
				{
				Lokalbetrieb = true;
				LokalZeichenAusgabe('o');
				LokalZeichenAusgabe('c');
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

		if (!MeldungEingeschaltet || KoAusschalten())
			{
			// ED1000Ausschalten() macht die aufrufende Routine
			return false;
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
		ED1000Ausschalten();
		return;
		}

	SperrzeitAussetzen();
	
	set_LEDGELB();
	
	switch (GeEinschalten())
		{ // hier nur break benutzen, wenn Einschaltung erfolgreich
		case GeEinschFehler:
			return; 

		case GeEinschWahl:
			if (WahlMitTastatur())
				// Einschalten ist nicht erforderlich, da schon eingeschaltet ist...
				break; // ist jetzt verbunden

			GeAusschalten(true);
			ED1000Ausschalten();

			
			if (KommendSperreWahl != 0 && LetzteInterneWahl() == KommendSperreWahl)
				{
				clr_LEDGELB();
				KommendSperren(SperreWahl);
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

	VerbindungSteht(true); // mit automatischer Kennungsgeber-Abfrage

	SperrzeitAussetzen(); // am Ende nochmal das Flag setzen.
	
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
		ED1000IO();
		TastePruefen();

		if (!MeldungEingeschaltet)
			{
			GeAusschalten(false);
			ED1000Ausschalten();
			while (!KoAusschalten())
				ED1000IO();
			return;
			}

		if (KoAusschalten())
			{
			ED1000Ausschalten();
			GeAusschalten(false);
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

//! Behandelt die Selbstkonfiguration des Moduls.
//-----------------------------------------------
//! Arbeitet mit dem angeschlossenen Endgerät zusammen.

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

	if (BusEigenAdresse != eeprom_read_byte(&BusEigenAdresse_EE))
		eeprom_write_byte(&BusEigenAdresse_EE, BusEigenAdresse);
	
	// Einschaltung der Sperre für kommende Rufe durch Wahl von...
	LokalTextAusgabeP(PSTR("\r\n kommend-sperre mit wahl: (akt. "));
	if (KommendSperreWahl != 0)
		LokalZahlAusgabe(KommendSperreWahl, 2);
	else
		LokalTextAusgabeP(PSTR("nein"));
	LokalTextAusgabeP(PSTR(") neu (0 = nein):     "));

	if (LokalZahlEingabe(&KommendSperreWahl, 2) < 0)
		return;

	if (KommendSperreWahl != eeprom_read_byte(&KommendSperreWahl_EE))
		eeprom_write_byte(&KommendSperreWahl_EE, KommendSperreWahl);

	LokalTextAusgabeP(OkStrP);
	
	if (!SperrzeitEingabeDialog())
		return;
	
	SperrzeitSpeicherEeprom(&Sperrzeit_EE);
	
	// weitere Eingaben

	LokalTextAusgabeP(PSTR("\r\n +++ \r\n"));
	}


/////////////////////////////////////////////////////////////

//! Beendet die Selbstkonfiguration des Moduls.
//-----------------------------------------------
//! Arbeitet mit dem angeschlossenen Endgerät zusammen.

static void KonfigurationEnde()
	{
	ED1000Ausschalten();
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
			
		ED1000IO();
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

//! Das Hauptprogramm der ED1000-Fernschreiber-Schnittstelle.
//----------------------------------------------------------

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
	
	SperrzeitInit();
	
	BusEigenAdresse = eeprom_read_byte(&BusEigenAdresse_EE) & 0xFE;
	if (BusEigenAdresse < BusAdrMin || BusEigenAdresse > BusAdrMax)
		BusEigenAdresse = 51 << 1; // Standardwert
	BusEigenAdrMehrfach = 1;
	RundsendEmpfFreig = true;
	
	KommendSperreWahl = eeprom_read_byte(&KommendSperreWahl_EE);
	if (KommendSperreWahl > 99)
		KommendSperreWahl = 0;

	SperrzeitLadeEeprom(&Sperrzeit_EE);
	
	BefehlEinschalten = false;
	BefehlMark = true;
	MeldungEingeschaltet = false;
	MeldungMark = true;

	KommInit();

/*/ Test der Berechnungsalgorithmen
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

	sei();
	
	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 250)
		;
		
	// Bei Tastendruck Selbsttest
#ifdef TASTE_NACH_PLUS
	bool SelbsttestAusfuehen = get_TASTE();
#else
	bool SelbsttestAusfuehen = !get_TASTE();
#endif

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
			Deaktivieren();
			}

		if (MeldungEingeschaltet)
			{
			VerbindungGehend();
			}
			
		if (KoEinschalten())
			{
			VerbindungKommend();
			}

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
		
			
		} // while (1)
	} // main()


