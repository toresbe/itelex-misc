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
// verwendete Pins siehe ports.h
//================================================================
//				


#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <inttypes.h>

#include "bits.h"

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
#include "KonfigSpeicher.h"
#include "FifoPuffer.h"
#include "LokalUhr.h"

#ifdef AF_ANRUFSPEICHER
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


#if (PLATINE_VERSION >= 20)
	
#ifdef PROGIDZUSATZ
//! Identifikation im Programmspeicher
const char PROGMEM Identifier[] = "___itlx_SeriellUndSpeicher2-" PROGIDZUSATZ "___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#else
//! Identifikation im Programmspeicher
const char PROGMEM Identifier[] = "___itlx_SeriellUndSpeicher2___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#endif

#else // PLATINE_VERSION < 20
	
#ifdef PROGIDZUSATZ
//! Identifikation im Programmspeicher
const char PROGMEM Identifier[] = "___itlx_SeriellUndSpeicher-" PROGIDZUSATZ "___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#else
//! Identifikation im Programmspeicher
const char PROGMEM Identifier[] = "___itlx_SeriellUndSpeicher___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#endif
	
#endif // PLATINE_VERSION


#ifdef SPRACHE_EN
#define Sprachwahl(de, en) en
#else
#define Sprachwahl(de, en) de 
#endif


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


// Eeprom-Speicher-Adressen
// ------------------------

enum {
	EEAdr_BusEigenAdresse = 0,
    EEAdr_Kennung = 1,
	EEAdr_Kennwort = 21,
    EEAdr_Jahr = 41,
	EEAdr_Monat = 42,
	EEAdr_Tag = 43,
	EEAdr_Stunde = 44, // Je Tag eine andere Speicherstelle, damit die Abnutzung nicht so groß ist.
	EEAdr_Minute = 75,
	EEAdr_BeginnErsteMeldung2 = 76,
    EEAdr_UmleitungAbweisen = 78,
	EEAdr_MenueImmerAusgeben = 79,
	EEAdr_SeriellHwHandshake = 80,
	EEAdr_Sperrzeiten = 81, // Beansprucht 20 Bytes
    EEAdr_Zeichensatz = 101,
};


// Typen
// -----

typedef enum { SperreTaste, SperreStoerung, SperreZeit, SperreWahl } TSperreGrund;


// sonstige Konfigurationen
// ------------------------

bool MenueImmerAusgeben; //!< Gibt das Menue nach jeder "Aktion" aus, bei false nur bei Fehleingabe

bool SeriellHwHandshake; //!< bei true werden Daten auf der seriellen Schnittstelle nur bei CTS = aktiv gesendet.

uint8_t CodeIndex; //!< Index der Code-Tabelle, siehe auch #CodeTabWechsel() in BaudotCode.c


TMsTimer CheckCtsTimer; 
	//!< Um ein Blockieren des Programmablaufs zu verhindern, wird bei Pufferüberlauf seriellen Ausgabe 
	//!< der Puuferanfang gelöscht, sofern CTS zu lange (mehr als 3 Sekunden) auf "Low" liegt.


// Uhr
// ---

uint8_t MinuteLetzeRundsendung; //!< Minute der letzten Rundsendung der Uhrzeit.
bool UhrzeitUeberBus; //!< Uhrzeit wird über den Bus übertragen, somit kein Speichern im 
					  //!< EEPROM und keine Abfrage in der Konfiguration.


// Kennung und Kennwort
// --------------------

char Kennung[KENNUNG_MAXLEN]; //!< Eigene Kennung, da kein echter Fernschreiber angeschlossen.
char Kennwort[KENNWORT_MAXLEN]; //!< Kennwort für Fernabfrage des Anrufspeichers.


// Sonstiges
// ---------

TMsTimer NachlaufTimer; //!< Steuert nur den Ausgang für den externen SV-Schalter

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


// Uhr
// ===


#ifdef AF_LOKALE_UHR

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
	} // UhrAktualisieren()
	
#else //ndef AF_LOKALE_UHR

#define UhrAktualisieren() while(0)
	
#endif
		

	
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
	if (!get_SER_CTS() || !SeriellHwHandshake)
		// wenn kein HW-Handshake (also RTS/CTS), dann CTS als "Dauer-OK" annehmen.
		{
		StartTimer(&CheckCtsTimer);
		return true;
		}
	else
		return false;
	}
	
	
//! Schreibt Zeichen aus dem Puffer auf die serielle Schnittstelle und bringt
//! ankommende Zeichen in den Puffer.
//---------------------------------------------------------------------------
//! Regelmäßig aufrufen.
static void SeriellIO()
	{
	if (BIT_IS_SET(UCSR0A, RXC0))
		{ // Zeichen empfangen
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

				LokalZeichenAusgabeKlar(c);

				if (c == '%')
					c = CodeChrKlingel;
				
				PufferSpeich(&SerInBuf, c); 
					// hier wird entweder das original-Zeichen gespeichert oder das zweite übersetzte
				} // kein ESC
			} // PufferAnzahl(&SerInBuf) < MaxPuffer - 3
			
		if (PufferAnzahl(&SerInBuf) > MaxPuffer / 2)
			{
			set_SER_RTS();
			//set_LEDROT(); // Test HACK
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


//!	Holt das nächste Zeichen aus dem seriell-Empfangspuffer.
//----------------------------------------------------------
//! Vorher muss sicher sein, dass mindestens ein Zeichen im Empfangspuffer
//! ist!
//! \param Loesch Zeichen wird auch aus dem Empfangspuffer gelöscht
//! \returns Das nächste Zeichen des Empfangspuffers.
static char SerEmpfZ(bool Loesch)
	{
	char Res;
	if (Loesch)
		Res = PufferAusg(&SerInBuf);
	else
		Res = PufferZeig(&SerInBuf);
	
	if (PufferAnzahl(&SerInBuf) < MaxPuffer / 2)
		{
		clr_SER_RTS();
		//clr_LEDROT(); // Test HACK
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
		
	char c;
	c = SerEmpfZ(true);
	if (c == CTRL('s'))
		return '\0';
	else
		{
		if (c >= 'A' && c <= 'Z')
			c += 'a'-'A'; // zum Kleinbuchstaben umwandeln
		return c;
		}
	}
	
	
//! Ein Zeichen auf der seriellen Schnittstelle ausgeben.
//-------------------------------------------------------
//! Wenn Ausgabepuffer voll, blockiert diese Funktion, erledigt aber die 
//! Routinefunktionen.
//! \param c Das auszugebende Zeichen.

void LokalZeichenAusgabe(char c)
	{
	while (PufferVoll(&SerOutBuf))
		// im Verbindungszustand nicht warten, zeichen geht ggf. verloren
		{
		SeriellIO();
		UhrAktualisieren();
		DoSwTwi();
		TastePruefen();
		GetCTS();
		if (TimerVal(&CheckCtsTimer) > 3000 || Tastendruck != NichtGedr || KoEinschalten())
			PufferAusg(&SerOutBuf); // wird verworfen um Platz zu schaffen.
		}

	PufferSpeich(&SerOutBuf, c); 
	// SET_BIT(UCSR0B, UDRIE0);
	}

	
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


//! Modul / Schnittstelle irreversibel stoppen.

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
	// 11: Fehler beim externen EEPROM
	// 12: Versuch, beim MEGA 8 mehrere Adressen einzustellen
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


// Zeitsperre
// =======================

// !!!!!!!!!!!!!!!!!!
// Damit die Zeitsperre beim Nicht-Aktivierung keinen Platz wegnimmt, muss diese hier
// eingebunden werden, anstelle sie nur zu linken.

#ifdef AF_ZEITSPERRE

#include "Zeitsperre.h"

#include "Zeitsperre.c"

#endif // def AF_ZEITSPERRE



// Verbindungen bearbeiten
// =======================


static void VerbindungSteht(bool AufzeichnungEin);

static void KommendSperren(TSperreGrund Grund);


//! Bearbeitet ankommende Verbindungen.
//-------------------------------------
//! Sendet an Verbindungspartner den Einschaltauftrag. Startet ggf. die 
//! Aufzeichnung. Kehrt erst nach Verbindungsabbau zurück.
static void VerbindungKommend()
	{
	set_LEDGRUEN();
	
	set_SV_EIN(); // Benennung SV_EIN nur als "Verweis" auf Standard-Schnittstelle (TW39 / ED1000)
	StartTimer(&NachlaufTimer);
	
	LokalTextAusgabeP(PSTR(Sprachwahl("\r\nAnruf\r\n" ,"\r\nIncall\r\n")));

#ifdef AF_ANRUFSPEICHER
	bool AufzeichnungEin = AufzeichnungBeginn(Jahr, Monat, Tag, Stunde, Minute);
#endif //def AF_ANRUFSPEICHER

	if (!GeEinschaltQuittung())
		{
		LokalTextAusgabeP(PSTR(Sprachwahl("\r\nFehler\r\n", "\r\nError\r\n")));
#ifdef AF_ANRUFSPEICHER
		AufzeichnungAbbruch();
#endif //def AF_ANRUFSPEICHER
		GeAusschalten(true);
		}
	else
		{
#ifdef AF_ANRUFSPEICHER
		VerbindungSteht(AufzeichnungEin);
#else //ndef AF_ANRUFSPEICHER
		VerbindungSteht(false);
#endif //else ndef AF_ANRUFSPEICHER

		}

	StartTimer(&NachlaufTimer); // Ausschaltung erfolgt dann in der Hauptschleife
		
	} // VerbindungKommend()
	

//! Bearbeitet gehende Verbindungen.
//-------------------------------------
//! Wartet auf Wahlziffern, ermittelt Verbindungspartner, sendet den Einschaltauftrag. 
//! Kehrt erst nach Verbindungsabbau zurück.
static void VerbindungGehend()
	{
	if (BusEigenAdresse == BusAdrUngueltig)
		{
		return;
		}

	#ifdef AF_ZEITSPERRE
	SperrzeitAussetzen();
	#endif //def AF_ZEITSPERRE

	set_LEDGELB();
	if (!GeAnrufBeginn())
		{ 
		clr_LEDGELB();
		return; 
		}
		
	LokalTextAusgabeP(PSTR(Sprachwahl("\r\nWaehlen: ", "\r\nDial: ")));
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
				GeAusschalten(false);
				} // auf die Quittung wird dann in dieser Schleife gewartet...
			}

		if (KoAusschalten())
			{
			LokalTextAusgabeP(PSTR(Sprachwahl("\r\nAbbruch", "\r\nAbort")));
			GeAusschalten(false); // zu warten ist nicht mehr nötig.
			// TODO hier Lokalbetrieb / KommendSperre?
			return;
			}

		} // while !KoEinschalten()

	LokalTextAusgabeP(PSTR(Sprachwahl("\r\nVerbunden\r\n", "\r\nConnected\r\n")));

	if (!GeEinschaltQuittung())
		{
		LokalTextAusgabeP(PSTR(Sprachwahl("Fehler\r\n", "failed\r\n")));
		GeAusschalten(false); // zu warten ist nicht mehr nötig.
		return;
		}

	VerbindungSteht(false);

#ifdef AF_ZEITSPERRE
	SperrzeitAussetzen();
#endif //def AF_ZEITSPERRE

	} // VerbindungGehend()


//! Schaltet LED entspechend der Status-Bits an.
static void LEDAktualisieren()
	{
	if (BIT_IS_SET(Status, StatBit_AngerufenBelegt))
		bset_LEDGELB(!BIT_IS_SET(Status, StatBit_FsMeldEin));
	else // !BIT_IS_SET(Status, StatBit_AngerufenBelegt))
		bset_LEDGRUEN(!BIT_IS_SET(Status, StatBit_FsMeldEin));

	bset_LEDBLAU(!BIT_IS_SET(Status, StatBit_FsBefEin));
	}
	

//! Sendet ein Zeichen, wartet aber bei vollem Puffer
static void ZeichenSenden(char c)
	{
	while (!GeSendePufferLeer())
		{
		DoSwTwi();
		SeriellIO();
		if (KoAusschalten())
			return;
		LEDAktualisieren();
		}
	while (!GeSendeZeichen(c))
		; // kann eigentlich nicht lange dauern
	}
	
	
//! Sendet einen Ascii-Text
static void GeSendeText(char* s)
	{
	while (!GeSendeCode(TtyCodeBuUm))
		; // probieren bis es geklappt hat... LEDAktualisieren und DoSwTwi hier egal.
	while (*s != '\0')
		{
		ZeichenSenden(*s);
		LokalZeichenAusgabeKlar(*s);
		s++;
		SeriellIO();
		}
	}
	

// #define DEBUG_OUT


//! Gibt die eigene Kennung beim Verbindungspartner aus und wertet eingegegeben Text
//! auf Übereinstimmung mit dem gespeicherten Kennwort aus.
static bool KennungsausgabeUndKennwortAbfrage(bool AufzeichnungEin)
	{
	char *p;
	char c;
	
	GeSendeText(Kennung);
	//! \todo Kennungsausgabe auch Aufzeichnen.

	while (!GeSendeCode(TtyCodeBuUm))
		; // probieren bis es geklappt hat... LEDAktualisieren und DoSwTwi hier egal.

	p = Kennwort;
	while (true)
		{
		DoSwTwi();
		SeriellIO();
		LEDAktualisieren();
		
		if (KoEmpfZeichen(&c))
			{
			LokalZeichenAusgabe(c);

#ifdef AF_ANRUFSPEICHER
			if (AufzeichnungEin)
				AufzeichnungZeichen(c);
#endif //def AF_ANRUFSPEICHER

			if (c == '\r' || c == '\n') 
				{
				if (*p == '\0') // am Ende des Soll-Kennworts angekommen
					{
					// HACK TEST: set_LEDROT();
					return true;
					}
				else if (p == Kennwort)
					; // Noch kein Zeichen eingegeben --> WR/ZL ignorieren
				else
					return false; // Kennwort zu früh beendet.
				}
			else if (c == CodeChrWerDa)
				{	
				GeSendeText(Kennung);
				p = Kennwort; // von vorn
				}
			else if (c == *p) // Vergleich eingegebenes Zeichen mit aktuellem Kennwort-Soll-Zeichen
				p++; 
			else
				return false;
			}
				
		if (KoAusschalten()) // falls Abbruch durch Sender
			return false;
			
		if (!PufferLeer(&SerInBuf)) // Eingabe über serielle Schnittstelle unterbricht Passwort-Auswertung
			return false;
			
		} // while true
	} // KennungsausgabeUndKennwortAbfrage()
	


#ifdef AF_ANRUFSPEICHER


//! Gibt die aufgezeichneten Meldungen wieder.
//--------------------------------------------
//! Basisfunktion für lokale Wiedergabe und Fernabfrage. 
//! \param FnZchnAusg Funktion für die Wiedergabe eines Zeichens.
//! \param FnTextPAusg Funktion für die Wiedergabe eines Textes aus dem Programmspeicher.
//! \param FnUnterbrechung Funktion für die Abfrage, ob der Benutzer ein Zeichen eingegegeben hat.
//! \param FnZeichenEing Funktion für die Abfrage eines durch den Benutzer eingegegeben Zeichens.

static void Wiedergabe(void (*FnZchnAusg)(char c),
						void (*FnTextPAusg)(PGM_P s),
						bool (*FnUnterbrechung)(),
						char (*FnZeichenEing)())
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
		SeriellIO();
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

	(*FnTextPAusg)(PSTR(Sprachwahl("\r\nStarte Wiedergabe...\r\n", "\r\nstart printout...\r\n")));

	if (!WiedergabeNaechsteMeldung()) 
		{
		(*FnTextPAusg)(PSTR(Sprachwahl("\r\nkeine ungelesenen Meldungen\r\n", "\r\nno unread messages\r\n")));
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
			SeriellIO();

			char c = WiedergabeZeichen();
			if (c == '\0')
				{
#ifdef DEBUG_OUT
				LokalTextAusgabeP(PSTR("\r\nMeld-Ende: "));
				LokalZahlAusgabe16(WiedergabeAdresse, 0);
				LokalTextAusgabeP(PSTR("\r\n"));
#endif //DEBUG_OUT
				(*FnTextPAusg)(PSTR(Sprachwahl("\r\n--- Ende Meldung ---", "\r\n--- end of message ---")));		
				(*FnTextPAusg)(PSTR(Sprachwahl("\r\nLoeschen, Naechste, Ende?   ", "\r\nDelete, Next, End?   ")));
				break;
				}
				
			(*FnZchnAusg)(c);

			if (ZeichenZaehler < 250)
				ZeichenZaehler++;
			
			if (c == '\n')
				{
				// set_LEDROT(); // HACK
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
				// clr_LEDROT(); // HACK
				} // Zeilenvorschub
				
			if ((*FnUnterbrechung)())
				break;
				
			}

		// set_LEDROT(); // HACK

		bool Verstanden = false;
		bool SpringeNaechste = false;
		while (!Verstanden)
			{
			DoSwTwi();
			SeriellIO();
			
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
				case Sprachwahl('l', 'd'):
				case Sprachwahl(')', '\''): // falls BU-ZI-Umschaltung nicht wirkte...
					(*FnTextPAusg)(PSTR(Sprachwahl("...Loesche...", "...deleting...")));
					WiedergabeLoescheAktuelleMeldung(); // springt auch automatisch zur nächsten
					Verstanden = true;
					SpringeNaechste = true;
					break;

				case 'n' :
				case ',': // falls BU-ZI-Umschaltung nicht wirkte...
					(*FnTextPAusg)(PSTR(Sprachwahl("...Naechste...", "...next...")));
					Verstanden = true;
					SpringeNaechste = true;
					break;

				case Sprachwahl('r', 'w') : // wiederholen
				case Sprachwahl('4', '2'): // falls BU-ZI-Umschaltung nicht wirkte...
					Verstanden = true;
					break;
	
				case 'e' :
				case '3': // falls BU-ZI-Umschaltung nicht wirkte...
					(*FnTextPAusg)(PSTR(Sprachwahl("...Abbruch", "...abort")));
					Beenden = true;
					Verstanden = true;
					break;
				}
				
			} // while !Verstanden

		// clr_LEDROT(); // HACK

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
		(*FnTextPAusg)(PSTR(Sprachwahl("\r\n--- keine weiteren Meldungen ---\r\n", "\r\n--- no more messages ---\r\n")));
	WiedergabeEnde();
	}
	

//! Sendet einen Text aus dem Programmspeicher an den Verbindungspartner.
static void GeSendeTextP(PGM_P s)
	{
	while (!GeSendeCode(TtyCodeBuUm))
		; // probieren bis es geklappt hat... LEDAktualisieren und DoSwTwi hier egal.
	while (pgm_read_byte(s) != '\0')
		{
		ZeichenSenden(pgm_read_byte(s));
		LokalZeichenAusgabeKlar(pgm_read_byte(s));
		s++;
		SeriellIO();
		}
	}


//! Ist ein Zeichen von der seriellen Schnittstelle (Empfangspuffer) noch unbearbeitet?
//! \returns Empfangspuffer ist nicht leer.	
static bool LokalEingabeErfolgt()
	{
	return !PufferLeer(&SerInBuf);
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
		SeriellIO();
		LEDAktualisieren();
		if (KoAusschalten())
			return 'e';
		}
	return res;
	}
	

#endif //def AF_ANRUFSPEICHER

	
//! Behandelt nach Verbindungsaufbau die Datenübertragung in beiden Richtungen.
//-----------------------------------------------------------------------------
//! Wird bei kommenden und bei gehenden Verbindungen benutzt.
static void VerbindungSteht(bool AufzeichnungEin)
	{
	while (true)
		{
		char c;

		SeriellIO(); 

		if (KoEmpfZeichen(&c))
			{
			if (c == CodeChrWerDa) // TODO and Kennung nicht leer
				{
				if (KennungsausgabeUndKennwortAbfrage(AufzeichnungEin))
					{ // richtiges Kennwort eingegeben
#ifdef AF_ANRUFSPEICHER
					AufzeichnungAbbruch();
					AufzeichnungEin = false;
					Wiedergabe(&ZeichenSenden, 
							   &GeSendeTextP,
							   &ZeichenEmpfangen,
							   &ZeichenLesen);
#endif //def AF_ANRUFSPEICHER
					}
				}
			else
				{
				LokalZeichenAusgabe(c);
#ifdef AF_ANRUFSPEICHER
				if (AufzeichnungEin)
					AufzeichnungZeichen(c);
#endif //def AF_ANRUFSPEICHER
				}
			}

		if (KoAusschalten())
			{
#ifdef AF_ANRUFSPEICHER
			if (AufzeichnungEin)
				AufzeichnungEnde();
#endif //def AF_ANRUFSPEICHER

			LokalTextAusgabeP(PSTR(Sprachwahl("\r\nGetrennt\r\n", "\r\nDisconnected\r\n")));

			GeAusschalten(true);

			return;
			}

		if (!PufferLeer(&SerInBuf) && GeSendePufferLeer())
			{
			char c;
			c = SerEmpfZ(true);
			
			if (c == CTRL('i') || c == CTRL('f'))
				// Eigene Kennung ausgeben
				{ // TODO außer Kennung ist leer.
				GeSendeText(Kennung);
#ifdef AF_ANRUFSPEICHER
				if (AufzeichnungEin)
					AufzeichnungZeichen(c);
#endif //def AF_ANRUFSPEICHER
				}
				
			else if (c == CTRL('s'))
				// Abbruch durch Bediener
				{
#ifdef AF_ANRUFSPEICHER
				if (AufzeichnungEin)
					AufzeichnungEnde();
#endif //def AF_ANRUFSPEICHER

				GeAusschalten(false);
				while (!KoAusschalten())
					{
					DoSwTwi();
					LEDAktualisieren();
					SeriellIO(); 
					}
				
				LokalTextAusgabeP(PSTR(Sprachwahl("\r\nBeendet\r\n", "\r\nDisconnected\r\n")));
				return;
				}
				
			else if (c == CTRL('w') || c == CTRL('e'))
				ZeichenSenden(CodeChrWerDa);
				
			else // kein besonderer CTRL-Code
				ZeichenSenden(c);

			} // if !PufferLeer(&SerInBuf)

		DoSwTwi();
		LEDAktualisieren();
		}

	} // VerbindungSteht()


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
#ifdef AF_ZEITSPERRE
			SperrzeitAussetzen();
#endif //def AF_ZEITSPERRE
			break;
			}

		DoSwTwi();
		SeriellIO(); 
			
		if (!PufferLeer(&SerInBuf))
			break; // jede Eingabe beendet den Kommend-Sperre-Modus
			
		if (TimerVal(&BlinkTimer) > 500 * BlinkTaktFaktor)
			StartTimer(&BlinkTimer);
		else
			bset_LEDBLAU(TimerVal(&BlinkTimer) > 300 * BlinkTaktFaktor);
		
		if (RundsendAnzDaten > 0)
			{
			if (LokalUhrPruefeRundsendung(RundsendDaten, RundsendAnzDaten))
				{
#ifdef AF_ZEITSPERRE
				if (Grund == SperreZeit && !SperrzeitAktiv())
					{
					RundsendAnzDaten = 0;
					break;
					}
#endif //def AF_ZEITSPERRE
				UhrzeitUeberBus	= true;
				}
			// else Daten anderwertig auswerten
			
			RundsendAnzDaten = 0;
			}
		
		}
		
	clr_LEDBLAU();
	Aktivieren(true);
		
	} // KommendSperren()
	
	
	
//! wird nach kurzem Tastendruck aufgerufen
static void Deaktivieren()
	{
	clr_LEDROT();
	clr_LEDGELB();
	clr_LEDGRUEN();
	set_LEDBLAU();
	Aktivieren(false);
	
#ifdef AF_BEI_DEAKT_SERIELL_DURCHLEIT
	UCSR0B = 0; // deactivated the UART
	init_SYS_SER_OUT();
	init_SYS_SER_IN();
#else
#endif //def AF_BEI_DEAKT_SERIELL_DURCHLEIT

	while (Tastendruck == NichtGedr)
		{
		TastePruefen();
#ifdef AF_BEI_DEAKT_SERIELL_DURCHLEIT
		bset_SYS_SER_OUT(get_EXT_SER_IN());
		bset_EXT_SER_OUT(get_SYS_SER_IN());
#else
		DoSwTwi();
		SeriellIO();
#endif //def AF_BEI_DEAKT_SERIELL_DURCHLEIT
		}

	Tastendruck = NichtGedr;

#ifdef AF_BEI_DEAKT_SERIELL_DURCHLEIT
	SerIOInit();
#endif //def AF_BEI_DEAKT_SERIELL_DURCHLEIT

	Aktivieren(true);
	
	clr_LEDBLAU();

	KommendSperren(SperreTaste);

	} // Deaktivieren



bool KonfigHwHandshake; 
	//!< waehrend der Konfiguration wird der Hardware-Handshake ausgeschaltet.
	//!< der tatsächliche Konfigurationswert wird derweil hier gespeichert.


// der folgende text wird mehrfach verwendet:

#ifdef AF_TTYCODE_SWITCHABLE

PROGMEM const char CodepageSelectStrP[] = Sprachwahl("\r\nZeichensatz waehlen: 0=ita2 1=ustty 2=KOI7N2 3=KOI8-R 4=Griechisch 5=Schwedish 6=Daenish: ", 
													 "\r\nselect character set: 0=ita2 1=ustty 2=KOI7N2 3=KOI8-R 4=Greek 5=Swedish 6=Dansk: ");
// diese Liste muss den verfügbaren Zeichensätzen von #CodeTabWechsel entsprechen.

#endif //def AF_TTYCODE_SWITCHABLE


//! wird nach langem Tastendruck aufgerufen
static void Konfiguration()
	{
	bool Abbruch;
	
	set_LEDROT();
	clr_LEDGELB();
	clr_LEDGRUEN();
	clr_LEDBLAU();

	Aktivieren(false);
	
	KonfigHwHandshake = SeriellHwHandshake;
	SeriellHwHandshake = false; // damit das Menü immer aufgerufen werden kann.
	
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

	BaudotMode_SetEmpfangen(BaudotMode); // damit auch eine BU-Umschaltung gesendet wird.

	LokalTextAusgabeP(PSTR(Sprachwahl("\r\n konfiguration seriell+speicher version " SVNVERSION " datum " __DATE__, "\r\n config telex serial version " SVNVERSION " date " __DATE__)));
	Abbruch = !KonfigurationAllgemein();
	if (Abbruch) 
		return;

#ifdef AF_TTYCODE_SWITCHABLE

	LokalTextAusgabeP(CodepageSelectStrP);

	// TODO: Alte Codepage anzeigen
	
	if (LokalZahlEingabe(&CodeIndex, 1) < 0)
		return;
	CodeTabWechsel(CodeIndex);
		
#endif //def AF_TTYCODE_SWITCHABLE


#ifdef AF_LOKALE_UHR

	if (!UhrzeitUeberBus)
		{
		LokalTextAusgabeP(PSTR(Sprachwahl("\r\n Datum/Uhrzeit: ", "\r\n date/time: ")));
	
		DatumAusgabe();
		LokalTextAusgabeP(NeuStrP);
		if (LokalZahlEingabe(&Tag, 2) < 0
			|| LokalZahlEingabe(&Monat, 2) < 0
			|| LokalZahlEingabe(&Jahr, 2) < 0
			|| LokalZahlEingabe(&Stunde, 2) < 0
			|| LokalZahlEingabe(&Minute, 2) < 0)
			return;
		Timer1OvfC = 0;
		TCNT1 = 0;
		}

#endif //def AF_LOKALE_UHR

	LokalTextAusgabeP(PSTR(Sprachwahl("\r\n Kennung: ", "\r\n answerback: ")));
	Kennung[0] = '\r';
	Kennung[1] = '\n';
	LokalTextAusgabe(Kennung + 2); // CR + LF weglassen
	LokalTextAusgabeP(NeuStrP);
	if (LokalTextEingabe(Kennung + 2, KENNUNG_MAXLEN - 3) == 0) // erste 2 Zeichen für CRLF reserviert
		return;

	LokalTextAusgabeP(PSTR(Sprachwahl("\r\n Kennwort: ", "\r\n password: ")));
	LokalTextAusgabe(Kennwort);
	LokalTextAusgabeP(NeuStrP);
	if (LokalTextEingabe(Kennwort, KENNWORT_MAXLEN - 1) == 0)
		return;

	LokalTextAusgabeP(PSTR(Sprachwahl("\r\n hauptmenue regelmaessig ausgeben? aktuell: ", "\r\n print main menu frequently? current: ")));

	LokalBoolAusgabe(MenueImmerAusgeben);
	LokalTextAusgabeP(NeuStrP);

	if (LokalBoolEingabe(&MenueImmerAusgeben) == 0)
		return;
	LokalTextAusgabeP(OkStrP);

#ifdef AF_ZEITSPERRE
	// Sperrzeiten
	// -----------
	if (!SperrzeitEingabeDialog())
		return;

#endif //def AF_ZEITSPERRE

	LokalTextAusgabeP(PSTR(Sprachwahl("\r\n hardware handshake auf ser. sst. verwenden? aktuell: ", "\r\n use hardware handshake on output? current: ")));
	LokalBoolAusgabe(KonfigHwHandshake);
	LokalTextAusgabeP(NeuStrP);

	if (LokalBoolEingabe(&KonfigHwHandshake) == 0)
		return;

	LokalTextAusgabeP(OkStrP);
	
	LokalTextAusgabeP(PSTR(Sprachwahl("\r\n fertig+++   \r\n", "\r\n config complete+++   \r\n")));

	} // Konfiguration()


//! Beendet die Selbstkonfiguration des Moduls.
//-----------------------------------------------
//! Arbeitet mit dem angeschlossenen Endgerät zusammen.

static void KonfigurationEnde()
	{
	SeriellHwHandshake = KonfigHwHandshake;

	KonfigSchreibeByte(EEAdr_BusEigenAdresse, BusEigenAdresse);

	KonfigSchreibeBool(EEAdr_UmleitungAbweisen, UmleitungAbweisen);

	KonfigSchreibeBool(EEAdr_MenueImmerAusgeben, MenueImmerAusgeben);

	KonfigSchreibeBool(EEAdr_SeriellHwHandshake, SeriellHwHandshake);

#ifdef AF_ZEITSPERRE
	SperrzeitSpeicherEeprom(EEAdr_Sperrzeiten);
#endif // def AF_ZEITSPERRE
	
	KonfigSchreibeString(EEAdr_Kennung, Kennung, sizeof(Kennung));
	
	KonfigSchreibeString(EEAdr_Kennwort, Kennwort, sizeof(Kennwort));

#ifdef AF_LOKALE_UHR
	if (!UhrzeitUeberBus)
		KonfigSchreibeByte(EEAdr_Minute, Minute);
#endif //def AF_LOKALE_UHR

#ifdef AF_TTYCODE_SWITCHABLE
	KonfigSchreibeByte(EEAdr_Zeichensatz, CodeIndex);
#endif //def AF_TTYCODE_SWITCHABLE
	
	Aktivieren(true);
	clr_LEDROT();
	}
	


#ifdef AF_MODULLISTE

//! Testfunktion zur Auflistung aller angeschlossenen Module	
static void BusteilnehmerListen()
	{
	LokalTextAusgabeP(PSTR(Sprachwahl("\r\nStatus der angeschlossenen Module:\r\n", "\r\nstatus of connected modules:\r\n")));
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

#endif //def AF_MODULLISTE


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

#ifdef AF_ANRUFSPEICHER
	extern uint16_t BeginnErsteMeldung2;
#endif //def AF_ANRUFSPEICHER

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
	init_LEDROT();
	init_LEDGELB();
	init_LEDGRUEN();
	init_LEDBLAU();
	init_SV_EIN(); // Benennung SV_EIN nur als "Verweis" auf Standard-Schnittstelle (TW39 / ED1000)
	init_TASTEEXT();

	set_LEDROT();

	init_SER_RTS();
	init_SER_CTS();

	//SET_BIT(TEST1_DDR, TEST1_BIT);

	// Timer initialisieren
	MsTimerInit();
	
	// Timer für Uhr
	TCCR1A = (0<<WGM11) | (0<<WGM10); // Clear on Timer Compare mit OCR1A
	TCCR1B = (0<<WGM13) | (1<<WGM12) | TIMER1_CS;
	OCR1A = TIMER1_OCRA; 
	//OCR1B = TIMER1_OCRB_INC; DoSwTwi wird jetzt direkt aufgerufen
	SET_BIT(TIMSK1, OCIE1A);
	//SET_BIT(TIMSK1, OCIE1B); DoSwTwi wird jetzt direkt aufgerufen

	StartTimer(&CheckCtsTimer);
	
#ifdef AF_ZEITSPERRE
	SperrzeitInit();
#endif //def AF_ZEITSPERRE

	pgm_read_byte(Identifier); // Dummy read to force the identifier to be placed in the FLASH.
	
	KonfigSpeicherInit();
	
	BusEigenAdresse = KonfigLeseByteBegrenzt(EEAdr_BusEigenAdresse, 31 << 1/*Standardwert*/, BusAdrMin, BusAdrMax) & 0xFE; // Bit 0 löschen
	BusEigenAdrMehrfach = 1;
	RundsendEmpfFreig = true;
	
	UmleitungAbweisen = KonfigLeseBool(EEAdr_UmleitungAbweisen, false);
	
#ifdef AF_LOKALE_UHR
	Jahr = KonfigLeseByteBegrenzt(EEAdr_Jahr, 20, 0, 99);
	Monat = KonfigLeseByteBegrenzt(EEAdr_Monat, 1, 1, 12);
	Tag = KonfigLeseByteBegrenzt(EEAdr_Tag, 1, 1,  31);
	Stunde = KonfigLeseByteBegrenzt(EEAdr_Stunde + Tag - 1, 0, 0, 23);
	Minute = KonfigLeseByteBegrenzt(EEAdr_Minute, 0, 0, 59);
#endif //def AF_LOKALE_UHR
	
	UhrzeitUeberBus = false;

	SeriellHwHandshake = KonfigLeseBool(EEAdr_SeriellHwHandshake, false);
	
	MenueImmerAusgeben = KonfigLeseBool(EEAdr_MenueImmerAusgeben, true);
		
	UhrAktualisieren();
	
	KonfigLeseString(EEAdr_Kennung, Kennung, sizeof(Kennung), PSTR("\r\ntxp-ab"));

	KonfigLeseString(EEAdr_Kennwort, Kennwort, sizeof(Kennwort), PSTR(Sprachwahl("kennwort", "password")));

#ifdef AF_ZEITSPERRE
	SperrzeitLadeEeprom(EEAdr_Sperrzeiten);
#endif //def AF_ZEITSPERRE

#ifdef AF_TTYCODE_SWITCHABLE
	CodeIndex = KonfigLeseByteBegrenzt(EEAdr_Zeichensatz, 0, 0, MAX_CODETAB_IDX);
#endif //def AF_TTYCODE_SWITCHABLE

#ifdef AF_ANRUFSPEICHER
	BeginnErsteMeldung2 = KonfigLeseWortBegrenzt(EEAdr_BeginnErsteMeldung2, 0, 0, 0xFFFF);
	//! \todo Prüfen auf Sinigkeit?
#endif //def AF_ANRUFSPEICHER

	SerIOInit();

	KommInit();

	sei();

	LokalTextAusgabeP(PSTR("\r\nSTART\r\nVersion " __DATE__ "/" __TIME__));

	TMsTimer Timer;
	StartTimer(&Timer);

	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 250)
		SeriellIO();

	clr_LEDROT();
	set_LEDGELB();

	TwiInit();

	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 500)
		SeriellIO();

	clr_LEDGELB();
	set_LEDGRUEN();

	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 750)
		SeriellIO();

	clr_LEDGRUEN();
	set_LEDBLAU();

#ifdef AF_ANRUFSPEICHER
	MsgSpeicherInit();
#endif //def AF_ANRUFSPEICHER

	// TWI nochmal resetten
	TWCR = (1<<TWINT) | (0<<TWEA) | (0<<TWSTA) | (1<<TWSTO) | (0<<TWEN) | (0<<TWIE);

	while (TimerVal(&Timer) < 760)
		SeriellIO();
		
	TWCR = (1<<TWINT) | (1<<TWEA) | (0<<TWSTA) | (0<<TWSTO) | (1<<TWEN) | (1<<TWIE);

	while (TimerVal(&Timer) < 1000 + BusEigenAdresse)
		SeriellIO();

	BusEigenAdressePruefenUndSetzen(BusEigenAdresse);

#ifdef AF_TTYCODE_SWITCHABLE
	CodeTabWechsel(CodeIndex);
#endif //def AF_TTYCODE_SWITCHABLE

	while (true) // Hauptschleife
		{
#ifdef AF_ANRUFSPEICHER
		extern uint16_t BeginnErsteMeldung;
		extern uint16_t EndeLetzteMeldung;
#endif //def AF_ANRUFSPEICHER
			
		// aktueller Zustand: Ausgeschaltet
		if (BusEigenAdresse == BusAdrUngueltig)
			{ // Noch nicht korrekt konfiguriert
			if (TimerVal(&Timer) <= 800)
				bset_LEDROT(TimerVal(&Timer) <= 400);
			else
				StartTimer(&Timer);
			clr_LEDGELB();
			clr_LEDGRUEN();
			clr_LEDBLAU();
			}
			
#ifdef AF_ANRUFSPEICHER
		else if (BeginnErsteMeldung != EndeLetzteMeldung) 		
			{ // Nachricht ungelesen 
			clr_LEDROT();
			clr_LEDGELB();
			if (TimerVal(&Timer) <= 1600)
				bset_LEDGRUEN(TimerVal(&Timer) <= 800);
			else
				StartTimer(&Timer);
			clr_LEDBLAU();
			}
#endif //def AF_ANRUFSPEICHER

		else
			{ // alles in Ordnung
			clr_LEDROT();
			clr_LEDGELB();
			clr_LEDGRUEN();
			clr_LEDBLAU();
			}
			
		DoSwTwi();

		if (HauptmenueAusgeben)
			{
			LokalTextAusgabeP(PSTR("\r\n"));

			if (Tag > 0)
				DatumAusgabe();

			LokalTextAusgabeP(PSTR(Sprachwahl("\r\nCtrl-A: Anwahl", "\r\nCtrl-A: dial/connect")));
			LokalTextAusgabeP(PSTR(Sprachwahl(", Ctrl-L: Lokalbetrieb", ", Ctrl-L: local mode")));
#ifdef AF_ANRUFSPEICHER
			LokalTextAusgabeP(PSTR(Sprachwahl(", Ctrl-Q: AB-Wiedergabe", ", Ctrl-Q: read messages")));
#endif //def AF_ANRUFSPEICHER
			LokalTextAusgabeP(PSTR(Sprachwahl("\r\nCtrl-K: Konfiguration", "\r\nCtrl-K: config")));
#ifdef AF_MODULLISTE
			LokalTextAusgabeP(PSTR(Sprachwahl(", Ctrl-T: Statusliste", ", Ctrl-T: list modules")));
#endif //def AF_MODULLISTE

#ifdef AF_TTYCODE_SWITCHABLE
			LokalTextAusgabeP(PSTR(Sprachwahl(", Ctrl-E: Zeichensatz", ", Ctrl-E: set encoding")));
#endif //def AF_TTYCODE_SWITCHABLE

#ifdef BUSDEBUG_DIALOG
			LokalTextAusgabeP(PSTR("\r\nCtrl-D: Debug"));
#endif
			LokalTextAusgabeP(PSTR(" --> "));
			HauptmenueAusgeben = false;
			}

		TastePruefen();
		SeriellIO(); 
		UhrAktualisieren();
		
		if (KoEinschalten())
			{
			VerbindungKommend();
#ifdef AF_ANRUFSPEICHER
			KonfigSchreibeWort(EEAdr_BeginnErsteMeldung2, BeginnErsteMeldung2);
#endif //def AF_ANRUFSPEICHER
			HauptmenueAusgeben = MenueImmerAusgeben;
			}
		
		if (!PufferLeer(&SerInBuf))
			{
			char c;
			c = SerEmpfZ(true);

			set_SV_EIN(); // Benennung SV_EIN nur als "Verweis" auf Standard-Schnittstelle (TW39 / ED1000)
			StartTimer(&NachlaufTimer);
			
			switch (c)
				{
				case CTRL('a'):
					if (BusEigenAdresse != BusAdrUngueltig)
						{
						VerbindungGehend();
#ifdef AF_ANRUFSPEICHER
						KonfigSchreibeWort(EEAdr_BeginnErsteMeldung2, BeginnErsteMeldung2);
#endif //def AF_ANRUFSPEICHER
						}
						
					else
						LokalTextAusgabeP(PSTR(Sprachwahl(" Fehler: nicht konfiguriert.", " Error: not configured")));
					HauptmenueAusgeben = MenueImmerAusgeben;
					break;
				
				case CTRL('k'):
					Konfiguration();
					KonfigurationEnde();
					HauptmenueAusgeben = MenueImmerAusgeben;
					break;

#ifdef AF_ANRUFSPEICHER
//				case CTRL('e'):
//					XEepromDebug();
//					HauptmenueAusgeben = MenueImmerAusgeben;
//					break;
			
				case CTRL('q'):
					Aktivieren(false);
					Wiedergabe(&LokalZeichenAusgabeKlar, 
							   &LokalTextAusgabeP,
							   &LokalEingabeErfolgt,
							   &LokalZeichenLesen);
					Aktivieren(true);
					KonfigSchreibeWort(EEAdr_BeginnErsteMeldung2, BeginnErsteMeldung2);
					HauptmenueAusgeben = MenueImmerAusgeben;
					break;

#endif //def AF_ANRUFSPEICHER

				case CTRL('l'): //Lokalbetrieb
					Aktivieren(false);
					LokalTextAusgabeP(PSTR(Sprachwahl("\r\nLokalbetrieb\r\n", "\r\nlocal mode\r\n")));
					while (true)
						{
						SeriellIO();
						if (!PufferLeer(&SerInBuf) && PufferAusg(&SerInBuf) == CTRL('s'))
							break;
						}
					Aktivieren(true);
					HauptmenueAusgeben = MenueImmerAusgeben;
					break;
					
#ifdef AF_RESET_CTRL_R
				case CTRL('r'):
					wdt_enable(WDTO_1S);
					cli();
					while (true)
						set_LEDBLAU();
					// wird beendet durch Watchdog-Reset
#endif //def AF_RESET_CTRL_R

#ifdef AF_MODULLISTE
				case CTRL('t'):
					Aktivieren(false);
					BusteilnehmerListen();
					Aktivieren(true);
					break;
#endif //def AF_MODULLISTE
					
#ifdef AF_TTYCODE_SWITCHABLE
				case CTRL('e'):
				{
					LokalTextAusgabeP(CodepageSelectStrP);
					if (LokalZahlEingabe(&CodeIndex, 1) < 0)
						break;
					
					if (CodeIndex > MAX_CODETAB_IDX)
						{
						LokalTextAusgabeP(PSTR(Sprachwahl("\r\nungueltige Eingabe", "\r\ninvalid selection")));
						break;
						}
						
					CodeTabWechsel(CodeIndex);
					break;
				}
#endif //def AF_TTYCODE_SWITCHABLE

				case CTRL('m'):
				case CTRL('j'):
				case ' ':
					HauptmenueAusgeben = MenueImmerAusgeben;
					// ansonsten ignorieren
					break;
				
				default:
					while (!PufferLeer(&SerInBuf))
						SerEmpfZ(true); // Puffer leeren
					LokalTextAusgabeP(PSTR(Sprachwahl("\r\nUngueltiges Kommando", "\r\ninvalid command")));			
					HauptmenueAusgeben = true;
					break;
				}

			StartTimer(&NachlaufTimer); // Ausschaltung erfolgt dann in der Hauptschleife
			
			} // if (!PufferLeer(&SerInBuf))
					
		if (Tastendruck == Lang)
			{
			Tastendruck = NichtGedr;
			Konfiguration();
			KonfigurationEnde();
			HauptmenueAusgeben = MenueImmerAusgeben;
			}

		else if (Tastendruck == Kurz)
			{
			Tastendruck = NichtGedr;
#ifdef AF_LOKALE_UHR
			if (!UhrzeitUeberBus)
				KonfigSchreibeByte(EEAdr_Minute, Minute);
#endif //def AF_LOKALE_UHR
			Deaktivieren();
			}

		#ifdef AF_ANRUFSPEICHER
		while (!EeAbschliessen())
			DoSwTwi(); // Eeprom-Zugriffe beenden...
		#endif //def AF_ANRUFSPEICHER
		
		// Parameter im eigenen Eeprom aktualisieren
/* TODO an bessere Stelle verschieben		
		if (!UhrzeitUeberBus)
			{
			eeprom_write_byte_noblock(&Jahr_EE, Jahr);
			eeprom_write_byte_noblock(&Monat_EE, Monat);
			eeprom_write_byte_noblock(&Tag_EE, Tag);
			eeprom_write_byte_noblock(&Stunde_EE[Tag-1], Stunde);
			}
		*/

		// Rundsendedaten auswerten:
		if (RundsendAnzDaten > 0)
			{
			if (LokalUhrPruefeRundsendung(RundsendDaten, RundsendAnzDaten))
				{
				#ifdef AF_ZEITSPERRE
				if (SperrzeitAktiv())
					KommendSperren(SperreZeit);
				#endif //def AF_ZEITSPERRE
				UhrzeitUeberBus = true;
				}
			else
				; // keine Ahnung, was hier gesendet wurde, ist aber auch egal...
				
			RundsendAnzDaten = 0;
			}
	
		if (get_TASTEEXT())
			{
			set_SV_EIN(); // Benennung SV_EIN nur als "Verweis" auf Standard-Schnittstelle (TW39 / ED1000)
			StartTimer(&NachlaufTimer);
			}
			
		if (TimerVal(&NachlaufTimer) > 60000)
			clr_SV_EIN(); // Benennung SV_EIN nur als "Verweis" auf Standard-Schnittstelle (TW39 / ED1000)
		
		} // while (1)
	} // main()


