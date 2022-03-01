//================================================================
// Schnittstelle für i-Telex-System für Hellschreiber (z.B. Hell GL72)
// für ATmega168 auf Platine Seriell+Spezial
// Teil Kommunikation
//================================================================
//				

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <inttypes.h>
#include <string.h>

#include "TwiEvents.h"
#include "Bits.h"
#include "timercs.h"

#include "TxP2-Defs.h"
#include "MsTimer.h"
#include "BusKomm.h"
#include "TxP2-Endgeraet.h"
#include "SeriellUmsetz.h"
#include "BaudotCode.h"
#include "KonfigDialog.h"
#include "KonfigSpeicher.h"
#include "LokalAusgabe.h"
#include "LokalUhr.h"
#include "Zeitsperre.h"

#include "PortsKomm.h"
#include "Taste.h"
#include "SwTwi.h"
#include "HellCodes.h" // Steuerzeichen auf der Schnittstelle zwischen Kommunikations-Prozessor und Signalprozessor.


#include "../SvnVersion.h"


// Schalter für Code-Varianten
// ===========================


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


#if (PLATINE_VERSION >= 20)
	
#ifdef PROGIDZUSATZ
//! Marker im Code als Identifikation
PROGMEM const char Identifier[] = "___itlx_HellschrKomm2-" PROGIDZUSATZ "___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#else
//! Marker im Code als Identifikation
PROGMEM const char Identifier[] = "___itlx_HellschrKomm2___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#endif

#else // PLATINE_VERSION < 20
	
#ifdef PROGIDZUSATZ
//! Identifikation im Programmspeicher
PROGMEM const char Identifier[] = "___itlx_HellschrKomm-" PROGIDZUSATZ "___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#else
//! Identifikation im Programmspeicher
PROGMEM const char Identifier[] = "___itlx_HellschrKomm___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#endif
	
#endif // PLATINE_VERSION


// #define LED_BLAU_INAKTIV


// Konstanten
// ----------

enum { LokalbetriebWahl_Std = 88 };
enum { KommendSperreWahl_Std = 0 };
enum { AutoWahlMaxZiffern = 10 }; // bei Änderung ist das EEPROM-Layout kompromittiert.
enum { AnrufAbbruchZeit_Std = 20 }; // sekunden


#define BUS_MEHRFACH_ADR 1
	// muss Zehnerpozenz von 2 sein

#if ((BUS_MEHRFACH_ADR - 1) & BUS_MEHRFACH_ADR) != 0
#error BUS_MEHRFACH_ADR muss 1, 2, 4, 8 ... sein

#endif


// sonstige Konstanten
// -------------------

#define KENNUNG_MAXLEN 20 //!< maximale Länge des Kennungsgebers.


// Eeprom-Speicher-Adressen
// ------------------------

enum {
	EEAdr_BusEigenAdresse = 0,
    EEAdr_KommendSperreWahl = 3,
    EEAdr_Sperrzeit = 4, // Beansprucht 20 Bytes
    EEAdr_TasteFunktion = 24,
    EEAdr_UmleitungAbweisen = 25,
    EEAdr_LokalbetriebWahl = 26,
    EEAdr_AutoWahlZiffern = 27,
	EEAdr_AnrufAbbruchZeit = 37,
	//EEAdr_StartQuittVerz = 38,
	EEAdr_Kennung = 39, // Beansprucht 20 Bytes
	EEAdr_Ende = 59 // Platz für neue Werte, darf erhöht werden	
};


// Typen
// -----

typedef enum { SperreTaste, SperreStoerungHell, SperreStoerungIntern, SperreZeit, SperreWahl } TSperreGrund;

typedef char TKennung[KENNUNG_MAXLEN];

// Konfigurations-Variablen
// ====================

uint8_t AnrufAbbruchZeit; //!< Maximale Zeit zwichen Aktivierung Anrufsignal und Ende des Hochlaufs des Fernschreibers

uint8_t KommendSperreWahl; //!< Welche Wahlnummer sperrt den Anschluss für ankommende Rufe

uint8_t LokalbetriebWahl; //!< Welche Wahlnummer aktiviert den simulieren Lokalbetrieb

uint8_t AutoWahlZiffern[AutoWahlMaxZiffern];
	//!< bei gehender Aktivierung wird sofort diese Nummer gewählt

typedef enum { Deaktivierung, DemoBetriebStarten, ExtStromEinschalten } TTasteFunktion;

TTasteFunktion TasteFunktion; //!< Bisher möglich: 0 = deaktivierung, 1 = Demo-Betrieb, 2 = Ausgang zum externen Schalter aktivieren

TKennung Kennung; //!< Eigene Kennungen, da kein echter Fernschreiber angeschlossen.

TMsTimer NachlaufTimer; //!< Steuert nur den Ausgang für den externen SV-Schalter


// Arbeits-Variablen 
// =======================

// Einstell-Modus
bool WarteKonfig; // TODO noch nicht implementiert

TPuffer KommInBuf; //!< Empfangspuffer für die Schnittstelle zum Signalprozessor. Nicht identisch mit Puffer für Baudot-Ein/-Ausgabe.
TPuffer KommOutBuf; //!< Sendepuffer für die Schnittstelle zum Signalprozessor. Nicht identisch mit Puffer für Baudot-Ein/-Ausgabe.

uint8_t HellStatus; //!< Aktuelle Zustandsmeldung des Hellschreibers

enum { Aus, Ein, KdoEin, MeldEin, KdoAus, MeldAus } HellBetrieb;

TMsTimer HellSignalStoerung; // steuert die Rote LED entsprechend des Bits HellStatBitEmpfStoer

bool LedRotEin; // wenn rot unabhängig von Störungen leuchten soll.


#ifndef KOMMSER

T_SwTwiTransferdaten SignalTwiDat;

uint8_t TwiPuffer;

uint8_t SignalTwiFehlerzaehler;

uint16_t GesamtTwiFehlerzaehler; // wird nie zurückgesetzt

enum { SignalTwiFehlerzaehlerMax = 50 };

#endif //ndef KOMMSER
	
TPuffer DebugOutBuf;

#ifdef DEBUGSER

bool DebugKommProt = false; // wird true, wenn alle TWI-IO-Aktionen protokolliert werden sollen.

#endif //def DEBUGSER


// Schnittstellen-Spezifische Funktionen
// =====================================

//! bedient Hardware-IO entsprechend der aktuellen Zustände.

//! Hier ist es nur das Schreiben und Lesen auf der Seriellen Schnittstelle


//! Initialisiert serielle Schnittstelle.
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


void DebugAusg(char c)
	{
	PufferSpeich(&DebugOutBuf, c);
	}


void DebugAusgZahl(int i)
	{
	if (i < 0)
		{
		DebugAusg('-');
		DebugAusgZahl(-i);
		}
	else
		{
		if (i >= 10)
			{
			int z = i / 10;
			DebugAusgZahl(z);
			i -= 10 * z;
			}
		DebugAusg('0' + i);
		}
	}


void DebugAusgPStr(const char *s)
	{
	char c;
	while ((c = pgm_read_byte(s)) != '\0')
		{
		DebugAusg(c);
		s++;
		}
	}


static void HellschreiberMeldetLaeuft()
	{
	switch (HellBetrieb)
		{
		case Aus:
			HellBetrieb = MeldEin;
			break;
		case KdoEin:
		case MeldAus:
			HellBetrieb = Ein;
			break;
		default:
			break;
		}
	}


static void HellschreiberMeldetSteht()
	{
	switch (HellBetrieb)
		{
		case Ein:
			HellBetrieb = MeldAus;
			break;
		case KdoAus:
		case MeldEin:
			HellBetrieb = Aus;
			break;
		default:
			break;
		}
	}


static void FernschrIO()
	{
	// Empfang von Codes und Zeichen vom Signalprozessor verarbeiten:	

	static TMsTimer KdoTimer;

	if (PufferLeer(&KommOutBuf) && TimerVal(&KdoTimer) >= 800)
		{
		switch (HellBetrieb)
			{
			case KdoEin:
				PufferSpeich(&KommOutBuf, HellEinschaltBefehl);
				StartTimer(&KdoTimer);
				break;

			case KdoAus:
				PufferSpeich(&KommOutBuf, HellAusschaltBefehl);
				StartTimer(&KdoTimer);
				break;

			default:
				break;

			} // switch (HellBetrieb)
		}


#ifdef KOMMSER

	if (BIT_IS_SET(UCSR0A, RXC0))
		{ // Zeichen empfangen
		char c = UDR0;
		if (c == HellEinschaltBefehl) // eigentlich Meldungen, ist mir aber jetzt egal
			HellschreiberMeldetLaeuft();

		else if (c == HellAusschaltBefehl) // eigentlich Meldungen, ist mir aber jetzt egal
			HellschreiberMeldetSteht();
			
		else // normales Zeichen -> Puffern
			{
			if (PufferAnzahl(&KommInBuf) < MaxPuffer - 2)
				{
				if (c == '%')
					c = CodeChrKlingel;
				PufferSpeich(&KommInBuf, c); 
				}
			} // PufferAnzahl(&KommInBuf) < MaxPuffer - 2
			
		} // Serielles Zeichen empfangen

	// Zeichen senden wenn Schnittstelle bereit.
	if (BIT_IS_SET(UCSR0A, UDRE0) && !PufferLeer(&KommOutBuf))
		UDR0 = PufferAusg(&KommOutBuf);

#else //not def KOMMSER

	SwTwiAktion(&SignalTwiDat);

	if (SignalTwiDat.Phase == 0)
		{ // Bereit für einen neuen Transfer
		if (!PufferLeer(&KommOutBuf) && !BIT_IS_SET(HellStatus, HellStatBitPufferVoll)) 
			{ // Auf TWI schreiben
			TwiPuffer = PufferZeig(&KommOutBuf); // erst löschen, wenn Übertragung erfolgreich war, daher PufferZeig() statt PufferAusg()
			SignalTwiDat.Adresse = HellTwiAdresse << 1;
			SignalTwiDat.Puffer = &TwiPuffer;
			SignalTwiDat.AnzDaten = 1;
#ifdef DEBUGSER
			if (DebugKommProt)
				{
				DebugAusg('>');
				DebugAusg(TwiPuffer);
				}
#endif //def DEBUGSER
			}
		else if (!PufferVoll(&KommInBuf)) // nur lesen, wenn auch Platz ist
			{ // von TWI lesen
			SignalTwiDat.Adresse = (HellTwiAdresse << 1) + 1;
			SignalTwiDat.Puffer = &TwiPuffer;
			SignalTwiDat.AnzDaten = 1;
			}
		}

	else if (SignalTwiDat.Phase == 255)
		{ // Transfer ist abgeschlossen, jetzt auswerten
		if (SignalTwiDat.Ergebnis != SignalTwiDat.AnzDaten + 1) 
			{ // Fehler
			if (SignalTwiFehlerzaehler < SignalTwiFehlerzaehlerMax)
				SignalTwiFehlerzaehler++;
			else
				{
				HellStatus = 0; // ausgeschalteten Zustand im Fehlerfall annehmen.
				if (HellBetrieb != Aus)
					HellBetrieb = MeldAus;
				}

			GesamtTwiFehlerzaehler++;

			StartTimer(&HellSignalStoerung);
			set_LEDROT();
			
#ifdef DEBUGSER
			if (DebugKommProt)
				{
				DebugAusg('#');
				DebugAusgZahl(SignalTwiDat.Adresse);
				DebugAusg(':');
				DebugAusgZahl(SignalTwiDat.Ergebnis);
				DebugAusg(' ');
				}
#endif //def DEBUGSER

			SignalTwiDat.AnzDaten = 0;
			SignalTwiDat.Adresse = 0;
			SignalTwiDat.Phase = SwTwiRecoverPhase; // Versuch einen Slave im falschen Zustand zurückzusetzen.

			set_DiagC();
			clr_DiagA();
			clr_DiagB();
			} // TWI Fehler

		else // letzter TWI Zugriff erfolgreich
			{
			clr_DiagA();
			clr_DiagB();
			clr_DiagC();

			if (SignalTwiFehlerzaehler > 0)
				SignalTwiFehlerzaehler--;

			if (SignalTwiDat.Adresse == (HellTwiAdresse << 1) + 1) 
				{ // das war ein Lesevorgang
				if (!BIT_IS_SET(TwiPuffer, HellStatBitStatFlag))
					{
					PufferSpeich(&KommInBuf, TwiPuffer);
#ifdef DEBUGSER
					if (DebugKommProt)
						{
						DebugAusg('<');
						DebugAusg(TwiPuffer);
						}
#endif //def DEBUGSER
					}
				else
					{ // Status-Meldung...
					HellStatus = TwiPuffer & ~(1 << HellStatBitStatFlag); // Bit 7 löschen

#ifdef DEBUGSER
					if (DebugKommProt && PufferAnzahl(&DebugOutBuf) < MaxPuffer - 20)
						{
						static uint8_t DebugLetztMeldStatus = 0;
						if (HellStatus != DebugLetztMeldStatus)
							{
							DebugAusg(':');
							DebugAusgZahl(HellStatus);
							DebugLetztMeldStatus = HellStatus;
							}
						}
#endif //def DEBUGSER

					if (BIT_IS_SET(HellStatus, HellStatBitLaeuft))
						HellschreiberMeldetLaeuft();
					else
						HellschreiberMeldetSteht();

					if (BIT_IS_SET(HellStatus, HellStatBitEmpfStoer))
						StartTimer(&HellSignalStoerung);

					} // if Statusmeldung (also Bit 7 gesetzt)
				set_DiagA();
				SignalTwiDat.AnzDaten = 0;
				} // if Lesevorgang
			else 
				{ // es war ein Schreibvorgang oder ein Fehler-Rücksetz-Vorgang, da gibt es nichts auszuwerten, außer Fehlermeldungen (siehe oben)
				set_DiagB();
				
				if (SignalTwiDat.Adresse == (HellTwiAdresse << 1))
					PufferAusg(&KommOutBuf); // erfolgreich gesendetes Zeichen aus dem Puffer löschen.
									
				// aber als nächstes unbedingt einen Lesevorgang einschieben
				if (!PufferVoll(&KommInBuf)) // allerdings nur, wenn auch Platz ist
					{ // von TWI lesen
					SignalTwiDat.Adresse = (HellTwiAdresse << 1) + 1;
					SignalTwiDat.Puffer = &TwiPuffer;
					SignalTwiDat.AnzDaten = 1;
					}
				else
					{ // erstmal alles erledigt.
					SignalTwiDat.AnzDaten = 0;
					}
				}
			SignalTwiDat.Phase = 0;
			} // letzter TWI Zugriff erfolgreich

		bseto_LEDBLAU(BIT_IS_SET(HellStatus, HellStatBitEmpfTon));

		if (BIT_IS_SET(Status, StatBit_AngerufenBelegt))
			bset_LEDGELB(BIT_IS_SET(HellStatus, HellStatBitSendeTon));
		else // !BIT_IS_SET(Status, StatBit_AngerufenBelegt))
			bset_LEDGRUEN(BIT_IS_SET(HellStatus, HellStatBitSendeTon));

		if (BIT_IS_SET(HellStatus, HellStatBitEmpfStoer))
			{
			StartTimer(&HellSignalStoerung);
			set_LEDROT();
			}
		else 
			bset_LEDROT(LedRotEin || TimerVal(&HellSignalStoerung) < 150);
			
		} // if (SignalTwiDat.Phase == 255)
	
#endif //ndef KOMMSER

#ifdef DEBUGSER

	if (BIT_IS_SET(UCSR0A, RXC0))
		{ // Zeichen empfangen
		DebugKommProt = false;
		uint8_t c = UDR0;
		switch (c)
			{
			case 's':
				DebugAusgPStr(PSTR("HellStatus: "));
				DebugAusgZahl(HellStatus);
				break;
			
			case 'b':
				DebugAusgPStr(PSTR("HellBetrieb: "));
				DebugAusgZahl(HellBetrieb);
				break;
			
			case 't':
				DebugAusgPStr(PSTR("SignalTwiDat: Phase="));
				DebugAusgZahl(SignalTwiDat.Phase);
				DebugAusgPStr(PSTR("  Adresse="));
				DebugAusgZahl(SignalTwiDat.Adresse);
				DebugAusgPStr(PSTR("  AnzDaten="));
				DebugAusgZahl(SignalTwiDat.AnzDaten);
				DebugAusgPStr(PSTR("  Ergebnis="));
				DebugAusgZahl(SignalTwiDat.Ergebnis);
				DebugAusgPStr(PSTR("  Fehlerz="));
				DebugAusgZahl(SignalTwiFehlerzaehler);
				DebugAusgPStr(PSTR("  Gesamtf="));
				DebugAusgZahl(GesamtTwiFehlerzaehler);
				break;
			
			case 'p':
				DebugKommProt = true;
				DebugAusgPStr(PSTR("Protokoll EIN"));
				break;
			
			case 'i':
				DebugAusgPStr(PSTR("BufferIn Content:"));
				for (uint8_t i = KommInBuf.AusgP ; i != KommInBuf.SpeichP && i - MaxPuffer != KommInBuf.SpeichP ; i++)
					{
					if (i >= MaxPuffer)
						i -= MaxPuffer;
					DebugAusg(' ');
					DebugAusgZahl(KommInBuf.Puffer[i]);
					}
				break;
				
			default:
				DebugAusgPStr(PSTR("unbekannt: "));
				DebugAusgZahl(c);
				DebugAusg(' ');
				DebugAusg(c);
				break;
				
			}
		DebugAusgPStr(PSTR("\r\n"));
		} // Serielles Zeichen empfangen

#endif //def DEBUGSER

	// Zeichen senden wenn Schnittstelle bereit.
	if (BIT_IS_SET(UCSR0A, UDRE0) && !PufferLeer(&DebugOutBuf))
		UDR0 = PufferAusg(&DebugOutBuf);
		
	} // FernschrIO


//! Wartet, bis ein Zeichen über die serielle Schnittstelle angekommen ist.
//-------------------------------------------------------------------------
//! Alle erforderlichen Routinefunktionen werden aufgerufen.
//! Falls gefüllt, wird auch das nächste Zeichen aus dem Empfangspuffer verwendet.
//! \returns Das nächste Zeichen.
char LokalZeichenLesen()
	{
	while (PufferLeer(&KommInBuf))
		{
		FernschrIO();
		TastePruefen();
		if (Tastendruck != NichtGedr)
			{
			Tastendruck = NichtGedr;
			return '\t';
			}
		if (HellBetrieb != Ein)
			return '\0';
		}
		
	char c;
	c = PufferAusg(&KommInBuf);
	if (c >= 'A' && c <= 'Z')
		c += 'a'-'A'; // zum Kleinbuchstaben umwandeln
	return c;
	}
	
	
//! Ein Zeichen auf der seriellen Schnittstelle ausgeben.
//-------------------------------------------------------
//! Wenn Ausgabepuffer voll, blockiert diese Funktion, erledigt aber die 
//! Routinefunktionen.
//! \param c Das auszugebende Zeichen.

void LokalZeichenAusgabe(char c)
	{
	while (PufferVoll(&KommOutBuf) && HellBetrieb == Ein)
		{
		FernschrIO();
		TastePruefen();
		}

	PufferSpeich(&KommOutBuf, c); 
	}

	
//////////////////////////////////////////////////////////////////

// Allgemeine Funktionen
// =====================
	

//! Einschaltung des Fs auslösen.
//-------------------------------
//! \returns Einschaltung wurde erfolgreich durch Endgerät quittiert.

static bool FernschrEinschalten(bool WarteQuittVerz)
	{
	TMsTimer AbbruchTimer;
	
	if (HellBetrieb == Aus || HellBetrieb == KdoAus || HellBetrieb == MeldAus)
		HellBetrieb = KdoEin;

	PufferInit(&KommInBuf);
	PufferInit(&KommOutBuf);
	StartTimer(&AbbruchTimer);
	
	while (HellBetrieb != Ein)
		{
		FernschrIO();
		if (HellBetrieb == MeldAus || HellBetrieb == Aus) // letzteres passiert nur, wenn die Einschaltung vom Anrufer zurückgenommen wird.
			return false;

		if (TimerVal(&AbbruchTimer) > 1000 * AnrufAbbruchZeit)
			{
			HellBetrieb = Aus;
			return false;
			}
		}

	return true;
	}
		

//////////////////////////////////////////////////////////////////

//! Ausschaltung des Fs auslösen.

static void FernschrAusschalten()
	{
	if (HellBetrieb == MeldAus)
		HellBetrieb = Aus;

	else if (HellBetrieb == Ein || HellBetrieb == KdoEin || HellBetrieb == MeldEin)
		HellBetrieb = KdoAus;

	while (HellBetrieb != Aus)
		{
		FernschrIO();
		if (HellBetrieb == MeldEin)
			HellBetrieb = KdoAus;
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
		
	

//////////////////////////////////////////////////////////////////////////////////////////

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
	// 11: Fehler auf dem internen TWI-Bus
	{
	TWCR = (1<<TWINT) | (0<<TWEA) | (0<<TWSTA) | (0<<TWSTO) | (0<<TWEN) | (0<<TWIE);

	clr_SV_EIN();
	
	uint8_t TasteZ = 0;
	bool TasteWirk = false;
	TMsTimer TasteTimer;
	StartTimer(&TasteTimer);
	while (1)
		{
		wdt_reset();

		if (TimerVal(&TasteTimer) > 400)
			{ // alle 400 ms:
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
#ifndef LED_BLAU_INAKTIV
		    if (BIT_IS_SET(Nummer, 3))
				seto_LEDBLAU();
			else
				inp_LEDBLAU();
#endif
			}
		else
			{
			clr_LEDROT();
			clr_LEDGELB();
			clr_LEDGRUEN();
#ifndef LED_BLAU_INAKTIV
			inp_LEDBLAU();
#endif //ndef LED_BLAU_INAKTIV
			}
		}
	}	


//! Sendet ein Zeichen an die Gegenstelle, wartet aber bei vollem Puffer
/*
static void ZeichenSenden(char c)
	{
	while (!GeSendePufferLeer())
		{
		FernschrIO();
		if (KoAusschalten())
			return;
		LEDAktualisieren();
		}
	while (!GeSendeZeichen(c))
		; // kann eigentlich nicht lange dauern
	}
*/

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

	// HellBetrieb wird durch FernschrEinschalten gesetzt
			
	if (!FernschrEinschalten(true))
		{ // Timeout...
		clr_LEDGRUEN();
		GeAusschalten(true);
		KommendSperren(SperreStoerungHell);
		LedRotEin = false;		
		return;
		}

	LedRotEin = false;		
	
	if (!GeEinschaltQuittung())
		GeAusschalten(true);
	else
		VerbindungSteht(false); // Keine automatische Kennungsgeber-Abfrage

	FernschrAusschalten();
	clr_LEDGRUEN();
	
	}
	

	
/////////////////////////////////////////////////////////////

//! Wird aufgerufen, wenn durch Wahl der entsprechenden Nummer oder
//! durch Buchstabe "L" bei Tastaturwahl ein Lokalbetrieb laufen soll.

static void LokalbetriebSimulieren()
	{
	Aktivieren(false);
	
	LokalTextAusgabeP(PSTR("loc  "));

	while (HellBetrieb == Ein)
		{
		FernschrIO();
		}

	FernschrAusschalten();
	Aktivieren(true);
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
	
	LedRotEin = false;
	Aktivieren(true);
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

	LokalZeichenAusgabe(' ');
	LokalZeichenAusgabe(' ');
	LokalZeichenAusgabe(HELLC_PFEIL);
	LokalZeichenAusgabe(HELLC_PFEIL);
	LokalZeichenAusgabe(' ');
	LokalZeichenAusgabe(' ');
	LokalZeichenAusgabe(' ');
	
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
		
		if (!PufferLeer(&KommInBuf))
			{
			c = PufferAusg(&KommInBuf);
			// Korrekturen aufgrund potenzieller Zeichenverfälschung
			switch (c)
				{
				case 'O': c = '0'; break;
				case 'I': c = '1'; break;
				case 'S': c = '5'; break;
				case 'G': c = '6'; break;
				case 'B': c = '8'; break;
				case 'R': c = '8'; break;
				}
			
			if (c >= '0' && c <= '9')
				{
				GeWaehlen(c - '0');
				EsWurdeGewaehlt = true;
				}
			else if (c == 'L' && !EsWurdeGewaehlt)
				{ 
				LokalbetriebSimulieren();
				return false;
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

		if (KoEinschalten())
			{
			return true;
			}

		if (TimerVal(&WahlendeTimer) > (EsWurdeGewaehlt ? 45000 : 15000)) // 15 / 45 Sekunden nicht gewählt
			{ // auf das Ausschalten durch die Schlusstaste warten
			AbschaltungZuLangeWahlpause(EsWurdeGewaehlt);
			return false;
			}

		if (HellBetrieb == MeldAus)
			// Abbruch durch Bediener
			{
			HellBetrieb = Aus;
			return false;
			}

		if (KoAusschalten())
			{
			// FernschrAusschalten() macht die aufrufende Routine
			LokalZeichenAusgabe(' ');
			LokalZeichenAusgabe(HELLC_ENDE);
			LokalZeichenAusgabe(' ');
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
	
	HellBetrieb = Ein;

	Verbunden = WahlMitTastatur();

	if (!Verbunden)
		{
		GeAusschalten(true);
		
		if (KommendSperreWahl != 0 && LetzteInterneWahl() == KommendSperreWahl)
			{
			clr_LEDGELB();
			FernschrAusschalten();
			KommendSperren(SperreWahl); // kehrt erst zurück, wenn Sperre aufgehoben wird.
			return; // damit am Ende nicht FernschrAusschalten() aufgerufen wird, da ein neuer gehender Anruf bereits eingeleitet sein könnte.
			}
			
		else if ((LokalbetriebWahl != 0 && LetzteInterneWahl() == LokalbetriebWahl)
			|| LetzteInterneWahl() == (BusEigenAdresse >> 1))
			{
			LokalbetriebSimulieren();
			}
		}

	else // Verbunden = true
		{ 
		if (GeEinschaltQuittung())  // endgültige Einschaltung bestätigen
			VerbindungSteht(true); 
		else
			GeAusschalten(true);
		}

	FernschrAusschalten();
		// doppelter Aufruf ist unschädlich.
		
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
	bool ErsteKennungAbfrage;
	bool EscapeMode; // Wird durch die Dauertaste gesetzt.
	bool AutoNewline; // kompensiert das fehlende WR / ZL
	uint8_t ZeilePosition;
	
	StartTimer(&KennungAbfrageTimer);
	ErsteKennungAbfrage = true;
	EscapeMode = false;
	AutoNewline = true;
	ZeilePosition = 0;

	GeSendeMark(true); 

	while (true)
		{
		FernschrIO();
		TastePruefen();

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
			
		char c;
		if (KoEmpfZeichen(&c))
			{
			if (c == CodeChrWerDa)
				{
				GeSendeText(Kennung);
				LokalZeichenAusgabe(HELLC_WERDA);
				LokalTextAusgabe(Kennung + 2); // WR+ZL überspringen
				ZeilePosition = strlen(Kennung);
				}
			else
				{
				AutoKennungAbfrage = false;
				if (c == CodeChrKlingel)
					c = HELLC_KLINGEL;
				if (!PufferVoll(&KommOutBuf))
					LokalZeichenAusgabe(c);
				if (c == '\r')
					ZeilePosition = 0;
				else
					ZeilePosition++;
				}
			}

		if (KoAusschalten())
			{
			LokalZeichenAusgabe(' ');
			LokalZeichenAusgabe(HELLC_ENDE);
			LokalZeichenAusgabe(' ');
			GeAusschalten(true);

			HellBetrieb = KdoAus;

			return;
			}

		if (!PufferLeer(&KommInBuf) && GeSendePufferLeer())
			{
			char c;
			c = PufferAusg(&KommInBuf);
			
			if (EscapeMode)
				{
				EscapeMode = false; // vorbereitend
				switch (c)
					{
					case HELLC_BREAK1 ... HELLC_BREAK6: // weiterhin ein
					case ' ':
						EscapeMode = true; 
						break;

					case 'N':
						GeSendeCode(TtyCodeWR);
						GeSendeCode(TtyCodeZL);
						LokalZeichenAusgabe(' ');
						ZeilePosition = 0;
						break;

					case 'R':
						GeSendeCode(TtyCodeWR);
						LokalZeichenAusgabe(' ');
						ZeilePosition = 0;
						break;

					case 'Z':
						GeSendeCode(TtyCodeZL);
						LokalZeichenAusgabe(' ');
						break;

					case 'H': // hier ist
					case 'I': // ich bin
						GeSendeText(Kennung);
						LokalZeichenAusgabe(HELLC_RAUTE); 
						LokalTextAusgabe(Kennung + 2); // WR+ZL überspringen
						ZeilePosition = strlen(Kennung);
						break;

					case 'W':
						GeSendeZeichen(CodeChrWerDa);
						LokalZeichenAusgabe(HELLC_WERDA); 
						break;

					case 'K':
						GeSendeZeichen(CodeChrKlingel);
						LokalZeichenAusgabe(HELLC_KLINGEL);
						ZeilePosition++;
						break;

					case 'S': // wie Streifenschreiber
						AutoNewline = false; 
						break;

					case 'B': // wie Blattschreiber
						AutoNewline = true;
						break;

					default:
						// nix
						break;
					} // switch c
					
#ifdef DEBUGSER
				if (!EscapeMode)
					DebugAusgPStr(PSTR("Escape aus\r\n"));
#endif //def DEBUGSER
					
				}
			else if (c >= HELLC_BREAK1 && c <= HELLC_BREAK6)
				{
#ifdef DEBUGSER
				DebugAusgPStr(PSTR("Escape ein\r\n"));
#endif //def DEBUGSER
				EscapeMode = true;
				}
			else // !EscapeMode
				{
				bool JetztNeueZeile = false;

				if (AutoNewline)
					{
					if (c == ' ' && ZeilePosition >= 55)
						JetztNeueZeile = true, c = '\0'; // unterdrückt das Senden des ursprünglichen Zeichens
					else if (c == '-' && ZeilePosition >= 55)
						JetztNeueZeile = true;
					else if (ZeilePosition >= 68)
						JetztNeueZeile = true;
					}

				if (JetztNeueZeile)
					{
#ifdef DEBUGSER
					DebugAusgPStr(PSTR("Auto WR ZL\r\n"));
#endif //def DEBUGSER
					GeSendeCode(TtyCodeWR);
					GeSendeCode(TtyCodeZL);
					ZeilePosition = 0;
					}

				if (c != '\0')
					{
					GeSendeZeichen(c);
					ZeilePosition++;
					}
				} // else !EscapeMode

			AutoKennungAbfrage = false;
			} // if !PufferLeer(&KommInBuf)

		if (HellBetrieb == MeldAus)
			// Abbruch durch Bediener
			{
			HellBetrieb = Aus;

			GeAusschalten(false);
			while (!KoAusschalten())
				{
				FernschrIO();
				}

			return;
			}

		}

	} // VerbindungSteht()


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
	
	p = DemoText;
	
	while (pgm_read_byte(p) != '\0')
		{
		LokalZeichenAusgabe(pgm_read_byte(p));	
			// macht intern FernschrIO also auch Schlusstaste-Erkennung
		p++;
		TastePruefen();
		if (Tastendruck != NichtGedr)
			break;
		if (HellBetrieb != Ein)
			break;
		}

	Tastendruck = NichtGedr;
	FernschrAusschalten();
	Aktivieren(true);
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
	
/* für später mit externer Konfiguration bei Nur-Schreib-Geräten (Tempf39)
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
*/

	set_LEDGELB(); 

	Aktivieren(false);
	
	if (!FernschrEinschalten(true))
		return;
	
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR(" hellschreiber version " SVNVERSION " date " __DATE__));
#else
	LokalTextAusgabeP(PSTR(" hellschreiber version " SVNVERSION " datum " __DATE__));
#endif //def SPRACHE_EN

// zum "Trainieren" zunächst Zeichen nur 'echoen'

#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR(" training phase, start configuration with +++   "));
#else
	LokalTextAusgabeP(PSTR(" lernphase, konfiguration beginnen mit +++   "));
#endif //def SPRACHE_EN

	int PlusZahl = 0;
	while (PlusZahl < 3)
		{
		char Zeichen = LokalZeichenLesen();
		if (Zeichen == '+')
			PlusZahl++;
		else if (Zeichen == '\0') // ausgeschaltet
			return;
		else 
			{
			PlusZahl = 0; 
			if (Zeichen == '0')
				LokalZeichenAusgabe('!');	
			else
				LokalZeichenAusgabe(Zeichen);	
			}
		}
		
	// Vorab die Frage nach "Expertenfunktionen"
	// -----------------------------------------
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR(" \025 simple configuration?    "));  // 025 oktal = 021 dezimal = Raute
#else	
	LokalTextAusgabeP(PSTR(" \025 einfache konfiguration?    ")); 
#endif //def SPRACHE_EN

	if (LokalBoolEingabe(&NoExpertSettings) == 0)
		return;

	LokalTextAusgabeP(OkStrP);

	// Durchwahl und co.
	// -----------------
	Abbruch = !KonfigurationAllgemein(); 
	if (Abbruch) 
		return;
		
#ifdef SPRACHE_EN	
	LokalTextAusgabeP(PSTR(" \025 answerback: "));
#else
	LokalTextAusgabeP(PSTR(" \025 kennung: "));
#endif	
	Kennung[0] = '\r';
	Kennung[1] = '\n';
	LokalTextAusgabe(Kennung + 2); // CR + LF weglassen
	LokalTextAusgabeP(NeuStrP);
	if (LokalTextEingabe(Kennung + 2, KENNUNG_MAXLEN - 3) == 0) // erste 2 Zeichen für CRLF reserviert
		return;

	// jetzt bei einfacher Konfiguration abbrechen
	// -------------------------------------------
	if (NoExpertSettings)
		{
		KommendSperreWahl = KommendSperreWahl_Std;
		LokalbetriebWahl = LokalbetriebWahl_Std;
		AnrufAbbruchZeit = AnrufAbbruchZeit_Std;
		SperrzeitInit();
		TasteFunktion = 0;
		AutoWahlZiffern[0] = 255; // Ende-Kennzeichen
		LokalTextAusgabeP(PSTR(" +++ "));
		LokalZeichenAusgabe(HELLC_ENDE);
		LokalZeichenAusgabe(' ');
		return;
		}

	// Einschaltung der Sperre für kommende Rufe durch Wahl von...
	// -----------------------------------------------------------
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR(" \025 block incoming calls by: (cur. "));
#else
	LokalTextAusgabeP(PSTR(" \025 kommende anrufe sperren mit: (akt. "));
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
	LokalTextAusgabeP(PSTR(") new (0 = off): "));
#else
	LokalTextAusgabeP(PSTR(") neu (0 = aus): "));
#endif //def SPRACHE_EN

	if (LokalZahlEingabe(&KommendSperreWahl, 2) < 0)
		return;

	LokalTextAusgabeP(OkStrP);
	
	// Lokalbetrieb durch Wahl von...
	// ------------------------------
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR(" \025 local operation by number: (cur. "));
#else
	LokalTextAusgabeP(PSTR(" \025 lokalbetrieb waehlen mit: (akt. "));
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
	LokalTextAusgabeP(PSTR(") new (0 = off): "));
#else
	LokalTextAusgabeP(PSTR(") neu (0 = aus): "));
#endif //def SPRACHE_EN

	if (LokalZahlEingabe(&LokalbetriebWahl, 2) < 0)
		return;

	LokalTextAusgabeP(OkStrP);
	
	// Sperrzeiten
	// -----------
	if (!SperrzeitEingabeDialog())
		return;
	
	// Feste Verbindung
	// ----------------
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR(" \025 activate automated prefix dialing? current: ")); 
#else	
	LokalTextAusgabeP(PSTR(" \025 automatische vorwahl aktivieren? aktuell: ")); 
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
		LokalTextAusgabeP(PSTR(" \025 enter dialing digits, finish with + (cur.: "));
#else		
		LokalTextAusgabeP(PSTR(" \025 wahlziffern eingeben, ende mit + (akt.: "));
#endif //def SPRACHE_EN
		uint8_t i;
		
		for (i = 0 ; i < AutoWahlMaxZiffern ; i++)
			if (AutoWahlZiffern[i] <= 9)
				LokalZahlAusgabe(AutoWahlZiffern[i], 0);
			else
				break;
			
		LokalTextAusgabeP(PSTR("+)"));
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
			else if (c == '\0') // abbruch
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
	LokalTextAusgabeP(PSTR(" \025 timeout for incoming calls in seconds (10-30, cur. "));
#else
	LokalTextAusgabeP(PSTR(" \025 maximale hochlauf-zeit in sekunden (10-30, akt. "));
#endif //def SPRACHE_EN

	LokalZahlAusgabe(AnrufAbbruchZeit, 0);

#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR(") new:  "));
#else
	LokalTextAusgabeP(PSTR(") neu:  "));
#endif //def SPRACHE_EN

	if (LokalZahlEingabe(&AnrufAbbruchZeit, 0) < 0)
		return;
	if (AnrufAbbruchZeit < 10)
		AnrufAbbruchZeit = 10;
	else if (AnrufAbbruchZeit > 30)
		AnrufAbbruchZeit = 30;

	LokalTextAusgabeP(OkStrP);
		

	// Modus für Tastendruck
	// ---------------------
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR(" \025 module button function (cur. "));
#else
	LokalTextAusgabeP(PSTR(" \025 funktion taste am modul (akt. "));
#endif //def SPRACHE_EN

	LokalZahlAusgabe(TasteFunktion, 0);

#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR(") new:  "));
#else
	LokalTextAusgabeP(PSTR(") neu:  "));
#endif //def SPRACHE_EN

	if (LokalZahlEingabe(&TasteFunktion, 0) < 0)
		return;

	LokalTextAusgabeP(OkStrP);
	
	// weitere Eingaben
	// ----------------

	LokalTextAusgabeP(PSTR(" +++ "));
	LokalZeichenAusgabe(HELLC_ENDE);
	LokalZeichenAusgabe(' ');
	}


/////////////////////////////////////////////////////////////

//! Beendet die Selbstkonfiguration des Moduls.
//-----------------------------------------------
//! Arbeitet mit dem angeschlossenen Endgerät zusammen.

static void KonfigurationEnde()
	{
	FernschrAusschalten();
	
	KonfigSchreibeByte(EEAdr_BusEigenAdresse, BusEigenAdresse);
	
	KonfigSchreibeBool(EEAdr_UmleitungAbweisen, UmleitungAbweisen);

	KonfigSchreibeByte(EEAdr_KommendSperreWahl, KommendSperreWahl);

	KonfigSchreibeByte(EEAdr_LokalbetriebWahl, LokalbetriebWahl);

	KonfigSchreibeByte(EEAdr_TasteFunktion, TasteFunktion);

	KonfigSchreibeByte(EEAdr_AnrufAbbruchZeit, AnrufAbbruchZeit);

	uint8_t i;
	for (i = 0 ; i < AutoWahlMaxZiffern ; i++)
		KonfigSchreibeByte(EEAdr_AutoWahlZiffern + i, AutoWahlZiffern[i]);
	
	SperrzeitSpeicherEeprom(EEAdr_Sperrzeit);

	KonfigSchreibeString(EEAdr_Kennung, Kennung, sizeof(Kennung));

	Aktivieren(true);
	
	clr_LEDGELB();

	}
	
	
/////////////////////////////////////////////////////////////

//! Fehlertext-Ausgabe

static void FehlermeldungDrucken()
	{
	Aktivieren(false);
	LedRotEin = true;
	
	if (!FernschrEinschalten(true))
		{
		LedRotEin = false;
		return;
		}
	
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
		{
		BlinkTaktFaktor = 1;
		LedRotEin = true;
		}
	
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
		if (HellBetrieb != Aus)
			break;

#ifndef KOMMSER
		if (Grund == SperreStoerungIntern && SignalTwiFehlerzaehler == 0)
			break;
#endif //ndef KOMMSER
			
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

#ifndef LED_BLAU_INAKTIV
		if (TimerVal(&BlinkTimer) > 500 * BlinkTaktFaktor)
			StartTimer(&BlinkTimer);
		else if (TimerVal(&BlinkTimer) > 300 * BlinkTaktFaktor)
			seto_LEDBLAU();
		else
			inp_LEDBLAU();
#endif //ndef LED_BLAU_INAKTIV
		
		}
		
#ifndef LED_BLAU_INAKTIV
	inp_LEDBLAU();
#endif //ndef LED_BLAU_INAKTIV

	LedRotEin = false;
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
	clr_LEDROT();
	clr_LEDGELB();
	clr_LEDGRUEN();
	Aktivieren(false);

#ifndef KOMMSER
	PufferSpeich(&KommOutBuf, HellDebugStartBefehl);
#endif //ndef KOMMSER

	while (Tastendruck == NichtGedr)
		{
		seto_LEDBLAU(); // wird von FernschrIO() ggf. wieder gelöscht. Dann ist "halbe Helligkeit" zu sehen.

		TastePruefen();
		FernschrIO();

#ifndef KOMMSER
		if (!PufferLeer(&KommInBuf) && !PufferVoll(&DebugOutBuf))
			PufferSpeich(&DebugOutBuf, PufferAusg(&KommInBuf));


		if (BIT_IS_SET(UCSR0A, RXC0) && !PufferVoll(&KommOutBuf))
			{ // Zeichen empfangen
			uint8_t c = UDR0;
			PufferSpeich(&KommOutBuf, c);
			}

#endif //ndef KOMMSER

		// if (HellBetrieb == MeldEin || HellBetrieb == KdoEin)
		//	HellBetrieb = KdoAus;
		}

		
	Tastendruck = NichtGedr;

#ifndef KOMMSER
	PufferSpeich(&KommOutBuf, HellDebugEndeBefehl);
	while (!PufferLeer(&KommOutBuf))
		FernschrIO();
#endif //ndef KOMMSER

	Aktivieren(true);
	inp_LEDBLAU();
	clr_LEDROT();

	KommendSperren(SperreTaste);
		
	} // Deaktivieren


//! Das Hauptprogramm 
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

	init_LEDROT();
	init_LEDGELB();
	init_LEDGRUEN();
	init_LEDBLAU();
	init_TASTE();

	init_SV_EIN();
	init_TASTEEXT();
	
	init_DiagA();
	init_DiagB();
	init_DiagC();

	set_LEDROT();

	KonfigSpeicherInit();
	
	// Timer initialisieren
	MsTimerInit();

	SperrzeitInit();
	
	pgm_read_byte(Identifier); // Dummy read to force the identifier to be placed in the FLASH.

	BusEigenAdresse = KonfigLeseByteBegrenzt(EEAdr_BusEigenAdresse, 11 << 1/*Standardwert*/, BusAdrMin, BusAdrMax) & 0xFE; // Bit 0 löschen
	BusEigenAdrMehrfach = 1;
	RundsendEmpfFreig = true;

	UmleitungAbweisen = KonfigLeseBool(EEAdr_UmleitungAbweisen, false);

	KommendSperreWahl = KonfigLeseByteBegrenzt(EEAdr_KommendSperreWahl, KommendSperreWahl_Std, 0, 99);

	TasteFunktion = KonfigLeseByteBegrenzt(EEAdr_TasteFunktion, 0, 0, 1); // KEIN Bool

	SperrzeitLadeEeprom(EEAdr_Sperrzeit);

	LokalbetriebWahl = KonfigLeseByteBegrenzt(EEAdr_LokalbetriebWahl, LokalbetriebWahl_Std, 0, 99);
	
	uint8_t i;
	for (i = 0 ; i < AutoWahlMaxZiffern ; i++)
		AutoWahlZiffern[i] = KonfigLeseByte(EEAdr_AutoWahlZiffern + i, 255); // nicht begrenzt, da alles über 9 das Endezeichen ist.
	
	AnrufAbbruchZeit = KonfigLeseByteBegrenzt(EEAdr_AnrufAbbruchZeit, AnrufAbbruchZeit_Std, 10, 30);

	KonfigLeseString(EEAdr_Kennung, Kennung, KENNUNG_MAXLEN, "\r\n555555 hell d");
	
	LedRotEin = false;

	SerIOInit();
	
	KommInit();

	PufferInit(&KommInBuf);
	PufferInit(&KommOutBuf);
	
	PufferInit(&DebugOutBuf);

	TMsTimer Timer;
	StartTimer(&Timer);

	sei();

	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 250)
		;
	
	// Bei Tastendruck Selbsttest
	bool SelbsttestAusfuehen = get_TASTE();

	/*/ Bei Tastendruck Watchdog AUS
	if (get_TASTE()) 
		{ 
		wdt_disable();
		set_LEDGELB();
		StartTimer(&Timer);
		}
	//*/

	clr_LEDROT();
	set_LEDGELB();

	TwiInit();

// Pull-Up-Widerstände für TWI einschalten:
	SET_BIT(PORTC, 4); // SDA
	SET_BIT(PORTC, 5); // SCL

	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 500)
		;

	clr_LEDGELB();
	set_LEDGRUEN();

	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 750)
		;

	clr_LEDGRUEN();
	seto_LEDBLAU();

	// TWI nochmal resetten
	TWCR = (1<<TWINT) | (0<<TWEA) | (0<<TWSTA) | (1<<TWSTO) | (0<<TWEN) | (0<<TWIE);

	while (TimerVal(&Timer) < 760)
		;
		
	TWCR = (1<<TWINT) | (1<<TWEA) | (0<<TWSTA) | (0<<TWSTO) | (1<<TWEN) | (1<<TWIE);

	while (TimerVal(&Timer) < 1000 + BusEigenAdresse)
		;

	if (SelbsttestAusfuehen)
		{
		} // if SelbsttestAusfuehren

	BusEigenAdressePruefenUndSetzen(BusEigenAdresse);

	WarteKonfig = false;
	
	StartTimer(&NachlaufTimer);
	
#ifdef DEBUGSER
	DebugAusgPStr(PSTR("HellschrKomm Adresse "));
	DebugAusgZahl(BusEigenAdresse >> 1);
	DebugAusgPStr(PSTR("\r\n"));
#endif //def DEBUGSER

#ifdef KOMMSER
	LokalTextAusgabeP(PSTR("HellschrKomm Ser-Test Adresse "));
	LokalZahlAusgabe(BusEigenAdresse >> 1, 0);
	LokalTextAusgabeP(PSTR("\r\n"));
#endif //def KOMMSER
	
	inp_LEDBLAU();
	
	while (true)
		{
		// aktueller Zustand: Ausgeschaltet
		/* So lange LEDROT die Störung anzeigen soll...
		if (TimerVal(&Timer) <= 1200)
			clr_LEDROT();
		else if (TimerVal(&Timer) <= 1400)
			bset_LEDROT(BusEigenAdresse == BusAdrUngueltig);
		else
			StartTimer(&Timer);

		clr_LEDGELB();
		clr_LEDGRUEN();
		inp_LEDBLAU();
		*/

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

		if (HellBetrieb == MeldEin)
			{
			VerbindungGehend();
			}
			
		if (KoEinschalten())
			{
			VerbindungKommend();
			}

		if (HellBetrieb == Aus && !PufferLeer(&KommInBuf))
			PufferAusg(&KommInBuf); // verwerfen

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
		
#ifndef KOMMSER
		if (SignalTwiFehlerzaehler >= SignalTwiFehlerzaehlerMax)
			KommendSperren(SperreStoerungIntern);	
#endif //ndef KOMMSER

		} // while (true)
	} // main()


