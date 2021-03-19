//================================================================
// Schnittstelle für i-Telex-System für Hellschreiber (z.B. Hell GL72)
// für ATmega168 auf Platine Seriell+Spezial
// Teil Signalverarbeitung (auch Stand-Alone nutzbar)
//================================================================

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>

#include "defports.h"
#include "timercs.h"
#include "bits.h"
#include "TwiEvents.h"

#include "FifoPuffer.h"
#include "MsTimer.h"

#include "HellCodes.h"
#include "PortsSignal.h"

#include "../SvnVersion.h"


#define HELL_FONT_CHAR_SIZE 6 // 6 Einträge in der Tabelle je Zeichen
#define HELL_FONT_TAB_START 10 // Linefeed
#define HELL_FONT_TAB_END 127

#define HELL_STARTCODE 0x0FF0 // Startimpuls
#define HELL_PIXEL_PRO_REIHE 14 // 14 Pixel übereinander
#define HELL_REIHEN_PRO_ZEICHEN 7 // 7 Pixel nebeneinander (inkl. "Startbits")

#define HELL_START_LAENGE 8 // Start-Zeichen ist 8 Bit lang
#define HELL_START_OFFSET 10 // 10 Bit nach dem Beginn des Startbits kommt das Bildzeichen

#define HELL_PIXELTAKT 600UL // 600 Pixel pro Sekunde, aber mind. 2 Pixel Schwarz oder Weiß

#define HELL_TON 3000UL // 3 kHz Sendefrequenz

#define HELL_ANRUF_FREQ 25 // 50 Hz (andere Werte mal testen)

#define HELL_TON_SCHWELLE 4 // 4 Messungen der halben Periodendauer müssen zur Sollfrequenz passen


// =========================================
// Typen
// =========================================

// noch keine


// ==================================================================
// globale Variablen fuer Hellschreiber-HW-Schnittstelle
// ==================================================================

volatile bool HellTonEmpfEin; //!< true, wenn gerade ein Hellton empfangen wird

volatile uint8_t HellTonEPZaehl; //!< zählt die Halbwellen mit korrekter Dauer

#define MESSBYTES_PRO_ZEICHEN 10 //!< eigentlich sind es 6*14 = 84 Bits. Da die letzte Spalte immer frei ist, wird auf die letzten 4 Bits verzichtet.

#define MESSUNG_ANZAHL_ZEICHEN 8 //!< Damit folgende Messungen laufen können, während die vorherige noch ausgewertet wird.


volatile uint8_t HellMessung[MESSBYTES_PRO_ZEICHEN * MESSUNG_ANZAHL_ZEICHEN];
// Enthält die Aufnahme des aktuellen Zeichens in 'natürlicher' Reihenfolge (MSB zuerst)
// Ringpuffer für bis zu MESSUNG_ANZAHL_ZEICHEN Aufzeichnungen.

volatile uint8_t HellMessPhase; // 0 = Ruhe, 1 = Startbits, 2 bis 11 = Zeichen

volatile uint8_t HellMessSchreibIndex; // index in HellMessung

volatile uint8_t HellMessBit; // im Startbereich ein Zähler, sonst eine Bitmaske

volatile uint16_t HellMessPeriode; // Aktueller Wert der PIXEL-Dauer in Takten des Counter 2

uint8_t HellAuswertIndex;


TPuffer SerOutBuf; //!< Sendepuffer für die serielle Schnittstelle. 
TPuffer TwiOutBuf; //!< Sendepuffer für die TWI Schnittstelle. 
TPuffer HellAusgZeichenPuffer; //!< Puffer für empfangene Zeichen / Befehle. Wird auch von TWI benutzt.


uint8_t HellStatus;
	// Bitmaske, siehe Bit-Definitionen HellStatBit... in HellCodes.h


bool WatchdogAktiv;


#ifdef PROGIDZUSATZ
//! Identifikation im Programmspeicher
const PROGMEM char Identifier[] = "___itlx_HellschrSignal-" PROGIDZUSATZ "___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#else
//! Identifikation im Programmspeicher
const PROGMEM char Identifier[] = "___itlx_HellschrSignal___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#endif


// ================================================
// Timer 0
// ================================================
// hat mehrere Aufgaben, aber jeweils 'exklusiv':
// - Messung der Periodendauer beim Auswerten von Hell-Signalen
// - PWM-Ausgabe für den Hell-Ton beim beim Senden von Hell-Zeichen (über OC0B)

// Für Messung der Periodendauer: Timer darf überlaufen nach 3 ms
// -> Takt < 256 / 3 ms = 85,3 kHz
// -> Vorteiler > 14745,6 kHz / 85,3 kHz
// -> Vorteiler > 172 -> Vorteiler = 256
// -> Tatsächlicher Messtakt 57,6 kHz bzw. 17,3 µs

#define TIMER0_VORTEILER_MESS 256UL
#define TIMER0_CS_MESS TCCR_DIV(0, 256)
#define TIMER0_FREQ_MESS (F_CPU / TIMER0_VORTEILER_MESS)

#define TIMER0_MESS_SOLLPERIODE (TIMER0_FREQ_MESS / HELL_TON)

#define TIMER0_MESS_LIMIT (TIMER0_MESS_SOLLPERIODE * HELL_TON_SCHWELLE / 2 + 2)

// Für PWM-Ausgabe für den Hell-Ton: Soll-Frequenz = HELL_TON = 3 kHz
// -> Takt < 256 * 3 kHz = 768 kHz
// -> Vorteiler > 14745,6 kHz / 768 kHz
// -> Vorteiler > 19.2 -> Vorteiler = 64
// -> TOP fuer PWM = 14745,6 kHz / 64 / 3 kHz = 77
// -> Tatsächliche Frequenz = 2992 Hz

#define TIMER0_VORTEILER_TONAUSG 64UL
#define TIMER0_CS_TONAUSG TCCR_DIV(0, 64)
#define TIMER0_FREQ_TONAUSG (F_CPU / TIMER0_VORTEILER_TONAUSG)

#define TIMER0_TOP_TONAUSG (TIMER0_FREQ_TONAUSG / HELL_TON + 1)


// ================================================
// Timer 1
// ================================================
// hat mehrere Aufgaben, aber jeweils 'exklusiv':
// - Zeittakt für das Messen des Bitmusters beim Lesen von Hell-Signalen
// - Zeittakt für das Generieren des Bitmusters beim Senden von Hell-Zeichen

// Für Messung des Bitmusters: OC1A wird verwendet, muss genau (1 / HELL_PIXELTAKT) sein
// -> Zähltakt < 65536 * 600 Hz = 39,3 MHz
// -> Vorteiler = 1
// Ebenso für Generierung des Bitmusters beim Senden

#define TIMER1_VORTEILER_MESS 1
#define TIMER1_CS_MESS TCCR_DIV(1, 1)
#define TIMER1_FREQ_MESS (F_CPU / TIMER1_VORTEILER_MESS)

#define TIMER1_MESS_OV_NENNW (TIMER1_FREQ_MESS / HELL_PIXELTAKT - 1)


// Timer 2 ist durch MsTimer.h belegt
// ----------------------------------

// =====================================================
// Funktionen fuer Hellschreiber-Schnittstelle
// =====================================================

ISR(ANALOG_COMP_vect)
// wird bei jedem Nulldurchgang des Hellton-Signals aufgerufen
	{
	// eine kleine Hysterese wird mit einer Mittkopplung erzwungen
	bset_HellTonGegenkopplung(BIT_IS_SET(ACSR, ACO)); // geht auf AIN0 das ist der positive Eingang des Komperators

	uint8_t aktTCNT = TCNT0;
	if ((aktTCNT >= TIMER0_MESS_SOLLPERIODE * 9/10 / 2 && aktTCNT <= TIMER0_MESS_SOLLPERIODE * 11/10 / 2 + 1) // eine Halbwelle
		|| (aktTCNT >= TIMER0_MESS_SOLLPERIODE * 9/10 && aktTCNT <= TIMER0_MESS_SOLLPERIODE * 11/10 + 1)) // eine Vollwelle
		{ // gut
		TCNT0 = 0; // neu starten
		if (HellTonEPZaehl < HELL_TON_SCHWELLE)
			HellTonEPZaehl++;
		else
			{
			// seto_LEDblau();
			if (HellMessPhase == 0)
				{
				TCNT1 = HellMessPeriode / 2; 
					// Timer für Einzelbits rücksetzen, dabei ein halbes Bit weglassen, damit in der Mitte der Bits "abgetastet" wird.
				HellMessPhase = 1;
				HellMessBit = HELL_START_OFFSET; 
				HellTonEmpfEin = true;
				SET_BIT(HellStatus, HellStatBitEmpfTon);
				set_DiagB();
				}
			else if (!HellTonEmpfEin)
				{ // gerade eingeschaltet.
				if (TCNT1 >= HellMessPeriode / 2)
					{
					if ((TCNT1 - HellMessPeriode / 2) > HellMessPeriode / 8)
						{
						SET_BIT(HellStatus, HellStatBitEmpfStoer);
						// TODO Fehlerart speichern
						}
					HellMessPeriode++;
					}
				else
					{
					if ((HellMessPeriode / 2 - TCNT1) > HellMessPeriode / 8)
						{
						SET_BIT(HellStatus, HellStatBitEmpfStoer);
						// TODO Fehlerart speichern
						}
					HellMessPeriode--;
					}
				OCR1A = HellMessPeriode;
				HellTonEmpfEin = true;
				SET_BIT(HellStatus, HellStatBitEmpfTon);
				}
			}
		}
	else
		{ // schlecht
		if (HellTonEPZaehl == 0)
			TCNT0 = 0;
		// else ignorieren. Falls dauerhaft keine ordentliche Periodendauer, schaltet der Overflow-Interrupt von Timer 2 die Erkennung aus.
		}
	}


ISR(TIMER0_COMPA_vect)
// wird aufgerufen, wenn Timer0 überläuft, weil der Hell-Ton wieder aufgehört hat.
	{
	HellTonEmpfEin = false;
	HellTonEPZaehl = 0;
	// inp_LEDblau();
	CLR_BIT(HellStatus, HellStatBitEmpfTon);
	}	


ISR(TIMER1_COMPA_vect)
// wird aufgerufen, wenn Timer1 überläuft, dann ist die Mitte eines Bits erreicht
	{
	if (HellMessPhase == 0)
		return; // nix
	else if (HellMessPhase == 1)
		{ // in Start-Bits
		HellMessBit--;
		if (HellMessBit > HELL_START_OFFSET / 2 && !HellTonEmpfEin)
			{
			HellMessPhase = 0; // Startbit-Phase zu kurz
			SET_BIT(HellStatus, HellStatBitEmpfStoer);
			// TODO noch Fehlerart Speichern
			clr_DiagB();
			}
		else if (HellMessBit == 0)
			{ // Startbits beendet
			HellMessPhase = 2;
			HellMessBit = (1 << 7);
			HellMessung[HellMessSchreibIndex] = 0;
			}
		}
	else
		{ // in Zeichen-Bits
		if (HellTonEmpfEin)
			HellMessung[HellMessSchreibIndex] |= HellMessBit;
		HellMessBit >>= 1;
		if (HellMessBit == 0)
			{ // Ende des Bytes
			if (HellMessPhase < 1 + MESSBYTES_PRO_ZEICHEN)
				{ // weitere Messungen folgen
				HellMessPhase++;
				HellMessSchreibIndex++;
				HellMessung[HellMessSchreibIndex] = 0;
				HellMessBit = (1 << 7);
				}
			else // Messung beendet.				
				{
				HellMessPhase = 0;
				HellMessSchreibIndex++;
				if (HellMessSchreibIndex >= MESSBYTES_PRO_ZEICHEN * MESSUNG_ANZAHL_ZEICHEN)
					HellMessSchreibIndex = 0; // Ringpuffer
				clr_DiagB();
				}
			} // if HellMessBit == 0
		} // else in Zeichen-Bits
	} // ISR(TIMER1_COMPA_vect)



void HellTonEmpfEinschalten()
	{
	CLR_BIT(ADCSRB, ACME);
	ACSR = (0 << ACD) | (0 << ACBG) | (1 << ACIE) | (0 << ACIC) | (0 << ACIS1) | (0 << ACIS0);
		// Analog-Komperator interrupt aktiv auf beide Flanken.
		
	OCR1A = HellMessPeriode;
	TIFR1 = (1 << ICF1) | (1 << OCF1B) | (1 << OCF1A) | (1 << TOV1); // alle bestehenden Overflow Flags löschen
	TCCR1A = (0 << COM1A0) | (0 << COM1B0) | (0 < WGM10);
	TCCR1B = (0 << ICNC1) | (0 << ICES1) | (0 << WGM13) | (1 << WGM12) | TIMER1_CS_MESS;
	SET_BIT(TIMSK1, OCIE1A);
	
	OCR0A = TIMER0_MESS_LIMIT;
	TIFR0 = (1 << OCF0B) | (1 << OCF0A) | (1 << TOV0); // alle bestehenden Overflow Flags löschen
	TCCR0A = (0 << COM0A0) | (0 << COM0B0) | (0 << WGM00); // non PWM
	TCCR0B = (0 << WGM02) | TIMER0_CS_MESS;
	SET_BIT(TIMSK0, OCIE0A);
	}


void HellTonEmpfAusschalten()
	{
	ACSR = (0 << ACD) | (0 << ACBG) | (0 << ACIE) | (0 << ACIC) | (0 << ACIS1) | (0 << ACIS0);
	SET_BIT(ACSR, ACI); // clear interrupt flag in any case.
	CLR_BIT(TIMSK1, OCIE1A);
	CLR_BIT(TIMSK0, OCIE0A);
	}


// =====================================================
// Auswertung der empfangenen Bitmuster (Pixelbild des Zeichens)
// =====================================================


const int8_t WertungTab[] PROGMEM = {
	#include "WertungsDaten.h"
0 };



int16_t MessungVergleichSchieb;

int16_t MessungVergleichen(char Zeichen)
	{
	uint8_t messw; // aktueller Messwert-Byte
	uint8_t messbitz; // zählt die nicht ausgewerteten Bits im messw;
	uint8_t messindex;
	uint16_t refw; // aktuelle Bitfolge in der Referenztabelle
	uint8_t refbitz;
	uint8_t refindex;
	uint16_t refbasis;
	uint8_t bitkombi; // enthält die Bitfolge 00AaBbCc, wobei
	int16_t punkte;

	messindex = HellAuswertIndex;
	messbitz = 0;
	messw = 0;
	refindex = 0;
	refbasis = HELL_FONT_CHAR_SIZE * (Zeichen - HELL_FONT_TAB_START);
	refbitz = 0;
	punkte = 0;
	MessungVergleichSchieb = 0;
	bitkombi = 0;
	refw = 0;

	while (punkte >= -20) // punkte < -10 ist ein ungewöhnlicher Abbruch. Regulär ist das break etwas weiter unten.
		{
		if (refbitz == 0)
			{
			if (refindex == HELL_FONT_CHAR_SIZE - 1) // die letzte Spalte ist immer leer
				break;
			refw = pgm_read_word(&(HellFontTab[refbasis + refindex]));
			refindex++;
			refbitz = HELL_PIXEL_PRO_REIHE;
			}
		if (messbitz == 0)
			{
			if (messindex > HellAuswertIndex + MESSBYTES_PRO_ZEICHEN)
				break; // nichts mehr da an daten.
			messw = HellMessung[messindex];
			messindex++;
			messbitz = 8;
			}
		bitkombi = (bitkombi & 0xF) << 2;
		if ((refw & (1 << 15)) != 0)
			bitkombi |= (1 << 1);
		refw <<= 1;
		refbitz--;
		if ((messw & (1 << 7)) != 0)
			bitkombi |= (1 << 0);
		messw <<= 1;
		messbitz--;

		punkte += (int8_t) pgm_read_byte(&(WertungTab[2 * bitkombi + 0]));
		MessungVergleichSchieb += (int8_t) pgm_read_byte(&(WertungTab[2 * bitkombi + 1]));
		}
	return punkte;
	}


// Simulation
// ==========
// wird nur zur Prüfung der Machbarkeit verwendet (ausreichende Rechenzeit)
/*
void MessungSimulieren(char Zeichen, int Stoergrad)
// Stoergrad = Kehrwert der Wahrscheinlichkeit für gekippte Bits.
{
	uint8_t messw; // Schieberegister für Messwerte
	uint8_t messbitz; // zählt in messw 'eingeschobenen' Bits
	uint8_t messindex;
	uint16_t refw; // Schieberegister aus der Referenztabelle
	uint8_t refbitz; // Anzahl gültiger Bits in refw
	uint8_t refindex;
	uint16_t refbasis;

	messindex = 0;
	messbitz = 0;
	messw = 0;
	refindex = 0;
	refbitz = 0;
	refw = 0;
	refbasis = HELL_FONT_CHAR_SIZE * (Zeichen - HELL_FONT_TAB_START);

	while (1)
	{
		if (refbitz == 0 && refindex < HELL_FONT_CHAR_SIZE)
		{
			refw = pgm_read_word(&(HellFontTab[refbasis + refindex]));
			refindex++;
			refbitz = HELL_PIXEL_PRO_REIHE;
		}

		messw <<= 1;
		if ((refw & (1 << 15)) != 0)
			messw |= 1;
		//if (rand() % Stoergrad == 0)
		//	messw ^= 1;
		refw <<= 1;
		refbitz--;
		messbitz++;

		if (messbitz == 8)
		{
			HellMessung[messindex] = messw;
			messindex++;
			messbitz = 0;
			if (messindex >= MESSBYTES_PRO_ZEICHEN)
				break;
		}
	}
}

*/


// =================================================================
// Schnittstelle zur Anwendung
// =================================================================


#ifdef TESTSER

//! Initialisiert serielle Schnittstelle und Puffer dazu.
static void SerIOInit()
{
	// PORTS initialisieren (Ausgabepins)
	// Serielle Schnittstelle initialisieren
	#ifndef SERBAUD
	#define BAUD 9600
	#else
	#define BAUD SERBAUD
	#endif //ndef SERBAUD

	#include <util/setbaud.h>
	UBRR0H = UBRRH_VALUE;
	UBRR0L = UBRRL_VALUE;

	#if USE_2X
	UCSR0A = (1 << U2X0);
	#else
	UCSR0A = (0 << U2X0);
	#endif

	UCSR0B = (1<<TXEN0)+(1<<RXEN0)+(0<<RXCIE0)+(0<<UCSZ02);

	#ifdef SER7BIT
	UCSR0C = (0<<UMSEL01)+(0<<UMSEL00)+(0<<UPM00)+(0<<UPM01)+(1<<USBS0)+(1<<UCSZ01)+(0<<UCSZ00);
	#else
	UCSR0C = (0<<UMSEL01)+(0<<UMSEL00)+(0<<UPM00)+(0<<UPM01)+(0<<USBS0)+(1<<UCSZ01)+(1<<UCSZ00);
	#endif
}


void SerAusg(char c)
{
	PufferSpeich(&SerOutBuf, c);
}


void SerAusgZahl(int i)
{
	if (i < 0)
	{
		SerAusg('-');
		SerAusgZahl(-i);
	}
	else
	{
		if (i >= 10)
		{
			int z = i / 10;
			SerAusgZahl(z);
			i -= 10 * z;
		}
		SerAusg('0' + i);
	}
}


void SerAusgPStr(const char *s)
{
	char c;
	while ((c = pgm_read_byte(s)) != '\0')
	{
		SerAusg(c);
		s++;
	}
}


#else //ndef TESTSER

#endif //ndef TESTSER


// Regelbetrieb über TWI als Slave

//! Interrupt-Routine für TWI. 
//----------------------------
//! Verarbeitet Statusänderungen des Atmel-TWI-Interface.
//! Diese Variante nur fuer Slave-Betrieb

void PollTwi()
	{
	uint8_t NewStat;

	set_DiagC();

#ifdef TESTSER
	// nebenbei wird auch Serielle Ein/Ausgabe bearbeitet
	if (!PufferLeer(&SerOutBuf) && BIT_IS_SET(UCSR0A, UDRE0))
		{
		UDR0 = PufferAusg(&SerOutBuf);
		}

	if (BIT_IS_SET(UCSR0A, RXC0))
		{
		uint8_t c;
		c = UDR0;
		PufferSpeich(&HellAusgZeichenPuffer, c);
		PufferSpeich(&SerOutBuf, c); // falls Echo gewünscht
		// PufferSpeich(&TwiOutBuf, c);
		WatchdogAktiv = false;
		wdt_disable();
		}

#endif //def TESTSER

	if (!BIT_IS_SET(TWCR, TWINT))
		{
		clr_DiagC();
		return;
		}

	set_DiagD();
	clr_DiagE();

	NewStat = (0<<TWINT) | (1<<TWEA) | (0<<TWSTA) | (0<<TWSTO) | (1<<TWEN) | (0<<TWIE);
		// TWEA standardmäßig gesetzt, muss ggf. wieder gelöscht werden.

	switch (TWSR & TwiEv_Mask)
		{
		// Allgemein
		// ---------
        case TwiEv_BusError			:
			SET_BIT(NewStat, TWSTO);
			break;

		// Slave-Receive
		// -------------
		case TwiEv_SR_AddrACK		:
			wdt_reset();
			// CLR_BIT(NewStat, TWEA); // damit nach erstem Datenbyte NACK gesendet wird   TODO prüfen ob es auch ohne geht.
			break;

        case TwiEv_SR_DataACK		:
        case TwiEv_SR_DataNACK		:
			PufferSpeich(&HellAusgZeichenPuffer, TWDR);
			if (PufferVoll(&HellAusgZeichenPuffer))
				SET_BIT(HellStatus, HellStatBitPufferVoll);
#ifdef TESTSER
			PufferSpeich(&SerOutBuf, '>');
			PufferSpeich(&SerOutBuf, TWDR);
#endif //def TESTSER
			// CLR_BIT(NewStat, TWEA); // damit weiter NACK gesendet wird  TODO prüfen ob es auch ohne geht.
			break;

        case TwiEv_SR_Stop			: // dies wird auch beim GeneralCall aufgerufen
			break;

		// Slave-Transmit (kann kein Rundsenden sein)
		// --------------
        case TwiEv_ST_AddrACK		:
        case TwiEv_ST_DataACK		:
        //case TwiEv_ST_DataNACK		:
        //case TwiEv_ST_DataLast		:
			wdt_reset();
			set_DiagE();
			if (PufferLeer(&TwiOutBuf))
				{
				TWDR = HellStatus | (1 << HellStatBitStatFlag);
				CLR_BIT(HellStatus, HellStatBitEmpfStoer);
				}
			else
				TWDR = PufferAusg(&TwiOutBuf);
			break;

		default:
			break;

		}
		
	TWCR = NewStat | (1<<TWINT);
	clr_DiagC();
	clr_DiagD();
	}
	

static void TwiInit()
	{
	TWSR = 0; // Prescaler bits not relevant if only in slave mode
	// TWBR = ((F_CPU / BusFrequenz) - 16) / (2 * TWI_PRESCALER); not relevant if only in slave mode
	TWAR = HellTwiAdresse << 1;
	TWCR = (1<<TWINT) | (1<<TWEA) | (0<<TWSTA) | (0<<TWSTO) | (1<<TWEN) | (0<<TWIE);
	}


static bool IstHellschreiberBereit()
	{
	static bool SchleifeZustand = true; // worst case von eingeschaltetem Gerät ausgehen.
	static TMsTimer PrellVerzoegerung;

	if (get_HellSchleife())
		{ // meldet ein
		if (SchleifeZustand)
			{ // war bereits ein
			StartTimer(&PrellVerzoegerung);
			return true;
			}
		else // war vorher aus
			{
			if (TimerVal(&PrellVerzoegerung) >= 500)
				{
				SchleifeZustand = true;
				return true;
				}
			else // abwarten
				return false;
			}
		}
	else
		{ // meldet aus
		if (!SchleifeZustand)
			{ // war bereits aus
			StartTimer(&PrellVerzoegerung);
			return false;
			}
		else // war vorher ein
			{
			if (TimerVal(&PrellVerzoegerung) >= 500)
				{
				SchleifeZustand = false;
				return false;
				}
			else // abwarten
				return true;
			}
		}
	} // IstHellschreiberBereit()


static void Initalisierungen()
{
	pgm_read_byte(Identifier); // Dummy read to force the identifier to be placed in the FLASH.

	init_HellTonGegenkopplung();
	init_HellAnrufPulse();
	init_HellSchleife();
	init_HellTonAusg();
	init_SDA(); // aktiviert Pull-Up fuer unbelegten Eingang
	init_SCL(); // aktiviert Pull-Up fuer unbelegten Eingang

	// init_LEDblau();
	init_DiagA();
	init_DiagB();
	init_DiagC();
	init_DiagD();
	init_DiagE();

	WatchdogAktiv = false;
	wdt_disable();
	
	DIDR1 = (1 << AIN1D) | (1 << AIN0D); // disables the digital input filtering of AIN0 and AIN1

	#ifdef TESTSER
	SerIOInit();
	#endif //def TESTSER

	MsTimerInit();

	TwiInit();
	
	HellMessPeriode = TIMER1_MESS_OV_NENNW;
	
	HellAuswertIndex = 0;

	PufferInit(&SerOutBuf);
	PufferInit(&TwiOutBuf);
	PufferInit(&HellAusgZeichenPuffer);
	
	sei();

	#ifdef TESTSER
	SerAusgPStr(PSTR("\r\nHellschreiber Signalprozessor\r\n"));
	#endif //def TESTSER
}


static void WarteAufEinschaltungKommendOderGehend()
	{
#ifdef TESTSER
	SerAusgPStr(PSTR("\r\nWarteAufEinschaltungKommendOderGehend\r\n"));
#endif //def TESTSER
	while (true)
		{
		PollTwi();
		
		if (!WatchdogAktiv && get_SCL())
			{
			WatchdogAktiv = true;
			wdt_enable(WDTO_1S);
			}

		if (IstHellschreiberBereit())
			// eingeschaltet, entweder selbst oder durch Anruf.
			break;

		if (!PufferLeer(&HellAusgZeichenPuffer)) // es gibt etwas zu drucken
			{ // bis zur Einschaltung immer 1 Sekunde Rufsignal und 10 Sekunden auf Einschaltung warten 
			TMsTimer KlingelTimer;
			TMsTimer KlingelFreqTimer;

			if (PufferZeig(&HellAusgZeichenPuffer) == HellAusschaltBefehl)
				{
				PufferAusg(&HellAusgZeichenPuffer); // Zeichen ignorieren
				continue;
				}
#ifdef TESTSER
			SerAusgPStr(PSTR("\r\nRufsignal EIN\r\n"));
#endif //def TESTSER
			StartTimer(&KlingelTimer);
			StartTimer(&KlingelFreqTimer);
			SET_BIT(HellStatus, HellStatBitSendeAnruf);
			while (!IstHellschreiberBereit() && TimerVal(&KlingelTimer) < 1000)
				{				
				PollTwi();
				if (TimerVal(&KlingelFreqTimer) < (1000 / HELL_ANRUF_FREQ) / 2)
					set_HellAnrufPulse();
				else if (TimerVal(&KlingelFreqTimer) < (1000 / HELL_ANRUF_FREQ))
					clr_HellAnrufPulse();
				else
					StartTimer(&KlingelFreqTimer);
				}
			clr_HellAnrufPulse();
#ifdef TESTSER
			SerAusgPStr(PSTR("\r\nRufsignal AUS\r\n"));
#endif //def TESTSER
			StartTimer(&KlingelTimer);
			CLR_BIT(HellStatus, HellStatBitSendeAnruf);
			while (!IstHellschreiberBereit() && TimerVal(&KlingelTimer) < 10000)
				PollTwi();
			} // Wiederholung des Rufsignals macht die Hauptschleife dieser Funktion.
		}

	SET_BIT(HellStatus, HellStatBitLaeuft);
	}


static void HellTonAusgabeEinschalten()
	{
	// benutzt Fast PWM
	OCR0A = TIMER0_TOP_TONAUSG;
	OCR0B = TIMER0_TOP_TONAUSG / 2;
	TCCR0A = (0 << COM0A0) | (1 << COM0B1) | (0 << COM0B0) | (1 << WGM01) | (1 << WGM00);
	TCCR0B = (1 << WGM02) | TIMER0_CS_TONAUSG;
	seto_HellTonAusg();
	// seto_LEDblau();
	SET_BIT(HellStatus, HellStatBitSendeTon);
	} // HellTonAusgabeEinschalten()


static void HellTonAusgabeAusschalten()
	{
	inp_HellTonAusg(); 
	// inp_LEDblau();
	TCCR0A = 0; // alles ausschalten
	TCCR0B = 0; // alles ausschalten
	CLR_BIT(HellStatus, HellStatBitSendeTon);
	} // HellTonAusgabeAusschalten()


static void HellZeichenSenden(uint8_t zeichen)
	{
	uint8_t PixelZaehler; // ex HellPixelZaehler
	uint8_t ReiheZaehler; // ex HellReiheZaehler
	uint16_t MusterTabPos; // ex HellSendeTabPos
	uint16_t SendeMuster; // ex HellSendeCode
	bool TonIstEin; // damit keine Glitches durch unnötige HellTonAusgabeEinschalten() passieren.

	if (zeichen < HELL_FONT_TAB_START || zeichen > HELL_FONT_TAB_END)
		return;

	// TIMER1 für den Pixeltakt konfigurieren
	TCNT1 = 0;
	TIFR1 = (1 << OCF1A) | (1 << OCF1B); // BIT setzen loescht es eigentlich
	OCR1A = TIMER1_MESS_OV_NENNW;
	OCR1B = TIMER1_MESS_OV_NENNW - 5000 / TIMER1_VORTEILER_MESS;
	TCCR1A = (0 << COM1A0) | (0 << COM1B0) | (0 << WGM11) | (0 << WGM10); // CTC Mode mit OCR1A
	TCCR1B = (0 << WGM13) | (1 << WGM12) | TIMER1_CS_MESS;

	SendeMuster = HELL_STARTCODE;
	MusterTabPos = (zeichen - HELL_FONT_TAB_START) * HELL_FONT_CHAR_SIZE;
	TonIstEin = false;

	for (ReiheZaehler = 0 ; ReiheZaehler < HELL_REIHEN_PRO_ZEICHEN ; ReiheZaehler++)
		{
		for (PixelZaehler = 0 ; PixelZaehler < HELL_PIXEL_PRO_REIHE ; PixelZaehler++)
			{
			if (SendeMuster & (1<<15))
				{
				if (!TonIstEin)
					{
					HellTonAusgabeEinschalten();
					TonIstEin = true;
					}
				}
			else
				{
				if (TonIstEin)	
					{
					HellTonAusgabeAusschalten();
					TonIstEin = false;
					}
				}

			while (!BIT_IS_SET(TIFR1, OCF1A)) 
				if (!BIT_IS_SET(TIFR1, OCF1B))
					PollTwi(); // kurz vor dem Eintritt des Interrupts nicht mehr pollen, damit der Zeitpunkt besser getroffen wird.
				else
					set_DiagA(); 

			TIFR1 = (1 << OCF1A) | (1 << OCF1B); // BIT setzen loescht es eigentlich
			clr_DiagA(); 

			SendeMuster <<= 1;
			}

		SendeMuster = pgm_read_word(&HellFontTab[MusterTabPos++]);
		}

	if (TonIstEin)
		HellTonAusgabeAusschalten();

	TCCR1A = 0; // alles Ausschalten
	TCCR1B = 0; // alles Ausschalten
	}


static void HellZeichenEmpfangAuswerten()
	{
	#define RL 4 // Anzahl Elemente in "Rangliste".

	char Zeichensatz[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ?+-/.,=:'()\032\033\034\035\036\037";
	int i2, ri;
	char VZeichen[RL];
	int Punkte[RL];
	int AktPunkte;
	int AnzahlVergl;
	int BestSchieb = 0;

	// set_LEDgelb();
	for (ri = 0; ri < RL; ri++)
		Punkte[ri] = -999;
	AnzahlVergl = 0;

#ifdef TESTSER
	// Ausgabe des "rohen" Puffers, ri und i2 werden "mißbraucht":
	for (ri = 0 ; ri < MESSBYTES_PRO_ZEICHEN ; ri++)
		for (i2 = 0 ; i2 < 8 ; i2++)
			{
			if (BIT_IS_SET(HellMessung[HellAuswertIndex + ri], 7 - i2))
				SerAusg('#');
			else
				SerAusg('.');
			if ((8 * ri + i2 + 1) % HELL_PIXEL_PRO_REIHE == 0)
				SerAusgPStr(PSTR("\r\n"));
			}
	SerAusgPStr(PSTR("\r\n"));
#endif //def TESTSER
			
	for (i2 = 0; i2 < sizeof(Zeichensatz) - 1; i2++)
		{
		AktPunkte = MessungVergleichen(Zeichensatz[i2]);
			if (AktPunkte >= 0)
				AnzahlVergl++;

		if (AktPunkte > Punkte[RL - 1])
			{
			for (ri = RL - 1; ri > 0; ri--)
				if (AktPunkte > Punkte[ri - 1])
					{
					Punkte[ri] = Punkte[ri - 1];
					VZeichen[ri] = VZeichen[ri - 1];
					}
				else
					break;
			Punkte[ri] = AktPunkte;
			VZeichen[ri] = Zeichensatz[i2];
			if (ri == 0)
				BestSchieb = MessungVergleichSchieb;
			}

		PollTwi();
		}

	HellAuswertIndex += MESSBYTES_PRO_ZEICHEN;
	if (HellAuswertIndex >= MESSBYTES_PRO_ZEICHEN * MESSUNG_ANZAHL_ZEICHEN)
		HellAuswertIndex = 0;
			
#ifdef TESTSER
	for (ri = 0; ri < RL; ri++)
		{
		SerAusg(VZeichen[ri]);
		SerAusg('=');
		SerAusgZahl(Punkte[ri]);
		SerAusg(' ');
		}
	SerAusgZahl(BestSchieb);
	SerAusg(' ');
	SerAusgZahl(AnzahlVergl);
	SerAusg(' ');
	SerAusgZahl(HellMessPeriode);
	SerAusgPStr(PSTR("\r\n"));
#endif //def TESTSER

	PufferSpeich(&TwiOutBuf, VZeichen[0]); // TODO ggf von Punktzahl abhängig machen.

	} // HellZeichenEmpfangAuswerten()


static void BestehendeVerbindungBearbeiten()
	{
	char DruckZeichen = '\0';

#ifdef TESTSER
	SerAusgPStr(PSTR("\r\nBestehendeVerbindungBearbeiten\r\n"));
#endif //def TESTSER
	
	HellTonEmpfEinschalten();
	while (DruckZeichen != HellAusschaltBefehl)
		{
		PollTwi();

		if (!IstHellschreiberBereit()) // Am Hellschreiber wurde Ausschalttaste gedrückt.
			{
			CLR_BIT(HellStatus, HellStatBitLaeuft);
			break;
			}

		if (HellMessSchreibIndex >= HellAuswertIndex + MESSBYTES_PRO_ZEICHEN || HellMessSchreibIndex < HellAuswertIndex)
			HellZeichenEmpfangAuswerten();

		if (!PufferLeer(&HellAusgZeichenPuffer)) 
			{
			HellTonEmpfAusschalten();
			while (!PufferLeer(&HellAusgZeichenPuffer))
				{
				DruckZeichen = PufferAusg(&HellAusgZeichenPuffer);
				if (DruckZeichen != HellAusschaltBefehl)
					HellZeichenSenden(DruckZeichen);
				}
			CLR_BIT(HellStatus, HellStatBitPufferVoll);
			HellTonEmpfEinschalten();
			}

		} // while (DruckZeichen != HellAusschaltBefehl)

	HellTonEmpfAusschalten();

	} // BestehendeVerbindungBearbeiten()


static void GrundstellungHerstellen()
	{
#ifdef TESTSER
	SerAusgPStr(PSTR("\r\nGrundstellungHerstellen\r\n"));
#endif //def TESTSER	
	if (IstHellschreiberBereit())
		{
#ifdef TESTSER
		SerAusgPStr(PSTR("\r\nAusschaltsignal EIN\r\n"));
#endif //def TESTSER
		HellTonAusgabeEinschalten(); // Dauerton bis zur Ausschaltung
		while (IstHellschreiberBereit())
			PollTwi();
		HellTonAusgabeAusschalten();
#ifdef TESTSER
		SerAusgPStr(PSTR("\r\nAusschaltsignal AUS\r\n"));
#endif //def TESTSER
		}
	CLR_BIT(HellStatus, HellStatBitLaeuft);
	} // GrundstellungHerstellen()


int main(void)
	{
	TMsTimer StartSperre; // 30 Sekunden jede Verbindung ablehnen. Zur Offenbarung von Abstürzen.
	
	Initalisierungen();
	StartTimer(&StartSperre);
	while (true)
		{ // Endlosschleife 
		GrundstellungHerstellen();
		WarteAufEinschaltungKommendOderGehend();
		if (TimerVal(&StartSperre) > 30000)
			BestehendeVerbindungBearbeiten();
		// sonst gleich wieder zu GrundstellungHerstellen gehen.
		}
	}


