//================================================================
// Fernschreiber-Schnittstelle ED1000 für i-Telex-System
//	für ATmega168 auf Platine ED1000
//================================================================
//				
//  
//================================================================
// verwendete Pins siehe Ports.h
//================================================================

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <inttypes.h>


#include "TwiEvents.h"
#include "Bits.h"
#include "timercs.h"

// #include "build_defs.h"   currently does not work as intended.

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
    EEAdr_KommendSperreWahl = 1,
    EEAdr_Sperrzeit = 2, // Beansprucht 20 Bytes
    EEAdr_TasteFunktion = 22,
    EEAdr_UmleitungAbweisen = 23,
    EEAdr_LokalbetriebWahl = 24,
    EEAdr_AutoWahlZiffern = 25,
	EEAdr_AnrufAbbruchZeit = 35,
	EEAdr_StartQuittVerz = 36,
    EEAdr_MitWaehlscheibe = 37,
    EEAdr_WahlauffordImpulsLaenge = 38,
	EEAdr_TastaturwahlStartZeichen = 39,
	EEAdr_Einschaltschwelle = 40,
	EEAdr_Ende = 41 // darf erhöht werden
};


// Typen
// -----

typedef enum { SperreTaste, SperreStoerung, SperreZeit, SperreWahl } TSperreGrund;

// Konfigurations-Variablen
// ====================
bool MitWaehlscheibe; //!< Gerät het eine Wählscheibe

uint8_t WahlauffordImpulsLaenge; //!< Länge des Wahlaufforderungsimpuls in 1/100 sek

uint8_t TastaturwahlStartZeichen; //!< Welches Zeichen als Start der Tastaturwahl ausgeben? (Standard = 'v' = 15)

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

uint8_t Einschaltschwelle; //!< Bei ausgeschaltetem Fernschreiber ist die Erkennungsschwelle Mark / Space verschoben


// Arbeits-Variablen 
// =======================

bool BefehlEinschalten; //!< Fs soll laufen
bool BefehlMark; //!< Fs Schleifenstrom soll Ein sein
bool MeldungEingeschaltet; //!< Fs läuft tatsächlich
bool MeldungMark; //!< Fs Schleifenstrom ist Ein

TMsTimer AusschaltungTimer; //!< Zählt die Millisekunden von Schleifenunterbrechung bis Ausschaltung
TMsTimer EntprellungTimer; //!< Zählt die Millisekunden von Pegelwechsel am Port bis tatsächlichem Pegelwechsel
TMsTimer NachlaufTimer; //!< Steuert nur den Ausgang für den externen SV-Schalter



// Schnittstellen-Spezifische Variablen
// ====================================

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

bool LedRotEin; 
	//!< rote LED zeigt Status an und auch Übersteuerung. Diese Variable steuert den Zustand entsprechend Status

bool EmpfangMark; //!< ist false, wenn Endgerät ausgeschaltet oder Endgerät Space sendet
bool SpaceSperre; //!< Wird gesetzt, wenn die empfangene Space-Frequenz trotzdem als Mark gewertet werden soll.



// Schnittstellen-Spezifische Funktionen
// =====================================

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


//! Initialisiert die Schnittstelle zum Endgerät.
//-----------------------------------------------
//! Initialisierung des digitalen Filters. Initialisierung der Sinus-Ausgabe.	
static void FernschrInit()
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
static void FernschrIO()
	{
	if (BefehlEinschalten && BefehlMark)
		SinAusgInc = SEND_MARK_FAKTOR;
	else
		SinAusgInc = SEND_SPACE_FAKTOR;
		
	if (BefehlEinschalten && !BefehlMark)
		SpaceSperre = true;
		
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
		bset_LEDROT(LedRotEin || UebersteuerWarnZaehl != 0);
			
		x2 = x1; x1 = x0;
		ym2 = ym1; ym1 = ym0;
		ys2 = ys1; ys1 = ys0;

		// Gleichrichten:
		if (ym0 < 0) ym0 = -ym0;
		if (ys0 < 0) ys0 = -ys0;

		if (ym0 > ys0)
			{
			// Mark über Space
			set_DETECTMARK();
			if (!MeldungEingeschaltet && !BefehlEinschalten && ym0 < ys0 + Einschaltschwelle)
				; // nichts tun
			else if (PegelGlaettZaehl < PEGEL_GLAETT)
				PegelGlaettZaehl++;
			else
				EmpfangMark = true;
			}
		else
			{
			// Space über Mark
			clr_DETECTMARK();
			if (PegelGlaettZaehl > 0)
				PegelGlaettZaehl--;
			else
				EmpfangMark = false;
			}
							
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
		BefehlEinschalten = true;
		BefehlMark = true;
		FernschrIO();
		if (!MeldungEingeschaltet)
			StartTimer(&StabilTimer);
		if (TimerVal(&AbbruchTimer) > AnrufAbbruchZeit * 1000)
			{
			BefehlEinschalten = false;
			BefehlMark = true;
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

	StartTimer(&Timer);
	do
		{
		BefehlEinschalten = false;
		BefehlMark = true;
		FernschrIO();
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
	FernschrIO(); // damit der Fernschreiber abgeschaltet wird.
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
	LedRotEin = true;
	
	if (!FernschrEinschalten(true))
		{ // Timeout...
		clr_LEDGRUEN();
		GeAusschalten(true);
		KommendSperren(SperreStoerung);
		LedRotEin = false;
		return;
		}

	LedRotEin = false;
	
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
	LedRotEin = true;

	Aktivieren(false);
	
	BefehlMark = true;
	
	if (!BefehlEinschalten) // offensichtlich Wählscheiben-Wahl
		{
		FernschrEinschalten(true);
		}
	
	LokalTextAusgabeP(PSTR("\r\nloc\r\n"));

	while(MeldungEingeschaltet)
		{
		FernschrIO();
		}

	FernschrAusschalten();

	Aktivieren(true);
	LedRotEin = false;
	}


/////////////////////////////////////////////////////////////

//! Wird aufgerufen, wenn bei gehender Verbindung zu lange nicht gewählt wird.
//----------------------------------------------
//! \param Abschaltimpuls soll ein Schlusszeichen an das Endgerät gesendet werden?

static void AbschaltungZuLangeWahlpause(bool Abschaltimpuls)
	{
	LedRotEin = true;

	GeAusschalten(true);
	Aktivieren(false);
	
	if (Abschaltimpuls)
		FernschrAusschalten();
	else
		StartTimer(&NachlaufTimer);
	
	while (MeldungEingeschaltet)
		FernschrIO();
	
	LedRotEin = false;

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

	// kurzzeitiger Missbrauch von WahlendeTimer für Wahlaufforderung: 0,3 Sek unterbrechung
	StartTimer(&WahlendeTimer);
	EsWurdeGewaehlt = false;
	while (TimerVal(&WahlendeTimer) < 700)
		FernschrIO();

	BefehlEinschalten = true; // speziell ED1000
	BefehlMark = true;
	
	StartTimer(&WahlendeTimer);
	while (TimerVal(&WahlendeTimer) < 10 * WahlauffordImpulsLaenge) 
		FernschrIO();
		
	BefehlEinschalten = false; // speziell ED1000
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

	// kurzzeitiger Missbrauch von WahlendeTimer für das "Anfangstiming"
	StartTimer(&WahlendeTimer);
	while (TimerVal(&WahlendeTimer) < 100)
		FernschrIO();
	
	if (!FernschrEinschalten(false))
		return false;

	SeriellUmsetzInit();

	BaudotMode_SetZiffern(BaudotMode);

	if (TastaturwahlStartZeichen > 0)
		{
		StartTimer(&WahlendeTimer);
		while (TimerVal(&WahlendeTimer) < 700)
			FernschrIO();

		LokalCodeAusgabe(TastaturwahlStartZeichen);			
		}
			
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
	
	StartTimer(&KennungAbfrageTimer);
	KennungAbfrageZaehler = 0;

	GeSendeMark(true); 

	SendeUmsetzModus = UmsetzFern;
	EmpfUmsetzModus = UmsetzFern; 

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
			&& TimerVal(&KennungAbfrageTimer) >= ((KennungAbfrageZaehler == 0) ? 500 : 5000))
			{
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

	LedRotEin = true;
	
	if (!FernschrEinschalten(true))
		return;
	
	BaudotMode_SetEmpfangen(BaudotMode); // damit auch eine BU-Umschaltung gesendet wird.
	
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR("\r\n configuration ed1000 version " SVNVERSION " date " __DATE__));
#else
	LokalTextAusgabeP(PSTR("\r\n konfiguration ed1000 version " SVNVERSION " datum " __DATE__));
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
		MitWaehlscheibe = false;
		WahlauffordImpulsLaenge = 3;
		TastaturwahlStartZeichen = 0; // keines
		KommendSperreWahl = KommendSperreWahl_Std;
		LokalbetriebWahl = LokalbetriebWahl_Std;
		AnrufAbbruchZeit = AnrufAbbruchZeit_Std;
		StartQuittVerzoegerung = StartQuittVerzoegerung_Std;
		SperrzeitInit();
		TasteFunktion = 0;
		AutoWahlZiffern[0] = 255; // Ende-Kennzeichen
		LokalTextAusgabeP(PSTR("\r\n +++ \r\n\n\n\n"));
		return;
		}

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
		LokalTextAusgabeP(PSTR("\r\n duration dial proceed pulse: cur. "));
		LokalZahlAusgabe(WahlauffordImpulsLaenge, 0);
		LokalTextAusgabeP(PSTR("/100 sec, "));
#else
		LokalTextAusgabeP(PSTR("\r\n laenge wahlauff-imp.: akt. "));
		LokalZahlAusgabe(WahlauffordImpulsLaenge, 0);
		LokalTextAusgabeP(PSTR("/100 sek, "));
#endif //def SPRACHE_EN
		LokalTextAusgabeP(NeuStrP);

		if (LokalZahlEingabe(&WahlauffordImpulsLaenge, 0) < 0)
			return;

		if (WahlauffordImpulsLaenge < 1)
			WahlauffordImpulsLaenge = 1;

		LokalTextAusgabeP(OkStrP);
		}
	else
		{
		// Startcode für Tastaturwahl
#ifdef SPRACHE_EN
		LokalTextAusgabeP(PSTR("\r\n start code for dialling (0 = none, 15 = v): cur. "));
		LokalZahlAusgabe(TastaturwahlStartZeichen, 0);
#else
		LokalTextAusgabeP(PSTR("\r\n start code fuer wahlaufforderung (0 = kein, 15 = v): akt. "));
		LokalZahlAusgabe(TastaturwahlStartZeichen, 0);
#endif //def SPRACHE_EN
		LokalTextAusgabeP(NeuStrP);

		if (LokalZahlEingabe(&TastaturwahlStartZeichen, 0) < 0)
			return;

		if (TastaturwahlStartZeichen > 31)
			TastaturwahlStartZeichen = 31;

		LokalTextAusgabeP(OkStrP);
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
	
	// Einschaltschwelle
	// -----------------
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR("\r\n threshold for switch on (default 0, cur. "));
#else
	LokalTextAusgabeP(PSTR("\r\n einschaltschwelle (normal 0, akt. "));
#endif //def SPRACHE_EN

	LokalZahlAusgabe(Einschaltschwelle, 0);

#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR(") new:   "));
#else
	LokalTextAusgabeP(PSTR(") neu:   "));
#endif //def SPRACHE_EN

	if (LokalZahlEingabe(&Einschaltschwelle, 0) < 0)
		return;

	LokalTextAusgabeP(OkStrP);

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
					i = AutoWahlMaxZiffern - 1; // damit nichts geändet wird
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
		
	// Verzögerung der Rückmeldung des Starts des Fernschreibers
	// ---------------------------------------------------------
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR("\r\n delay confirmation of startup in /10 seconds\r\n (3-200, cur. "));
#else
	LokalTextAusgabeP(PSTR("\r\n verzoegerung rueckmeldung fs-anlauf in /10 sekunden\r\n (3-200, akt. "));
#endif //def SPRACHE_EN

	LokalZahlAusgabe(StartQuittVerzoegerung, 0);

#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR(") new:     "));
#else
	LokalTextAusgabeP(PSTR(") neu:     "));
#endif //def SPRACHE_EN

	if (LokalZahlEingabe(&StartQuittVerzoegerung, 0) < 0)
		return;
	if (StartQuittVerzoegerung < StartQuittVerzoegerung_Min)
		StartQuittVerzoegerung = StartQuittVerzoegerung_Min;
	else if (StartQuittVerzoegerung > 200)
		StartQuittVerzoegerung = 200;

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
	FernschrAusschalten();
	
	KonfigSchreibeByte(EEAdr_BusEigenAdresse, BusEigenAdresse);
	
	KonfigSchreibeByte(EEAdr_UmleitungAbweisen, UmleitungAbweisen);

	KonfigSchreibeBool(EEAdr_MitWaehlscheibe, MitWaehlscheibe);

	KonfigSchreibeByte(EEAdr_WahlauffordImpulsLaenge, WahlauffordImpulsLaenge);

	KonfigSchreibeByte(EEAdr_TastaturwahlStartZeichen, TastaturwahlStartZeichen);

	KonfigSchreibeByte(EEAdr_KommendSperreWahl, KommendSperreWahl);

	KonfigSchreibeByte(EEAdr_LokalbetriebWahl, LokalbetriebWahl);

	KonfigSchreibeByte(EEAdr_Einschaltschwelle, Einschaltschwelle);

	KonfigSchreibeByte(EEAdr_TasteFunktion, TasteFunktion);

	KonfigSchreibeByte(EEAdr_AnrufAbbruchZeit, AnrufAbbruchZeit);

	KonfigSchreibeByte(EEAdr_StartQuittVerz, StartQuittVerzoegerung);
	
	uint8_t i;
	for (i = 0 ; i < AutoWahlMaxZiffern ; i++)
		KonfigSchreibeByte(EEAdr_AutoWahlZiffern + i, AutoWahlZiffern[i]);
	
	SperrzeitSpeicherEeprom(EEAdr_Sperrzeit);

	Aktivieren(true);

	LedRotEin = false;
	}
	
	
/////////////////////////////////////////////////////////////

//! Fehlertext-Ausgabe

static void FehlermeldungDrucken()
	{
	SeriellUmsetzInit();
	Aktivieren(false);
	LedRotEin = true;
	
	if (!FernschrEinschalten(true))
		return;
	
	BaudotMode_SetEmpfangen(BaudotMode); // damit auch eine BU-Umschaltung gesendet wird.
	
	KonfigSpeicherFehlerAusgeben();
	
	FernschrAusschalten();

	Aktivieren(true);

	LedRotEin = false;
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

	LedRotEin = false;

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
	init_SV_EIN();
	init_TASTEEXT();
	init_DETECTMARK();

	//init_TASTE2();

	LedRotEin = true;

	InitADC();

	KonfigSpeicherInit();
	
	MsTimerInit();
	FernschrInit();
	InitTimer();
	
	SperrzeitInit();

	pgm_read_byte(Identifier); // Dummy read to force the identifier to be placed in the FLASH.

	BusEigenAdresse = KonfigLeseByteBegrenzt(EEAdr_BusEigenAdresse, 31 << 1/*Standardwert*/, BusAdrMin, BusAdrMax) & 0xFE; // Bit 0 löschen
	BusEigenAdrMehrfach = 1;
	RundsendEmpfFreig = true;
	
	MitWaehlscheibe = KonfigLeseBool(EEAdr_MitWaehlscheibe, true);
	
	WahlauffordImpulsLaenge = KonfigLeseByteBegrenzt(EEAdr_WahlauffordImpulsLaenge, 3, 1, 100); // ED1000 -> vsl elektronische Maschine

	TastaturwahlStartZeichen = KonfigLeseByteBegrenzt(EEAdr_TastaturwahlStartZeichen, 0, 0, 31); // ED1000 -> vsl elektronische Maschine

	UmleitungAbweisen = KonfigLeseBool(EEAdr_UmleitungAbweisen, false);

	KommendSperreWahl = KonfigLeseByteBegrenzt(EEAdr_KommendSperreWahl, KommendSperreWahl_Std, 0, 99);

	TasteFunktion = KonfigLeseByteBegrenzt(EEAdr_TasteFunktion, 0, 0, 2); // KEIN Bool

	SperrzeitLadeEeprom(EEAdr_Sperrzeit);

	LokalbetriebWahl = KonfigLeseByteBegrenzt(EEAdr_LokalbetriebWahl, LokalbetriebWahl_Std, 0, 99);
	
	StartQuittVerzoegerung = KonfigLeseByteBegrenzt(EEAdr_StartQuittVerz, StartQuittVerzoegerung_Std, StartQuittVerzoegerung_Min, 200);

	Einschaltschwelle = KonfigLeseByteBegrenzt(EEAdr_Einschaltschwelle, 0, 0, 200);

	uint8_t i;
	for (i = 0 ; i < AutoWahlMaxZiffern ; i++)
		AutoWahlZiffern[i] = KonfigLeseByte(EEAdr_AutoWahlZiffern + i, 255); // nicht begrenzt, da alles über 9 das Endezeichen ist.
	
	AnrufAbbruchZeit = KonfigLeseByteBegrenzt(EEAdr_AnrufAbbruchZeit, AnrufAbbruchZeit_Std, 3, 25);
		
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
// Ende Test der Berechnungsalgorithmen */
	
	TMsTimer Timer;
	StartTimer(&Timer);

	sei();

	FernschrIO(); // initialisiert die Ausgabe von "Space", damit der Fs erstmal abschaltet.
	
	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 250)
		;
	
	// Bei Tastendruck Selbsttest
	bool SelbsttestAusfuehen = get_TASTE();

	clr_LEDROT(); 
	LedRotEin = false;

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
			FernschrIO();

			LedRotEin = BefehlEinschalten;
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
			LedRotEin = false;
		else if (TimerVal(&Timer) <= 1400)
			LedRotEin = (BusEigenAdresse == BusAdrUngueltig);
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


