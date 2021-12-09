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
//================================================================
// verwendete Pins siehe ports.h
//================================================================
//				


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
#include "LokalUhr.h"

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
const char PROGMEM Identifier[] = "___TxP2_SeriellUndSpeicher2-" PROGIDZUSATZ "___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#else
//! Identifikation im Programmspeicher
const char PROGMEM Identifier[] = "___TxP2_SeriellUndSpeicher2___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#endif

#else // PLATINE_VERSION < 20
	
#ifdef PROGIDZUSATZ
//! Identifikation im Programmspeicher
const char PROGMEM Identifier[] = "___TxP2_SeriellUndSpeicher-" PROGIDZUSATZ "___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#else
//! Identifikation im Programmspeicher
const char PROGMEM Identifier[] = "___TxP2_SeriellUndSpeicher___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#endif
	
#endif // PLATINE_VERSION

#include "timercs.h"


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


// Kennung und Kennwort

char Kennung[KENNUNG_MAXLEN]; //!< Eigene Kennung, da kein echter Fernschreiber angeschlossen.
char Kennwort[KENNWORT_MAXLEN]; //!< Kennwort für Fernabfrage des Anrufspeichers.


// Grundfunktionen
// ---------------

#include "TxP2-Endgeraet.h"


void LokalZeichenAusgabe(char c);


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


//! Meldet Serielle Schnittstelle bereitschaft zur Übernahme weiterer Daten?
//--------------------------------------------------------------------------
//! \retval true bei Empfangsbereitschaft der Gegenstelle.
static bool GetCTS()
	{ 
	return !get_SER_CTS();
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
			set_SER_RTS();
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
		clr_SER_RTS();
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
		TastePruefen();
		if (Tastendruck != NichtGedr)
			{
			Tastendruck = NichtGedr;
			return '\0'; // wirkt wie abbruch.
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
	TWCR = (1<<TWINT) | (0<<TWEA) | (0<<TWSTA) | (0<<TWSTO) | (0<<TWEN) | (0<<TWIE);
	
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
//! Aufzeichnung. Kehrt erst nach Verbindungsabbau zurück.
static void VerbindungKommend()
	{
	bool SeriellEin;
	
	LED_EIN(GRUEN);
	
	SeriellEin = SeriellBereit();
	
	if (SeriellEin)
		{
#ifdef SPRACHE_EN
		LokalTextAusgabeP(PSTR("\r\nIncall\r\n"));
#else
		LokalTextAusgabeP(PSTR("\r\nAnruf\r\n"));
#endif
		SerSendFlush();
		}

	if (GeEinschalten() != GeEinschAnrufquitt)
		{
		if (SeriellEin)
#ifdef SPRACHE_EN		
			LokalTextAusgabeP(PSTR("\r\nError\r\n"));
#else			
		    LokalTextAusgabeP(PSTR("\r\nFehler\r\n"));
#endif			
		GeAusschalten();
		}
	else
		{
		VerbindungSteht(SeriellEin, false); PARAMETER WEG
		}
		
	}
	

//! Bearbeitet gehende Verbindungen.
//-------------------------------------
//! Wartet auf Wahlziffern, ermittelt Verbindungspartner, sendet den Einschaltauftrag. 
//! Kehrt erst nach Verbindungsabbau zurück.
static void VerbindungGehend()
	{
	LED_EIN(GELB);

	switch (GeEinschalten())
		{ // hier nur break benutzen, wenn Einschaltung erfolgreich
		case GeEinschFehler:
			return; 

		case GeEinschWahl:
#ifdef SPRACHE_EN		
			LokalTextAusgabeP(PSTR("\r\nDial: "));
#else			
			LokalTextAusgabeP(PSTR("\r\nWählen: "));
#endif			
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
#ifdef SPRACHE_EN					
					LokalTextAusgabeP(PSTR("\r\nAbort"));
#else
					LokalTextAusgabeP(PSTR("\r\nAbbruch"));
#endif					
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
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR("\r\nConnected\r\n"));
#else	
	LokalTextAusgabeP(PSTR("\r\nVerbunden\r\n"));
#endif	

	VerbindungSteht(true, false);

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
	
	
//! Schaltet LED entspechend der Status-Bits an.
static void GeSendeText(char* s)
	{
	GeSendeCode(TtyCodeBuUm); // für definierte Verhältnisse...
	while (*s != '\0')
		{
		GeSendeZeichen(*s);
		s++;
		}
	}
	

// #define DEBUG_OUT


//! Gibt die eigene Kennung beim Verbindungspartner aus und wertet eingegegeben Text
//! auf Übereinstimmung mit dem gespeicherten Kennwort aus.
static bool KennungsausgabeUndKennwortAbfrage(bool SeriellEin)
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
	
	
//! Behandelt nach Verbindungsaufbau die Datenübertragung in beiden Richtungen.
//-----------------------------------------------------------------------------
//! Wird bei kommenden und bei gehenden Verbindungen benutzt.
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
					}
				}
			else
				{
				if (SeriellEin)
					LokalZeichenAusgabe(c);
				}
			}

		if (KoAusschalten())
			{
			GeAusschalten();
			if (SeriellEin)
#ifdef SPRACHE_EN				
				LokalTextAusgabeP(PSTR("\r\nDisconnected\r\n"));
#else
				LokalTextAusgabeP(PSTR("\r\nGetrennt\r\n"));
#endif			
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
				}
				
			else if (c == CTRL('s'))
				// Abbruch durch Bediener
				{
				GeAusschalten();
				if (SeriellEin)
#ifdef SPRACHE_EN					
					LokalTextAusgabeP(PSTR("\r\nDisconnected\r\n"));
#else
					LokalTextAusgabeP(PSTR("\r\nBeendet\r\n"));
#endif					
				return;
				}
			else
				GeSendeZeichen(c);

			} // if !PufferLeer(&SerInBuf)

		DoSwTwi();
		LEDAktualisieren();
		}

	} // VerbindungSteht


//! wird nach kurzem Tastendruck aufgerufen
static void Deaktivieren()
	{
	LED_AUS(ROT);
	LED_AUS(GELB);
	LED_AUS(GRUEN);
	LED_EIN(BLAU);
	Aktivieren(false);

	while (Tastendruck == NichtGedr)
		{
		TastePruefen();
		DoSwTwi();
		}

	Tastendruck = NichtGedr;

	Aktivieren(true);
	} // Deaktivieren


//! wird nach langem Tastendruck aufgerufen
static void Konfiguration()
	{
	LED_EIN(ROT);
	LED_AUS(GELB);
	LED_AUS(GRUEN);
	LED_AUS(BLAU);

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

#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR("\r\n config telex serial version " SVNVERSION " date " __DATE__));
#else
	LokalTextAusgabeP(PSTR("\r\n konfiguration seriell+speicher version " SVNVERSION " datum " __DATE__));
#endif	

	if (!KonfigurationAllgemein())
		{
		Aktivieren(true);
		LED_AUS(ROT);
		return;
		}

	if (BusEigenAdresse != eeprom_read_byte(&BusEigenAdresse_EE))
		eeprom_write_byte(&BusEigenAdresse_EE, BusEigenAdresse);

#ifdef SPRACHE_EN	
	LokalTextAusgabeP(PSTR("\r\n answerback: "));
#else
	LokalTextAusgabeP(PSTR("\r\n Kennung: "));
#endif	
	LokalTextAusgabe(Kennung + 2); // CR + LF weglassen
#ifdef SPRACHE_EN	
	LokalTextAusgabeP(PSTR(" new:         "));
#else
	LokalTextAusgabeP(PSTR(" neu:         "));
#endif	
	LokalTextEingabe(Kennung + 2, KENNUNG_MAXLEN - 3); // erste 2 Zeichen für CRLF reserviert

#ifdef SPRACHE_EN	
	LokalTextAusgabeP(PSTR("\r\n password: "));
#else
	LokalTextAusgabeP(PSTR("\r\n Kennwort: "));
#endif	
	LokalTextAusgabe(Kennwort);
#ifdef SPRACHE_EN	
	LokalTextAusgabeP(PSTR(" new:         "));
#else
	LokalTextAusgabeP(PSTR(" neu:         "));
#endif	
	LokalTextEingabe(Kennwort, KENNWORT_MAXLEN - 1);

#ifdef SPRACHE_EN	
	LokalTextAusgabeP(PSTR("\r\n config complete+++   \r\n"));
#else
	LokalTextAusgabeP(PSTR("\r\n fertig+++   \r\n"));
#endif	

	Aktivieren(true);

	}


//! Das Hauptprogramm der Fernschreiber-Simulation mit RS232.
//-----------------------------------------------------------
int main()
	{
	bool HauptmenueAusgeben = true;

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
	init_LED_GELB();
	init_LED_GRUEN();
	init_LED_BLAU();

	LED_EIN(ROT);

	init_SER_RTS();
	init_SER_CTS();

	//SET_BIT(TEST1_DDR, TEST1_BIT);

	// Timer initialisieren
	MsTimerInit();
	
	BusEigenAdresse = eeprom_read_byte(&BusEigenAdresse_EE) & 0xFE;
	if (BusEigenAdresse < BusAdrMin || BusEigenAdresse > BusAdrMax)
		BusEigenAdresse = 44 << 1; // Standardwert
	BusEigenAdrMehrfach = 1;
	
	eeprom_read_string(Kennung, Kennung_EE, sizeof(Kennung));
	if (Kennung[0] == '\377')
		strcpy_P(Kennung, PSTR("\r\ntxp-ab"));
	else
		Kennung[sizeof(Kennung)-1] = '\0'; // sicherheitshalber
		
	eeprom_read_string(Kennwort, Kennwort_EE, sizeof(Kennwort));
	if (Kennwort[0] == '\377')
		strcpy_P(Kennwort, PSTR("kennwort"));
	else
		Kennwort[sizeof(Kennwort)-1] = '\0'; // sicherheitshalber

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
		if (BusEigenAdresse == BusAdrUngueltig)
			{ // Noch nicht korrekt konfiguriert
			if (TimerVal(&Timer) <= 800)
				LED_AUS(ROT);
			else if (TimerVal(&Timer) <= 1000)
				LED_EIN(ROT);
			else
				StartTimer(&Timer);
			LED_AUS(GELB);
			LED_AUS(GRUEN);
			LED_AUS(BLAU);
			}
		else
			{ // alles in Ordnung
			LED_AUS(ROT);
			LED_AUS(GELB);
			LED_AUS(GRUEN);
			LED_AUS(BLAU);
			}
			
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
#ifdef SPRACHE_EN				
				LokalTextAusgabeP(PSTR("\r\nCtrl-A: dial/connect"));
#else
				LokalTextAusgabeP(PSTR("\r\nCtrl-A: Anwahl"));
#endif				
				SerSendFlush();
#ifdef SPRACHE_EN				
				LokalTextAusgabeP(PSTR(", Ctrl-K: config"));
#else
				LokalTextAusgabeP(PSTR(", Ctrl-K: Konfiguration"));
#endif				
#ifdef BUSDEBUG_DIALOG
				SerSendFlush();
				LokalTextAusgabeP(PSTR(", Ctrl-D: Debug"));
#endif
				SerSendFlush();
				LokalTextAusgabeP(PSTR(" --> "));
				} // if (SeriellBereit())
			HauptmenueAusgeben = false;
			}

		TastePruefen();
		SeriellIO(); 
		
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
#ifdef SPRACHE_EN						
						LokalTextAusgabeP(PSTR(" Error: not configured"));
#else
						LokalTextAusgabeP(PSTR(" Fehler: nicht konfiguriert."));
#endif						
					HauptmenueAusgeben = true;
					break;
				
				case CTRL('k'):
					eeprom_write_byte(&Minute_EE, Minute);
					Konfiguration();
					HauptmenueAusgeben = true;
					break;

				default:
					while (!PufferLeer(&SerInBuf))
						SerEmpfZ(true); // Puffer leeren
#ifdef SPRACHE_EN				
					LokalTextAusgabeP(PSTR("\r\ninvalid command"));
#else
					LokalTextAusgabeP(PSTR("\r\nUngültiges Kommando"));
#endif					
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

		// Parameter im eigenen Eeprom aktualisieren
		eeprom_write_byte_noblock(&BusEigenAdresse_EE, BusEigenAdresse);
		eeprom_write_string_noblock(Kennung_EE, Kennung);
		eeprom_write_string_noblock(Kennwort_EE, Kennwort);
			
		} // while (1)
	} // main()


