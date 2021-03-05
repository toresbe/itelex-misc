//================================================================
// Schnittstelle für i-Telex-System für Hellschreiber (z.B. Hell GL72)
// für ATmega168 auf Platine Seriell+Spezial
// Teil Kommunikation
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


// Konstanten
// ----------

enum { LokalbetriebWahl_Std = 88 };
enum { KommendSperreWahl_Std = 0 };
enum { AutoWahlMaxZiffern = 10 }; // bei Änderung ist das EEPROM-Layout kompromittiert.
enum { AnrufAbbruchZeit_Std = 7 }; // sekunden
enum { StartQuittVerzoegerung_Std = 5 }; // x/10 sekunden
enum { StartQuittVerzoegerung_Min = 2 }; // x/10 sekunden


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
	EEAdr_StartQuittVerz = 38,
	EEAdr_Kennung = 39, // Beansprucht 20 Bytes
	EEAdr_Ende = 59 // Platz für neue Werte, darf erhöht werden	
};


// Typen
// -----

typedef enum { SperreTaste, SperreStoerung, SperreZeit, SperreWahl } TSperreGrund;

typedef char TKennung[KENNUNG_MAXLEN];

// Konfigurations-Variablen
// ====================

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

TKennung Kennung; //!< Eigene Kennungen, da kein echter Fernschreiber angeschlossen.

TMsTimer NachlaufTimer; //!< Steuert nur den Ausgang für den externen SV-Schalter



// Arbeits-Variablen 
// =======================

// Einstell-Modus
bool WarteKonfig; // TODO noch nicht implementiert

TPuffer SerInBuf; //!< Empfangspuffer für die serielle Schnittstelle. Nicht identisch mit Puffer für Baudot-Ein/-Ausgabe.
TPuffer SerOutBuf; //!< Sendepuffer für die serielle Schnittstelle. Nicht identisch mit Puffer für Baudot-Ein/-Ausgabe.

enum { Aus, Ein, KdoEin, MeldEin, KdoAus, MeldAus } HellBetrieb;
	

// Schnittstellen-Spezifische Funktionen
// =====================================

//! bedient Hardware-IO entsprechend der aktuellen Zustände.

//! Hier ist es nur das Schreiben und Lesen auf der Seriellen Schnittstelle


static void FernschrIO()
	{
	static TMsTimer KdoTimer;
	
/* TODO LED Anzeigen
	if (BefehlEinschalten)
		bset_LEDBLAU(!BefehlMark);

	if (BIT_IS_SET(Status, StatBit_AngerufenBelegt))
		bset_LEDGELB(!MeldungMark);
	else // !BIT_IS_SET(Status, StatBit_AngerufenBelegt))
		bset_LEDGRUEN(!MeldungMark);
*/

	// Empfang von Codes und Zeichen vom Signalprozessor verarbeiten:	
	if (BIT_IS_SET(UCSR0A, RXC0))
		{ // Zeichen empfangen
		char c = UDR0;
		if (c == HellEinschaltMeldung)
			{
			switch (HellBetrieb)
				{
				case Aus:
					HellBetrieb = MeldEin;
					break;
				case KdoAus:
				case MeldAus:
					HellBetrieb = Ein;
					break;
				default:
					break;
				}
			}
		else if (c == HellAusschaltMeldung)
			{
			switch (HellBetrieb)
				{
				case Ein:
					HellBetrieb = MeldAus;
					break;
				case KdoEin:
				case MeldEin:
					HellBetrieb = Aus;
					break;
				default:
					break;
				}
			}
		else // normales Zeichen -> Puffern
			{
			if (PufferAnzahl(&SerInBuf) < MaxPuffer - 2)
				{
				if (c == '%')
					c = CodeChrKlingel;
				PufferSpeich(&SerInBuf, c); 
				}
			} // PufferAnzahl(&SerInBuf) < MaxPuffer - 2
			
		} // Serielles Zeichen empfangen

	if (BIT_IS_SET(UCSR0A, UDRE0)) // Schnittstelle bereit für Daten
		{
		switch (HellBetrieb)
			{
			case Aus: 
				// nix, ggf. Puffer leeren
				break;
				
			case KdoEin:
				if (TimerVal(&KdoTimer) >= 500)
					{
					UDR0 = HellEinschaltBefehl;
					StartTimer(&KdoTimer);
					}
				break;

			case KdoAus:
				if (TimerVal(&KdoTimer) >= 500)
					{
					UDR0 = HellAusschaltBefehl;
					StartTimer(&KdoTimer);
					}
				break;

			case Ein:
			case MeldAus:
				if (!PufferLeer(&SerOutBuf))
					UDR0 = PufferAusg(&SerOutBuf);
				break;

			default:
				break; 
			} // switch (HellBetrieb)
		} // if Schnittstelle bereit für Daten

	} // FernschrIO


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
		FernschrIO();
		TastePruefen();
		DoSwTwi();
		if (Tastendruck != NichtGedr)
			{
			Tastendruck = NichtGedr;
			return '\t';
			}
		if (HellBetrieb != Ein)
			return '\0';
		}
		
	char c;
	c = SerEmpfZ(true);
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
	while (PufferVoll(&SerOutBuf) && HellBetrieb == Ein)
		{
		FernschrIO();
		DoSwTwi();
		TastePruefen();
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


//////////////////////////////////////////////////////////////////

// Allgemeine Funktionen
// =====================
	

//! Einschaltung des Fs auslösen.
//-------------------------------
//! \returns Einschaltung wurde erfolgreich durch Endgerät quittiert.

static bool FernschrEinschalten(bool WarteQuittVerz)
	{
	if (HellBetrieb == Aus || HellBetrieb == KdoAus || HellBetrieb == MeldAus)
		HellBetrieb = KdoEin;

	PufferInit(&SerInBuf);
	PufferInit(&SerOutBuf);

	while (HellBetrieb != Ein)
		{
		FernschrIO();
		if (HellBetrieb == MeldAus)
			return false;
		}

	// TODO irgendwas zu WarteQuittVerz
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
		


//! Schaltet LED entspechend der Status-Bits an.
static void LEDAktualisieren()
	{
/* TODO		
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
		
*/
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

			UDR0 = HellAusschaltBefehl; // sicherheitshalber Ausschaltbefehle an den Signalprozessor

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


//! Sendet ein Zeichen an die Gegenstelle, wartet aber bei vollem Puffer
static void ZeichenSenden(char c)
	{
	while (!GeSendePufferLeer())
		{
		DoSwTwi();
		FernschrIO();
		if (KoAusschalten())
			return;
		LEDAktualisieren();
		}
	while (!GeSendeZeichen(c))
		; // kann eigentlich nicht lange dauern
	}


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
	set_LEDROT();
	
	if (!FernschrEinschalten(true))
		{ // Timeout...
		clr_LEDGRUEN();
		GeAusschalten(true);
		KommendSperren(SperreStoerung);
		clr_LEDROT();		
		return;
		}

	clr_LEDROT();
	
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
	set_LEDROT();
	Aktivieren(false);
	
	LokalTextAusgabeP(PSTR("loc  "));

	while (HellBetrieb == Ein)
		{
		FernschrIO();
		}

	FernschrAusschalten();

	Aktivieren(true);
	clr_LEDROT();
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
		FernschrAusschalten();
	else
		StartTimer(&NachlaufTimer);
	
	clr_LEDROT();
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
	
	TODO anpassen!
	
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR("dial: "));
#else
	LokalTextAusgabeP(PSTR("waehlen: "));
#endif
	
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
		
		if (!PufferLeer(&SerInBuf))
			{
			c = SerEmpfZ(true);
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

		if (KoAusschalten())
			{
			// FernschrAusschalten() macht die aufrufende Routine
			LokalTextAusgabeP(PSTR("\r\nAbort"));
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
	
	Verbunden = WahlMitTastatur();
	if (Verbunden)
		FernschrEinschalten(true);

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
			VerbindungSteht(true); 
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
	bool ErsteKennungAbfrage;
	
	StartTimer(&KennungAbfrageTimer);
	ErsteKennungAbfrage = true;

	GeSendeMark(true); 

	SendeUmsetzModus = UmsetzFern;
	EmpfUmsetzModus = UmsetzFern; 

	while (true)
		{
		FernschrIO();
		TastePruefen();

		// TODO nochmal passende stelle suchen:
		
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
		
		
		// if (TimerVal(&KennungAbfrageTimer) > 1000 && !MeldungMark)
		//	AutoKennungAbfrage = false;
			


		char c;
		if (KoEmpfZeichen(&c))
			{
			if (c == CodeChrWerDa) // TODO and Kennung nicht leer
				{
				// Kennungsausgabe();  TODO
				}
			else
				{
				LokalZeichenAusgabe(c);
				}
			}

		if (KoAusschalten())
			{
#ifdef SPRACHE_EN				
			LokalTextAusgabeP(PSTR("\r\nDisconnected\r\n"));
#else
			LokalTextAusgabeP(PSTR("\r\nGetrennt\r\n"));
#endif			
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
				}
				
			else if (c == CTRL('s'))
				// Abbruch durch Bediener
				{

				GeAusschalten(false);
				while (!KoAusschalten())
					{
					DoSwTwi();
					LEDAktualisieren();
					FernschrIO(); 
					}
				
#ifdef SPRACHE_EN					
				LokalTextAusgabeP(PSTR("\r\nDisconnected\r\n"));
#else
				LokalTextAusgabeP(PSTR("\r\nBeendet\r\n"));
#endif					
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

	SeriellUmsetzInit();
	Aktivieren(false);
	set_LEDROT();
	
	if (!FernschrEinschalten(true))
		return;
	
	BaudotMode_SetEmpfangen(BaudotMode); // damit auch eine BU-Umschaltung gesendet wird.
	
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR("\r\n configuration tw39plus version " SVNVERSION " date " __DATE__));
#else
	LokalTextAusgabeP(PSTR("\r\n konfiguration tw39plus version " SVNVERSION " datum " __DATE__));
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
					{
					LokalTextAusgabeP(OkStrP);
					return; // unverändert lassen
					}
				else
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

	KonfigSchreibeByte(EEAdr_KommendSperreWahl, KommendSperreWahl);

	KonfigSchreibeByte(EEAdr_LokalbetriebWahl, LokalbetriebWahl);

	KonfigSchreibeByte(EEAdr_TasteFunktion, TasteFunktion);

	KonfigSchreibeByte(EEAdr_AnrufAbbruchZeit, AnrufAbbruchZeit);

	KonfigSchreibeByte(EEAdr_StartQuittVerz, StartQuittVerzoegerung);
	
	uint8_t i;
	for (i = 0 ; i < AutoWahlMaxZiffern ; i++)
		KonfigSchreibeByte(EEAdr_AutoWahlZiffern + i, AutoWahlZiffern[i]);
	
	SperrzeitSpeicherEeprom(EEAdr_Sperrzeit);

	Aktivieren(true);

	clr_LEDROT();
	}
	
	
/////////////////////////////////////////////////////////////

//! Fehlertext-Ausgabe

static void FehlermeldungDrucken()
	{
	SeriellUmsetzInit();
	Aktivieren(false);
	set_LEDROT();
	
	if (!FernschrEinschalten(true))
		return;
	
	BaudotMode_SetEmpfangen(BaudotMode); // damit auch eine BU-Umschaltung gesendet wird.
	
	KonfigSpeicherFehlerAusgeben();
	
	FernschrAusschalten();

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
	
	StartQuittVerzoegerung = KonfigLeseByteBegrenzt(EEAdr_StartQuittVerz, StartQuittVerzoegerung_Std, StartQuittVerzoegerung_Min, 200);

	uint8_t i;
	for (i = 0 ; i < AutoWahlMaxZiffern ; i++)
		AutoWahlZiffern[i] = KonfigLeseByte(EEAdr_AutoWahlZiffern + i, 255); // nicht begrenzt, da alles über 9 das Endezeichen ist.
	
	AnrufAbbruchZeit = KonfigLeseByteBegrenzt(EEAdr_AnrufAbbruchZeit, AnrufAbbruchZeit_Std, 3, 25);
		

	// TODO eeprom_read_string(Kennung, Kennung_EE);

	SerIOInit();
	
	KommInit();

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
		} // if SelbsttestAusfuehren

	BusEigenAdressePruefenUndSetzen(BusEigenAdresse);

	WarteKonfig = false;
	
	StartTimer(&NachlaufTimer);
	
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


