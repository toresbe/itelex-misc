//================================================================
// Fernschreiber-Schnittstelle für TxP2-System
//	für ATmega168 auf Platine Spezial
//================================================================
//				
// Über serielle Schnittstelle
//	<Strg>A:	Anruf starten
//		1-0:	Wählen
//	a-z0-9:		Zeichen senden
//	<Strg>S:	Schluß
//	<Strg>W:	Werda
//	<Strg>B:	Buchstaben
//	<Strg>Z:	Ziffern
//	<Strg>K:	Klingel
//	<Strg>I:	Hier ist
//
// Möglichkeiten, den AB abzufragen:
//
// - externer Anruf mit Kennungsabfrage und Passwort
// oder
// - ^Q über serielle Schnittstelle
//
// danach:
// - wenn keine weitere Meldung Ausgabe "keine neue Meldung."; Ende
// - bei neuer Meldung 
// 	 - Ausgabe <Datum/Uhrzeit> und die ersten 4 Zeilen der Nachricht / maximal 130 Zeichen
//   - Ausgabe "Weiter, Loeschen, Naechste, Ende?   "
//     - bei Weiter: Text fortsetzen, danach Ausgabe "Loeschen, Naechste?   "
//     - bei Löschen: Anfangs-Kennung ändern, Ende suchen
//     - bei Naechste: Ende suchen, Anfangs-Adresse ggf. setzen
//     - bei Ende: Anfangs-Adresse ggf. setzen, Abbruch
//  
//================================================================
// verwendete Pins
//================================================================
//				
//   01: C6  	Reset
//   02: D0 	RxD 			
//   03: D1 	TxD 			
//   04: D2  	bis V1.0: CTS
//				ab V1.3: Taster nach Masse
//   05: D3  	bis V1.0: RTS
//				ab V1.3: LED grün (High = ein)
//   06: D4  	LED blau (High = ein)
//   07: VCC		
//   08: GND		
//   09: B6 	Quarz
//   10: B7 	Quarz
//   11: D5  	ab V1.3: XI1 (Sonderfunktion User)
//				HACK: Debug-Ausgang
//   12: D6   	ab V1.3: XI2 (Sonderfunktion User)
//   13: D7   	bis V1.0: SDA an EEPROM
//				ab V1.3: SCL an EEPROM
//   14: B0  	bis V1.0: SCL an EEPROM
//				ab V1.3: SDA an EEPROM
//   15: B1  	ab V1.3: RTS
//   16: B2  	ab V1.3: CTS
//   17: B3 MOSI	Synchronisation (High = Start, mit Pull-Up)
//   18: B4 MISO
//   19: B5 SCK 
//   20: AVCC
//   21: AREF
//   22: GND
//   23: C0  	bis V1.0: LED rot (High = ein)				
//   24: C1  	bis V1.0: LED gelb (High = ein)
//   25: C2  	bis V1.0: LED grün (High = ein)
//				ab V1.3: LED rot (High = ein)
//   26: C3  	bis V1.0: Taster (nach Low)
//				ab V1.3: LED gelb (High = ein)
//   27: C4 SDA	(Bus)
//   28: C5 SCL	(Bus)


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
#include "LokalAusgabe.h"
#include "BusKomm.h"
#include "KonfigDialog.h"
#include "FifoPuffer.h"

#ifndef OHNE_SPEICHER
#include "SwTwi.h"
#else
#define DoSwTwi() while(0) // nichts tun
#endif

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
PROGMEM const char Identifier[] = "___TxP2_SeriellUndSpeicher___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";


#include "timercs.h"

// Timer 1: Uhr
// ------------

#define TIMER1_OCFREQ 10 //!< Timer1 soll 10 OC-Ereignisse pro Minute erzeugen.

#define TIMER1_CS TCCR_DIV(1, 1024)
#define TIMER1_PRESCALER 1024
#define TIMER1_FREQ (F_CPU / TIMER1_PRESCALER)

#define TIMER1_OCRA ((TIMER1_FREQ * 60 / TIMER1_OCFREQ) - 1)
	//!< Sollwert für Output Compare Register des Timer1
	
	// das ist gleichzeitig der MAX-Wert
//#define TIMER1_OCRB_FREQ 5000
//#define TIMER1_OCRB_INC (TIMER1_FREQ / TIMER1_OCRB_FREQ + 1)
	// bestimmt die Aufruf-Frequenz von OCR1B
	// OCR1B nicht mehr benutzt


// sonstige Konstanten
// -------------------

#define KENNUNG_MAXLEN 20 //!< maximale Länge des Kennungsgebers.
#define KENNWORT_MAXLEN 20 //!< maximale Länge des Kennwortes für die Fernabfrage.


// interner Eeprom-Speicher
// ------------------------

EEMEM uint8_t Platzhalter[4]; //!< Anfang des EEPROM ist gern von Störungen betroffen
EEMEM uint8_t BusEigenAdresse_EE = BusAdrUngueltig; //!< #BusEigenAdresse, Kopie im EEPROM
EEMEM char Kennung_EE[KENNUNG_MAXLEN] = "\r\ntxp2-ab"; //!< #Kennung, Kopie im EEPROM
EEMEM char Kennwort_EE[KENNWORT_MAXLEN] = "kennwort"; //!< #Kennwort, Kopie im EEPROM
EEMEM uint8_t Jahr_EE = 9;  //!< #Jahr, Kopie im EEPROM
EEMEM uint8_t Monat_EE = 4; //!< #Monat, Kopie im EEPROM
EEMEM uint8_t Tag_EE = 13;  //!< #Tag, Kopie im EEPROM
EEMEM uint8_t Stunde_EE[31] = { 0 } ; //!< #Stunde, Kopie im EEPROM. Je Tag eine andere Speicherstelle, damit die Abnutzung nicht so groß ist.
EEMEM uint8_t Minute_EE = 0; //!< #Minute, Kopie im EEPROM, wird nur bei besonderer Bedienung gespeichert.
EEMEM uint16_t BeginnErsteMeldung2_EE = 0xEEEE; //!< #BeginnErsteMeldung2, Kopie im EEPROM

// Uhr
// ---

uint8_t Jahr; //!< Jahr der mitlaufenden Uhr (nur die letzten zwei Stellen).
uint8_t Monat; //!< Monat der mitlaufenden Uhr.
uint8_t Tag; //!< Tag der mitlaufenden Uhr.
uint8_t Stunde; //!< Stunde der mitlaufenden Uhr.
uint8_t Minute; //!< Minute der mitlaufenden Uhr.

// Kennung und Kennwort

char Kennung[KENNUNG_MAXLEN]; //!< Eigene Kennung, da kein echter Fernschreiber angeschlossen.
char Kennwort[KENNWORT_MAXLEN]; //!< Kennwort für Fernabfrage des Anrufspeichers.


// Grundfunktionen
// ---------------

#include "TxP2-Endgeraet.h"


// Interrupts
// ----------

volatile uint16_t Timer1OvfC; //!< Zählt die Timer1-Oberflows. Wird mit Takt #TIMER1_OCFREQ inkrementiert.

ISR(TIMER1_COMPA_vect) //!< Timer1-Interrupt. (#TIMER1_OCFREQ). Grundtakt für die mitlaufende Uhr.
	{
	Timer1OvfC++;
	}


//! Schaltet bei interrupt-gesteuerter Software-TWI-Bearbeitung den Interrupt ein.
void StartSwTwi()
	{
/*
	SET_BIT(TIMSK1, OCIE1B);
	uint16_t NeuOCR = TCNT1 + TIMER1_OCRB_INC;
	if (NeuOCR >= TIMER1_OCRA)
		OCR1B = TIMER1_OCRB_INC; // etwas suboptimal, aber ok
	else
		OCR1B = NeuOCR;
DoSwTwi muss jetzt explizit aufgerufen werden */
	}
	

/*
ISR(TIMER1_COMPB_vect)
	{
	void DoSwTwi();

	DoSwTwi();
	StartSwTwi(); // Restart...
	}
DoSwTwi muss jetzt explizit aufgerufen werden */


//! Schaltet bei interrupt-gesteuerter Software-TWI-Bearbeitung den Interrupt aus.
void StopSwTwi()
	{
/*
	CLR_BIT(TIMSK1, OCIE1B);
DoSwTwi muss jetzt explizit aufgerufen werden */
	}


// Uhr
// ===


//! Aktualisiert die mitlaufende Uhr. Basis ist der Timer1.
static void UhrAktualisieren()
	{
	static uint8_t MonatsTab[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31} ;

	while (Timer1OvfC >= TIMER1_OCFREQ) // pro Minute!!
		{
		uint8_t SregAlt = SREG;
		cli();
		Minute++;
		Timer1OvfC -= TIMER1_OCFREQ;
		SREG = SregAlt;
		}
	if (Minute >= 60)
		{
		Stunde++;
		Minute -= 60;
		if (Stunde >= 24)
			{
			uint8_t MaxTag = MonatsTab[Monat-1];
			if (Monat == 2 && (Jahr & 3) == 0)
				MaxTag = 29;
			Tag++;
			Stunde -= 24;
			if (Tag > MaxTag)
				{
				Monat++;
				Tag = 1;
				if (Monat > 12)
					{
					Jahr++;
					Monat = 1;
					}
				}
			}
		}
	}	
		

	
void LokalZeichenAusgabe(char c);


//! Gibt das aktuelle Datum aus.	
static void DatumAusgabe()
	{
	LokalZahlAusgabe(Tag, 2);
	LokalZeichenAusgabe('.');
	LokalZahlAusgabe(Monat, 2);
	LokalZeichenAusgabe('.');
	LokalZahlAusgabe(Jahr, 2);
	LokalZeichenAusgabe(' ');
	LokalZahlAusgabe(Stunde, 2);
	LokalZeichenAusgabe(':');
	LokalZahlAusgabe(Minute, 2);
	}


// Puffer für serielle Schnittstelle und anderes
// ---------------------------------------------

volatile bool SerInEsc = false; //!< Flag für Umsetzung ESC-X in Ctrl-X (wird nach ESC gesetzt).

TPuffer SerInBuf; //!< Empfangspuffer für die serielle Schnittstelle. Nicht identisch mit Puffer für Baudot-Ein/-Ausgabe.
TPuffer SerOutBuf; //!< Sendepuffer für die serielle Schnittstelle. Nicht identisch mit Puffer für Baudot-Ein/-Ausgabe.

// Übersetzung bestimmter Tasten in Doppel-Codes

PROGMEM const char SerInUebersE[] = "\r\näöüÄÖÜß<>[]{}"; //!< Übersetzungstabelle für Umlaute: zu übersetzendes Zeichen.
PROGMEM const char SerInUebers1[] = "\r\raouAOUs(.(:(,"; //!< Übersetzungstabelle für Umlaute: erstes Ersatzzeichen.
PROGMEM const char SerInUebers2[] = "\n\neeeeees.):),)"; //!< Übersetzungstabelle für Umlaute: zweites Ersatzzeichen.


//! Funktion zur Ausgabe von Zeichen einschließlich Sonderzeichen.
//----------------------------------------------------------------
//! stellt Ctrl-X als ^X dar.
static void LokalZeichenAusgabeKlar(char c)
	{
	if (c >= ' ' || c == '\r' || c == '\n')
		LokalZeichenAusgabe(c);
	else
		{
		LokalZeichenAusgabe('^');
		LokalZeichenAusgabe(c + 'A' - 1);
		}
	}


/*
ISR(USART_UDRE_vect)
	{
	// TODO: CTS berücksichtigen...
	if (PufferLeer(&SerOutBuf))
		CLR_BIT(UCSR0B, UDRIE0);
	else
		UDR0 = PufferAusg(&SerOutBuf);
	}
*/


//! Meldet Serielle Schnittstelle bereitschaft zur Übernahme weiterer Daten?
//--------------------------------------------------------------------------
//! \retval true bei Empfangsbereitschaft der Gegenstelle.
static bool GetCTS()
	{ 
	return !BIT_IS_SET(SER_CTS_IPORT, SER_CTS_BIT);
	}
	
	
//! Ist serielle Schnittstelle überhaupt angeschlossen?
//-----------------------------------------------------
//! \retval true wenn CTS nicht auf Dater-Aus
static bool SeriellBereit()
	{
	TMsTimer Timer;

	StartTimer(&Timer);
	while (TimerVal(&Timer) < 1000)
		{
		wdt_reset();
		if (GetCTS())
			return true;
		}
	return false;
	}
	

//! Schreibt Zeichen aus dem Puffer auf die serielle Schnittstelle und bringt
//! ankommende Zeichen in den Puffer.
//---------------------------------------------------------------------------
//! Regelmäßig aufrufen.
static void SeriellIO()
	{
	if (BIT_IS_SET(UCSR0A, RXC0))
		{
		if (PufferAnzahl(&SerInBuf) < MaxPuffer - 3)
			{
			char c = UDR0;
			if (c == ESC)
				SerInEsc = true;
			else if (SerInEsc)
				{
				PufferSpeich(&SerInBuf, c & 0x1F);
				SerInEsc = false;
				LokalZeichenAusgabeKlar(c);
				}
			else
				{
				PGM_P p = SerInUebersE;
				while (pgm_read_byte(p) != '\0')
					{
					if (pgm_read_byte(p) == c)
						{
						c = pgm_read_byte(SerInUebers1 + (p - SerInUebersE));
						PufferSpeich(&SerInBuf, c);
						LokalZeichenAusgabeKlar(c);
						c = pgm_read_byte(SerInUebers2 + (p - SerInUebersE));
						// Ausgabe erfolgt gleich...
						break;
						}
					else
						p++;
					}
				PufferSpeich(&SerInBuf, c); 
					// hier wird entweder das original-Zeichen gespeichert oder das zweite übersetzte
				LokalZeichenAusgabeKlar(c);
				} // kein ESC
			} // PufferAnzahl(&SerInBuf) < MaxPuffer - 3
			
		if (PufferAnzahl(&SerInBuf) > MaxPuffer / 2)
			{
			SET_BIT(SER_RTS_OPORT, SER_RTS_BIT);
			//LED_EIN(ROT); // Test HACK
			}

		} // Serielles Zeichen empfangen

//* entfällt bei ISR(UDRE)... 
	if (!PufferLeer(&SerOutBuf) 
		&& BIT_IS_SET(UCSR0A, UDRE0) 
		&& GetCTS())
		{
		UDR0 = PufferAusg(&SerOutBuf);
		}
//*/

	}


//! Wartet bis zum leeren des Seriellen-Ausgabepuffers.
//-----------------------------------------------------
//! Diese Funktion aufrufen, wenn umfangreiche Textausgaben vollständig
//! zur seriellen Schnittstelle zu senden sind.	
void SerSendFlush()
	{
	while (!PufferLeer(&SerOutBuf))
		{
		SeriellIO();
		UhrAktualisieren();
		DoSwTwi();
		}
	}


//!	Holt das nächste Zeichen aus dem seriell-Empfangspuffer.
//----------------------------------------------------------
//! Vorher muss sicher sein, dass mindestens ein Zeichen im Empfangspuffer
//! ist!
//! \param Loesch Zeichen wird auch aus dem Empfangspuffer gelöscht
//! \returns Das nächste Zeichen des Empfangspuffers.
static char SerEmpfZ(bool Loesch)
	{
	char Res = PufferAusg(&SerInBuf);
	if (!Loesch)
		PufferSpeich(&SerInBuf, Res);
	else if (PufferAnzahl(&SerInBuf) < MaxPuffer / 2)
		{
		CLR_BIT(SER_RTS_OPORT, SER_RTS_BIT);
		//LED_AUS(ROT); // Test HACK
		}

	return Res;
	}

	
//! Wartet, bis ein Zeichen über die serielle Schnittstelle angekommen ist.
//-------------------------------------------------------------------------
//! Alle erforderlichen Routinefunktionen werden aufgerufen.
//! Falls gefüllt, wird auch das nächste Zeichen aus dem Empfangspuffer verwendet.
//! \returns Das nächste Zeichen.
char LokalZeichenLesen()
	{
	while (PufferLeer(&SerInBuf))
		{
		SeriellIO();
		UhrAktualisieren();
		TastePruefen();
		DoSwTwi();
		if (Tastendruck != NichtGedr)
			{
			Tastendruck = NichtGedr;
			return '\t';
			}
		}
	if (SerEmpfZ(false) == CTRL('s'))
		{
		SerEmpfZ(true); // Zeichen löschen
		return '\0';
		}
	else
		return SerEmpfZ(true);
	}
	
	
//! Ist ein Zeichen von der seriellen Schnittstelle (Empfangspuffer) noch unbearbeitet?
//! \returns Empfangspuffer ist nicht leer.	
static bool LokalEingabeErfolgt()
	{
	return !PufferLeer(&SerInBuf);
	}
	
	
//! Ein Zeichen auf der seriellen Schnittstelle ausgeben.
//-------------------------------------------------------
//! Wenn Ausgabepuffer voll, blockiert diese Funktion, erledigt aber die 
//! Routinefunktionen.
//! \param c Das auszugebende Zeichen.

void LokalZeichenAusgabe(char c)
	{
	if (PufferVoll(&SerOutBuf))
		SerSendFlush(); // TODO: Nicht ewig warten...
	PufferSpeich(&SerOutBuf, c);
	// SET_BIT(UCSR0B, UDRIE0);
	}


//! Initialisiert serielle Schnittstelle und Puffer dazu.
static void SerIOInit()
	{
	// PORTS initialisieren (Ausgabepins)
	// Serielle Schnittstelle initialisieren
	#define BAUD 9600
	#include <util/setbaud.h>
	UBRR0H = UBRRH_VALUE;
	UBRR0L = UBRRL_VALUE;
	#if USE_2X
	UCSR0A = (1 << U2X0);
	#else
	UCSR0A = (0 << U2X0);
	#endif

	UCSR0B = (1<<TXEN0)+(1<<RXEN0)+(0<<RXCIE0)+(0<<UCSZ02);
	UCSR0C = (0<<UMSEL01)+(0<<UMSEL00)+(0<<UPM00)+(0<<UPM01)+(0<<USBS0)+(1<<UCSZ01)+(1<<UCSZ00);
	}


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
	// 11: Fehler beim externen EEPROM
	// 12: Versuch, beim MEGA 8 mehrere Adressen einzustellen
	// 13: GeEinschalten liefert ungültigen Code
	{
	TWCR = (1<<TWINT) | (0<<TWEA) | (0<<TWSTA) | (1<<TWSTO) | (1<<TWEN) | (0<<TWIE);
	
	uint8_t TasteZ = 0;
	bool TasteWirk = false;
	TMsTimer TasteTimer;
	StartTimer(&TasteTimer);
	while (1)
		{
		wdt_reset();

		UhrAktualisieren();
		
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


// Verbindungen bearbeiten
// =======================


static void VerbindungSteht(bool SeriellEin, bool AufzeichnungEin);


//! Bearbeitet ankommende Verbindungen.
//-------------------------------------
//! Sendet an Verbindungspartner den Einschaltauftrag. Startet ggf. die 
//! Aufzeichnung.
static void VerbindungKommend()
	{
	bool SeriellEin;
	
	LED_EIN(GRUEN);
	
	SeriellEin = SeriellBereit();
	
	if (SeriellEin)
		{
		LokalTextAusgabeP(PSTR("\r\nAnruf\r\n"));
		SerSendFlush();
		}

#ifndef OHNE_SPEICHER
	bool AufzeichnungEin = AufzeichnungBeginn(Jahr, Monat, Tag, Stunde, Minute);
#endif //ndef OHNE_SPEICHER

	if (GeEinschalten() != GeEinschAnrufquitt)
		{
		if (SeriellEin)
			LokalTextAusgabeP(PSTR("\r\nFehler\r\n"));
#ifndef OHNE_SPEICHER
		AufzeichnungAbbruch();
#endif //ndef OHNE_SPEICHER
		GeAusschalten();
		}
	else
		{
#ifndef OHNE_SPEICHER
		VerbindungSteht(SeriellEin, AufzeichnungEin);
#else //def OHNE_SPEICHER
		VerbindungSteht(SeriellEin, false);
#endif //else def OHNE_SPEICHER

		}
		
	}
	

static void VerbindungGehend()
	{
	LED_EIN(GELB);

	switch (GeEinschalten())
		{ // hier nur break benutzen, wenn Einschaltung erfolgreich
		case GeEinschFehler:
			return; 

		case GeEinschWahl:
			LokalTextAusgabeP(PSTR("\r\nWählen: "));
			SerSendFlush();
			while (!KoEinschalten())
				{
				SeriellIO();
				DoSwTwi();
				if (!PufferLeer(&SerInBuf))
					{
					char c;
					c = SerEmpfZ(true);
					if (c >= '0' && c <= '9')
						{ // Ziffer eingegeben
						GeWaehlen(c - '0');
						}

					else if (c == CTRL('s'))
						{ // Abbruch durch Bediener
						GeAusschalten();
						return;
						}

					}

				if (KoAusschalten())
					{
					LokalTextAusgabeP(PSTR("\r\nAbbruch"));
					GeAusschalten();
					return;
					}

				} // while !KoEinschalten()

			break; // ist jetzt Verbunden

//		case GeEinschSofortEin: GIBTS NICHT MEHR
//			break; // ist jetzt Verbunden

//		case GeEinschFremdKonfig: GIBTS NICHT MEHR
//			LeitungsSstKonfigurationsDialog();
//			GeAusschalten();
//			return; // keine normale Verbindung

		default:
			FehlerStop(13); 
			return;
		}

	LokalTextAusgabeP(PSTR("\r\nVerbunden\r\n"));

	VerbindungSteht(true, false);

	}


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
	
	
static void GeSendeText(char* s)
	{
	GeSendeCode(TtyCodeBuUm); // für definierte Verhältnisse...
	while (*s != '\0')
		{
		GeSendeZeichen(*s);
		s++;
		}
	}
	

static void GeSendeTextP(PGM_P s)
	{
	GeSendeCode(TtyCodeBuUm); // für definierte Verhältnisse...
	while (pgm_read_byte(s) != '\0')
		{
		GeSendeZeichen(pgm_read_byte(s));
		s++;
		}
	}


// #define DEBUG_OUT


#ifndef OHNE_SPEICHER

static void Wiedergabe(void (*FnZchnAusg)(char c),
						void (*FnTextPAusg)(PGM_P s),
						void (*FnAusgFlush)(),
						bool (*FnUnterbrechung)(),
						char (*FnZeichenEing)())
	// gibt die aufgezeichneten Meldungen wieder
	{ 
	bool Beenden;
	uint8_t ZeichenZaehler, ZeilenZaehler;
	TMsTimer Timer;

	// warten, bis nicht mehr geschrieben wird...
	StartTimer(&Timer);
	while (TimerVal(&Timer) < 500)
		{
		if ((*FnUnterbrechung)())
			{
			(*FnZeichenEing)(); // nachlaufende Zeichen verwerfen
			StartTimer(&Timer);
			}
		DoSwTwi();
		} 

	WiedergabeStart();

	Beenden = false;

#ifdef DEBUG_OUT
	extern uint16_t WiedergabeAdresse;
	extern uint16_t EndeLetzteMeldung;

	LokalTextAusgabeP(PSTR("\r\nEndeLetzte: "));
	LokalZahlAusgabe16(EndeLetzteMeldung, 0);
	LokalTextAusgabeP(PSTR("   Nach Start: "));
	LokalZahlAusgabe16(WiedergabeAdresse, 0);
	LokalTextAusgabeP(PSTR("\r\n"));
#endif //DEBUG_OUT

	(*FnTextPAusg)(PSTR("\r\nStarte Wiedergabe...\r\n"));
	(*FnAusgFlush)();

	if (!WiedergabeNaechsteMeldung()) 
		{
		(*FnTextPAusg)(PSTR("\r\nkeine ungelesenen Meldungen\r\n"));
		WiedergabeEnde();
		return;
		}

	while (!Beenden)
		{
#ifdef DEBUG_OUT
		LokalTextAusgabeP(PSTR("\r\nWiedergBeginn: "));
		LokalZahlAusgabe16(WiedergabeAdresse, 0);
#endif //DEBUG_OUT

		ZeichenZaehler = 0;
		ZeilenZaehler = 0;

		while (true)
			{
			DoSwTwi();

			char c = WiedergabeZeichen();
			if (c == '\0')
				{
#ifdef DEBUG_OUT
				LokalTextAusgabeP(PSTR("\r\nMeld-Ende: "));
				LokalZahlAusgabe16(WiedergabeAdresse, 0);
				LokalTextAusgabeP(PSTR("\r\n"));
#endif //DEBUG_OUT
				(*FnTextPAusg)(PSTR("\r\n--- Ende Meldung ---"));
				(*FnAusgFlush)();
				(*FnTextPAusg)(PSTR("\r\nLoeschen, Naechste, Ende?   "));
				(*FnAusgFlush)();
				break;
				}
				
			(*FnZchnAusg)(c);
			(*FnAusgFlush)();

			if (ZeichenZaehler < 250)
				ZeichenZaehler++;
			
			if (c == '\n')
				{
				// LED_EIN(ROT); // HACK
				ZeilenZaehler++;
				if (ZeichenZaehler > 160 || ZeilenZaehler > 3)
					{
					ZeichenZaehler = 0;
					ZeilenZaehler = 0;
					
					StartTimer(&Timer);
					while (TimerVal(&Timer) < 1500)
						{
						if ((*FnUnterbrechung)())
							break; // Timer vorzeitig abbrechen
						DoSwTwi();
						}
					} // 4. Zeile oder >160 Zeichen
				// LED_AUS(ROT); // HACK
				} // Zeilenvorschub
				
			if ((*FnUnterbrechung)())
				break;
				
			}

		// LED_EIN(ROT); // HACK

		bool Verstanden = false;
		bool SpringeNaechste = false;
		while (!Verstanden)
			{
			DoSwTwi();
			char z = (*FnZeichenEing)();

			// mehrfache Zeichen verwerfen...
			StartTimer(&Timer);

			while (TimerVal(&Timer) < 300)
				{
				if ((*FnUnterbrechung)())
					{
					(*FnZeichenEing)(); // nachlaufende Zeichen verwerfen
					StartTimer(&Timer);
					}
				DoSwTwi();
				} 

			// nur das erste Zeichen auswerten...
			switch (z)
				{
				case 'l':
				case ')': // falls BU-ZI-Umschaltung nicht wirkte...
					(*FnTextPAusg)(PSTR("...Loesche..."));
					(*FnAusgFlush)();
					WiedergabeLoescheAktuelleMeldung(); // springt auch automatisch zur nächsten
					Verstanden = true;
					SpringeNaechste = true;
					break;

				case 'n' :
				case ',': // falls BU-ZI-Umschaltung nicht wirkte...
					(*FnTextPAusg)(PSTR("...Naechste..."));
					(*FnAusgFlush)();
					Verstanden = true;
					SpringeNaechste = true;
					break;

				case 'w' :
				case '2': // falls BU-ZI-Umschaltung nicht wirkte...
					Verstanden = true;
					break;
	
				case 'e' :
				case '3': // falls BU-ZI-Umschaltung nicht wirkte...
					(*FnTextPAusg)(PSTR("...Abbruch"));
					(*FnAusgFlush)();
					Beenden = true;
					Verstanden = true;
					break;
				}
				
			} // while !Verstanden

		// LED_AUS(ROT); // HACK

#ifdef DEBUG_OUT
		LokalTextAusgabeP(PSTR("\r\nNach WiedAktion: "));
		LokalZahlAusgabe16(WiedergabeAdresse, 0);
		LokalTextAusgabeP(PSTR("\r\n"));
#endif //DEBUG_OUT

		if (SpringeNaechste)
			{
			if (WiedergabeNaechsteMeldung())
				(*FnTextPAusg)(PSTR("\r\n"));
			else
				break;
			}

		} // while (!Beenden)
		
#ifdef DEBUG_OUT
	LokalTextAusgabeP(PSTR("\r\nEnde: "));
	LokalZahlAusgabe16(WiedergabeAdresse, 0);
	LokalTextAusgabeP(PSTR("\r\n"));
#endif //DEBUG_OUT

	if (!Beenden)
		(*FnTextPAusg)(PSTR("\r\n--- keine weiteren Meldungen ---\r\n"));

	WiedergabeEnde();
	}
	
#endif //ndef OHNE_SPEICHER

	
static bool KennungsausgabeUndKennwortAbfrage(bool SeriellEin, bool AufzeichnungEin)
	{
	char *p;
	char c;
	
	GeSendeText(Kennung);
	//! \todo Kennungsausgabe auch Aufzeichnen.
	
	GeSendeCode(TtyCodeBuUm);

	p = Kennwort;
	while (true)
		{
		DoSwTwi();
		if (KoEmpfZeichen(&c))
			{
			if (SeriellEin)
				LokalZeichenAusgabe(c);

#ifndef OHNE_SPEICHER
			if (AufzeichnungEin)
				AufzeichnungZeichen(c);
#endif //ndef OHNE_SPEICHER

			if ((c == '\r' || c == '\n') && *p == '\0')
				{
				// HACK TEST: LED_EIN(ROT);
				return true;
				}
			else if (c == *p) // Vergleich eingegebenes Zeichen mit aktuellem Kennwort-Soll-Zeichen
				p++; // erledigt gleichzeitig eine falsche Wortlänge
			else
				return false;
			}
				
		if (KoAusschalten()) // falls Abbruch durch Sender
			return false;
		}
	}
	

//! Hilfsfunktion bei Wiedergabe an Gegenstelle (Fernabfrage).
static bool ZeichenEmpfangen()
	{
	return !PufferLeer(&EmpfPuffer);
	}
	
	
//! Hilfsfunktion bei Wiedergabe an Gegenstelle (Fernabfrage).
static char ZeichenLesen()
	{
	char res;
	
	while (!KoEmpfZeichen(&res))
		{
		DoSwTwi();
		if (KoAusschalten())
			return 'e';
		}
	return res;
	}
	
	
static void ZeichenSenden(char c)
// nur Hilfsfunktion bei Wiedergabe an Gegenstelle (Fernabfrage)
	{
	while (!GeSendeZeichen(c))
		{
		DoSwTwi();
		if (KoAusschalten())
			return;
		LEDAktualisieren();
		}
	}
	

static void SendenAbschliessen()
// nur Hilfsfunktion bei Wiedergabe an Gegenstelle (Fernabfrage)
	{
	while (!PufferLeer(&SendePuffer) && !KoAusschalten())
		{
		DoSwTwi();
		LEDAktualisieren();
		}
	}
	
		
static void VerbindungSteht(bool SeriellEin, bool AufzeichnungEin)
	{
	while (true)
		{
		char c;

		SeriellIO(); 

		if (KoEmpfZeichen(&c))
			{
			if (c == CTRL('w'))
				{
				if (KennungsausgabeUndKennwortAbfrage(SeriellEin, AufzeichnungEin))
					{ // richtiges Kennwort eingegeben
#ifndef OHNE_SPEICHER
					AufzeichnungAbbruch();
					AufzeichnungEin = false;
					Wiedergabe(&ZeichenSenden, 
							   &GeSendeTextP,
							   &SendenAbschliessen,
							   &ZeichenEmpfangen,
							   &ZeichenLesen);
#endif //ndef OHNE_SPEICHER
					}
				}
			else
				{
				if (SeriellEin)
					LokalZeichenAusgabe(c);
#ifndef OHNE_SPEICHER
				if (AufzeichnungEin)
					AufzeichnungZeichen(c);
#endif //ndef OHNE_SPEICHER
				}
			}

		if (KoAusschalten())
			{
#ifndef OHNE_SPEICHER
			if (AufzeichnungEin)
				AufzeichnungEnde();
#endif //ndef OHNE_SPEICHER

			GeAusschalten();
			if (SeriellEin)
				LokalTextAusgabeP(PSTR("\r\nGetrennt\r\n"));
			return;
			}

		if (!PufferLeer(&SerInBuf) && GeSendePufferLeer())
			{
			char c;
			c = SerEmpfZ(true);
			
			if (c == CTRL('i'))
				// Eigene Kennung ausgeben
				{
				GeSendeText(Kennung);
#ifndef OHNE_SPEICHER
				if (AufzeichnungEin)
					; //! \todo Ausgegebene Kennung auch im Protokoll speichern.
					AufzeichnungZeichen(c);
#endif //ndef OHNE_SPEICHER
				}
				
			else if (c == CTRL('s'))
				// Abbruch durch Bediener
				{
#ifndef OHNE_SPEICHER
				if (AufzeichnungEin)
					AufzeichnungEnde();
#endif //ndef OHNE_SPEICHER
				GeAusschalten();
				if (SeriellEin)
					LokalTextAusgabeP(PSTR("\r\nBeendet\r\n"));
				return;
				}
			else
				GeSendeZeichen(c);

			} // if !PufferLeer(&SerInBuf)

		DoSwTwi();
		LEDAktualisieren();
		}

	}


static void Deaktivieren()
// wird nach kurzem Tastendruck aufgerufen
	{
	LED_EIN(BLAU);
	Aktivieren(false);

	while (Tastendruck == NichtGedr)
		{
		TastePruefen();
		DoSwTwi();
		}

	Tastendruck = NichtGedr;

	Aktivieren(true);
	LED_AUS(BLAU);

	} // Deaktivieren


static void Konfiguration()
// wird nach langem Tastendruck aufgerufen
	{
	LED_EIN(ROT);
	Aktivieren(false);

#ifdef TWI_DEBUG
	uint8_t* AusgP;

	LokalTextAusgabeP(PSTR(".twi:"));
	for (AusgP = DebugBuf ; AusgP < DebugBufP ; AusgP++)
		{
		LokalZeichenAusgabe(' ');
		LokalHexAusgabe(*AusgP);
		}
	LokalTextAusgabeP(PSTR(" <<<"));
	DebugBufP = DebugBuf;
#endif //!TWI_DEBUG

	LokalTextAusgabeP(PSTR("\r\n konfiguration seriell+speicher version " SVNVERSION " datum " __DATE__));

	if (!KonfigurationAllgemein())
		{
		Aktivieren(true);
		LED_AUS(ROT);
		return;
		}

	LokalTextAusgabeP(PSTR("\r\n Datum/Uhrzeit: "));
	DatumAusgabe();
	LokalTextAusgabeP(PSTR(" neu:         "));
	if (LokalZahlEingabe(&Tag, 0) == 0
		|| LokalZahlEingabe(&Monat, 0) == 0
		|| LokalZahlEingabe(&Jahr, 0) == 0
		|| LokalZahlEingabe(&Stunde, 0) == 0
		|| LokalZahlEingabe(&Minute, 0) == 0)
		return;
	Timer1OvfC = 0;
	TCNT1 = 0;

	LokalTextAusgabeP(PSTR("\r\n Kennung: "));
	LokalTextAusgabe(Kennung + 2); // CR + LF weglassen
	LokalTextAusgabeP(PSTR(" neu:         "));
	LokalTextEingabe(Kennung + 2, KENNUNG_MAXLEN - 3); // erste 2 Zeichen für CRLF reserviert
	
	LokalTextAusgabeP(PSTR("\r\n Kennwort: "));
	LokalTextAusgabe(Kennwort);
	LokalTextAusgabeP(PSTR(" neu:         "));
	LokalTextEingabe(Kennwort, KENNWORT_MAXLEN - 1);
	
	LokalTextAusgabeP(PSTR("\r\n fertig+++   \r\n"));

	Aktivieren(true);
	LED_AUS(ROT);

	}


static void BusteilnehmerListen()
	{
	LokalTextAusgabeP(PSTR("\r\nStatus der angeschlossenen Module:\r\n"));
	for (uint8_t AnzZif = 1 ; AnzZif <= 2 ; AnzZif++)
		for (uint8_t Wahl = 0 ; Wahl <= ((AnzZif == 1) ? 9 : 99) ; Wahl++)
			{
			uint8_t BusNr = WahlZuAdresse(Wahl, AnzZif);
			int16_t Stat = ((BusNr == BusEigenAdresse) ? Status : GetStatus(BusNr));
			if (Stat >= 0)
				{
				LokalZahlAusgabe(Wahl, AnzZif);
				LokalTextAusgabeP(PSTR(": "));
				LokalZahlAusgabe(Stat, 0);
				LokalTextAusgabeP(PSTR("\r\n"));
				}
			}
	}


/*/ nur für Debugging...

void LokalZahlAusgabe16(uint16_t i, int8_t minzif)
	{
	if (i >= 10 || minzif > 1)
		{
		uint16_t z = i / 10;
		LokalZahlAusgabe(z, minzif - 1);
		i -= 10 * z;
		}
	LokalZifferAusgabe(i);
	}

//*/	


//! Das Hauptprogramm der Fernschreiber-Simulation mit RS232.
//-----------------------------------------------------------
int main()
	{
	bool HauptmenueAusgeben = true;

#ifndef OHNE_SPEICHER
	extern uint16_t BeginnErsteMeldung2;
#endif //ndef OHNE_SPEICHER

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

	SET_BIT(SER_RTS_DDR, SER_RTS_BIT);

	SET_BIT(TEST1_DDR, TEST1_BIT);

	// Timer initialisieren
	MsTimerInit();
	
	// Timer für Uhr
	TCCR1A = (0<<WGM11) | (0<<WGM10); // Clear on Timer Compare mit OCR1A
	TCCR1B = (0<<WGM13) | (1<<WGM12) | TIMER1_CS;
	OCR1A = TIMER1_OCRA; 
	//OCR1B = TIMER1_OCRB_INC; DoSwTwi wird jetzt direkt aufgerufen
	SET_BIT(TIMSK1, OCIE1A);
	//SET_BIT(TIMSK1, OCIE1B); DoSwTwi wird jetzt direkt aufgerufen

	BusEigenAdresse = eeprom_read_byte(&BusEigenAdresse_EE) & 0xFE;
	BusEigenAdrMehrfach = 1;
	
	Jahr = eeprom_read_byte(&Jahr_EE);
	Monat = eeprom_read_byte(&Monat_EE);
	Tag = eeprom_read_byte(&Tag_EE);
	Stunde = eeprom_read_byte(&Stunde_EE[Tag-1]);
	Minute = eeprom_read_byte(&Minute_EE);

	UhrAktualisieren();
	eeprom_read_string(Kennung, Kennung_EE);
	eeprom_read_string(Kennwort, Kennwort_EE);

#ifndef OHNE_SPEICHER
	BeginnErsteMeldung2 = eeprom_read_word(&BeginnErsteMeldung2_EE);
#endif //ndef OHNE_SPEICHER

	SerIOInit();

	KommInit();

	sei();

	if (SeriellBereit())
		LokalTextAusgabeP(PSTR("\r\nSTART" __DATE__ "/" __TIME__));

	TMsTimer Timer;
	StartTimer(&Timer);

	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 250)
		SeriellIO();

	LED_AUS(ROT);
	LED_EIN(GELB);

	TwiInit();

	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 500)
		SeriellIO();

	LED_AUS(GELB);
	LED_EIN(GRUEN);

	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 750)
		SeriellIO();

	LED_AUS(GRUEN);
	LED_EIN(BLAU);

#ifndef OHNE_SPEICHER
	MsgSpeicherInit();
#endif //ndef OHNE_SPEICHER

	// TWI nochmal resetten
	TWCR = (1<<TWINT) | (0<<TWEA) | (0<<TWSTA) | (1<<TWSTO) | (0<<TWEN) | (0<<TWIE);

	while (TimerVal(&Timer) < 760)
		SeriellIO();
		
	TWCR = (1<<TWINT) | (1<<TWEA) | (0<<TWSTA) | (0<<TWSTO) | (1<<TWEN) | (1<<TWIE);

	while (TimerVal(&Timer) < 1000 + BusEigenAdresse)
		SeriellIO();

	BusEigenAdressePruefenUndSetzen(BusEigenAdresse);

	while (1)
		{
		// aktueller Zustand: Ausgeschaltet
		if (TimerVal(&Timer) <= 800)
			LED_AUS(ROT);
		else if (TimerVal(&Timer) <= 1000)
			{
			if (BusEigenAdresse == BusAdrUngueltig)
				LED_EIN(ROT);
			else
				LED_AUS(ROT);
			}
		else
			StartTimer(&Timer);
		LED_AUS(GELB);
		LED_AUS(GRUEN);
		LED_AUS(BLAU);

		DoSwTwi();

		if (HauptmenueAusgeben)
			{
			if (SeriellBereit())
				{
				LokalTextAusgabeP(PSTR("\r\n"));

				/*/ TEST:
				extern uint8_t FsBetriebsart;
				LokalTextAusgabeP(PSTR("\r\nBetriebsart: "));
				LokalZahlAusgabe(FsBetriebsart, 0);
				LokalTextAusgabeP(PSTR("\r\nStatus: "));
				LokalZahlAusgabe(Status, 0); //*/

				SerSendFlush();
				DatumAusgabe();
				SerSendFlush();
				LokalTextAusgabeP(PSTR("\r\nCtrl-A: Anwahl"));
				SerSendFlush();
				LokalTextAusgabeP(PSTR(", Ctrl-K: Konfiguration"));
#ifdef BUSDEBUG_DIALOG
				SerSendFlush();
				LokalTextAusgabeP(PSTR(", Ctrl-D: Debug"));
#endif
#ifndef OHNE_SPEICHER
				SerSendFlush();
				LokalTextAusgabeP(PSTR(", Ctrl-Q: AB-Wiedergabe"));
#endif //ndef OHNE_SPEICHER
				SerSendFlush();
				LokalTextAusgabeP(PSTR(" --> "));
				} // if (SeriellBereit())
			HauptmenueAusgeben = false;
			}

		TastePruefen();
		SeriellIO(); 
		UhrAktualisieren();
		
		if (KoEinschalten())
			{
			VerbindungKommend();
			HauptmenueAusgeben = true;
			}
		
		if (!PufferLeer(&SerInBuf))
			{
			char c;
			c = SerEmpfZ(true);
			switch (c)
				{
				case CTRL('a'):
					if (BusEigenAdresse != BusAdrUngueltig)
						VerbindungGehend();
					else
						LokalTextAusgabeP(PSTR(" Fehler: nicht konfiguriert."));
					HauptmenueAusgeben = true;
					break;
				
				case CTRL('k'):
					eeprom_write_byte(&Minute_EE, Minute);
					Konfiguration();
					HauptmenueAusgeben = true;
					break;

#ifndef OHNE_SPEICHER
//				case CTRL('e'):
//					XEepromDebug();
//					HauptmenueAusgeben = true;
//					break;
			
				case CTRL('q'):
					Aktivieren(false);
					Wiedergabe(&LokalZeichenAusgabeKlar, 
							   &LokalTextAusgabeP,
							   &SerSendFlush,
							   &LokalEingabeErfolgt,
							   &LokalZeichenLesen);
					Aktivieren(true);
					HauptmenueAusgeben = true;
					break;

#endif //ndef OHNE_SPEICHER

//HACK:
				case CTRL('r'):
					wdt_enable(WDTO_1S);
					cli();
					while (true)
						LED_EIN(BLAU);
					// wird beendet durch Watchdog-Reset
//:HACK
					
				case CTRL('l'):
					Aktivieren(false);
					BusteilnehmerListen();
					Aktivieren(true);
					break;
										
				default:
					LokalTextAusgabeP(PSTR("\r\nUngültiges Kommando"));
					HauptmenueAusgeben = true;
					break;
				}
			}
					
		if (Tastendruck == Lang)
			{
			Tastendruck = NichtGedr;
			if (SeriellBereit())
				Konfiguration();
			HauptmenueAusgeben = true;
			}

		else if (Tastendruck == Kurz)
			{
			Tastendruck = NichtGedr;
			eeprom_write_byte(&Minute_EE, Minute);
			Deaktivieren();
			}

#ifndef OHNE_SPEICHER
		while (!EeAbschliessen())
			DoSwTwi(); // Eeprom-Zugriffe beenden...
#endif //ndef OHNE_SPEICHER
		
		// Parameter im eigenen Eeprom aktualisieren
		eeprom_write_byte_noblock(&BusEigenAdresse_EE, BusEigenAdresse);
		eeprom_write_byte_noblock(&Jahr_EE, Jahr);
		eeprom_write_byte_noblock(&Monat_EE, Monat);
		eeprom_write_byte_noblock(&Tag_EE, Tag);
		eeprom_write_byte_noblock(&Stunde_EE[Tag-1], Stunde);
		eeprom_write_string_noblock(Kennung_EE, Kennung);
		eeprom_write_string_noblock(Kennwort_EE, Kennwort);

#ifndef OHNE_SPEICHER
		eeprom_write_word_noblock(&BeginnErsteMeldung2_EE, BeginnErsteMeldung2);
#endif //ndef OHNE_SPEICHER

		} // while (1)
	} // main()


