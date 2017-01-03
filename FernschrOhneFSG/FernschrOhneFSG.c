//================================================================
// Fernschreiber-Schnittstelle Einfachstrom ohne Fernschaltgerät für TxP2-System
//	für ATmega168 auf Platine FernschrTW39
//================================================================
// Fernschreiber muss über einen Zeitschalter (Motorschalter) verfügen.
// Tastaturwahl, nach drücken der ersten Taste wird eine Wahlaufforderung in form von "ga" gesendet. 
// Verbindungsende wahlweise durch "break"-Signal, durch NNNN oder durch +++ [future]

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


#ifdef PROGIDZUSATZ
//! Marker im Code als Identifikation
PROGMEM const char Identifier[] = "___itlx_OhneFSG-" PROGIDZUSATZ "___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#else
//! Marker im Code als Identifikation
PROGMEM const char Identifier[] = "___itlx_OhneFSG___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";
#endif


// Typen
// -----

typedef enum { SperreTaste, SperreStoerung, SperreZeit, SperreWahl } TSperreGrund;

typedef enum { EndeNurBreak, EndeNachNNNN, EndeNach3Plus } TVerbindungsEndeKriterium;

enum { CodefolgeEndeMarke = 0x5A } ;
	//!< Markierung des Endes einer Codefolge. Wert wurde abweichend von 255 gewählt, um uninitialisiertes EEPROM zu erkennen.



// Variablen
// =========

// Hardware-Level
// --------------

bool BefehlMark; //!< Fs Schleifenstrom soll Ein sein
bool MeldungMark; //!< Fs Schleifenstrom ist Ein
bool BreakSignal; //!< Fs Schleife wurde für mehr als 0,8 Sekunden unterbrochen. Muss von der Anwendung zurückgesetzt werden.

TMsTimer AusschaltungTimer; //!< Zählt die Millisekunden von Schleifenunterbrechung bis Break-Signal
TMsTimer EntprellungTimer; //!< Zählt die Millisekunden von Pegelwechsel am Port bis tatsächlichem Pegelwechsel
TMsTimer RuheTimer; //!< Läuft, wenn weder gedruckt noch geschrieben wird


// Ablauf-Variablen
// ----------------

uint8_t KommendSperreWahl; //!< Welche Wahlnummer sperrt den Anschluss für ankommende Rufe

char BuZiMode; //!< Marker für Buchstaben-Ziffern-Umschaltung.

enum { MaxCodefolgeLaenge = 30 }; //!< Maximale Länge von #AusschaltZeichen, #Wahlaufforderung, #VerbindungHergestelltZeichen, #EigeneKennung

uint8_t AusschaltZeichen[MaxCodefolgeLaenge+1]; 
	//!< Druck-Sequenz als Zeichen für Ende der Verbindung.
	//!< Irgendwas > 0x1F (also mehr als 5 Bit) markiert Sequenz-Ende.

PROGMEM uint8_t AusschaltZeichenDefault[] = { TtyCodeBuUm, TtyCodeBuUm, TtyCodeBuUm, TtyCodeWR, TtyCodeZL, 6, 6, 6, 6, TtyCodeWR, TtyCodeZL, TtyCodeZL } ; // NNNN
	//!< Standardwert für #AusschaltZeichen.
	// Manuell prüfen, dass es nicht mehr als MaxCodefolgeLaenge Zeichen sind!
	
uint8_t WahlaufforderungZeichen[MaxCodefolgeLaenge+1];
	//!< Druck-Sequenz als Zeichen jetzt zu wählen

PROGMEM uint8_t WahlaufforderungZeichenDefault[] = { TtyCodeBuUm, TtyCodeWR, TtyCodeZL, 
													 11, 24, TtyCodeLeer, TtyCodeLeer, TtyCodeLeer, TtyCodeLeer, TtyCodeLeer, TtyCodeZiUm } ; // GA _ _ _ _ _
	//!< Standardwert für #WahlaufforderungZeichen.
	// Manuell prüfen, dass es nicht mehr als MaxCodefolgeLaenge Zeichen sind!

uint8_t VerbindungHergestelltZeichen[MaxCodefolgeLaenge+1];
	//!< Druck-Sequenz nach Eingang der Verbindungsbestätigung
	
PROGMEM uint8_t VerbindungHergestelltZeichenDefault[] = { TtyCodeBuUm, TtyCodeLeer, 14, 3, 6, TtyCodeWR, TtyCodeZL } ; // CON
	//!< Standardwert für #VerbindungHergestelltZeichen.
	// Manuell prüfen, dass es nicht mehr als MaxCodefolgeLaenge Zeichen sind!
	
uint8_t EigeneKennung[MaxCodefolgeLaenge+1];
	//!< Text des eigenen Kennungsgeber-Simulators

	
// Eeprom-Speicher
// ---------------

typedef struct 
	{
	uint8_t Platzhalter[4]; //!< Platzhalter, da Anfang des EEPROM gern von Störungen betroffen ist
	uint8_t KonfigVersion; //!< Falls strukturelle Änderungen mal erforderlich sind, können diese hiermit berücksichtigt werden.
	uint8_t BusEigenAdresse; //!< Eigene Busadresse auf dem I²C-Bus
	uint8_t KommendSperreWahl; //!< Welche Wahlnummer sperrt den Anschluss für ankommende Rufe
	TVerbindungsEndeKriterium VerbindungsEndeKriterium; //!< Wie wird eine Verbindung beendet? 
	uint8_t AusschaltZeichen[MaxCodefolgeLaenge+1]; 
	uint8_t WahlaufforderungZeichen[MaxCodefolgeLaenge+1];
	uint8_t VerbindungHergestelltZeichen[MaxCodefolgeLaenge+1];
	uint8_t EigeneKennung[MaxCodefolgeLaenge+1];
	TSperrzeitDaten SperrzeitDaten;
	} TEepromDaten;


EEMEM TEepromDaten EEDaten = { { 0 }, 1, 33, 0, EndeNurBreak, { 255 }, { 255 }, { 255 }, { 255 } } ;

	
///////////////////////////////////////////////////////////////////////////////

//! bedient Hardware-IO entsprechend der aktuellen Zustände.
//----------------------------------------------------------
//! setzt FS_AUSG entsprechend BefehlMark,
//! setzt MeldungMark und BreakSignal entsprechend FS_EING,
//! steuert die Status-LEDs.

static void FernschrIO(bool TasteMachtBreak)
	{
	if (BefehlMark)
		{
		clr_LEDBLAU();
		set_FS_AUSG();
		
		if (get_FS_EING() || (TasteMachtBreak && !get_TASTE()))
			{ // Schleifenstrom ist aus (negierter Eingang) ODER bei aktivierter Taste ist diese gedrückt.
			if (MeldungMark)
				{ // noch wird aber 'Mark' gemeldet
				if (TimerVal(&EntprellungTimer) > 3) // mindestens 3 ms konstant Space --> Space melden
					MeldungMark = false;
				}
			else
				StartTimer(&EntprellungTimer); // Regelzustand bei Space: Space wird auch gemeldet
			}
		else
			{ // Schleifenstrom ist ein
			if (!MeldungMark)
				{
				if (TimerVal(&EntprellungTimer) > 3) // mehr als 3 ms Strom --> ein
					MeldungMark = true;
				} 
			else
				StartTimer(&EntprellungTimer); // Meldung und tatsächlicher Zustand stimmen überein
			} // else Schleifenstrom ist ein
		
		// Prüfen, ob Space lange Andauert
		if (MeldungMark || BreakSignal)
			StartTimer(&AusschaltungTimer);
		else if (TimerVal(&AusschaltungTimer) > 800)
			BreakSignal = true;

		if (!MeldungMark)
			StartTimer(&RuheTimer);
		}
	else
		{ // Schleifenstrom-Unterbrechung durch Schnittstelle
		set_LEDBLAU();
		clr_FS_AUSG();
		MeldungMark = true; // da kein Duplex möglich (Echosperre)
		StartTimer(&EntprellungTimer); 
			// da Schleifenstrom 'zwangsweise' unterbrochen ist, wird Timer
			// zur Entprellung gesetzt
		StartTimer(&RuheTimer);
		StartTimer(&AusschaltungTimer);
		}
		
	if (BIT_IS_SET(Status, StatBit_AngerufenBelegt))
		bset_LEDGELB(!MeldungMark);
	else // !BIT_IS_SET(Status, StatBit_AngerufenBelegt))
		bset_LEDGRUEN(!MeldungMark);

	} // FernschrIO()
	
	

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
			if (get_TASTE()) //! \todo umgekehrte Tastenpolarität prüfen
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


//////////////////////////////////////////////////////////////////

//! Einschaltung des Fs auslösen.
//-------------------------------
//! Einfach eine kurze Schleifenunterbrechung als Start ausgeben und
//! Zwei Sekunden warten, es sei denn der Fernschreiber läuft vermutlich schon.
//! \returns Einschaltung wurde erfolgreich durch Endgerät quittiert.

static bool FsEinschalten()
	{
	TMsTimer AnlaufTimer;
	TMsTimer AbbruchTimer;
	
	set_FS_AKTIV(); // optionalen Motorschalter einschalten
	BefehlMark = false;
	StartTimer(&AnlaufTimer);
	while (TimerVal(&AnlaufTimer) < 20)
		FernschrIO(false);
	BefehlMark = true;
	BreakSignal = false;
	FernschrIO(false);
		
	StartTimer(&AbbruchTimer);
	while (TimerVal(&RuheTimer) < 1200) //! \todo Konfigurierbar
		{
		FernschrIO(false); // Bearbeitet auch #RuheTimer
		if (MeldungMark)
			StartTimer(&AbbruchTimer); // bei Dauer-Space: kein Gerät angeschlossenen
		
		if (TimerVal(&AbbruchTimer) > 7000)
			return false;
		}
		
	return true;
	}
		

static void LokalCodeAusgabe(uint8_t code); // kommt erst später...

static void LokalCodeAusgabeS(uint8_t *codep, bool StopIfCalled); // kommt erst später...


//////////////////////////////////////////////////////////////////

//! Ausschaltung des Fs auslösen. 
//-------------------------------
//! Da es keine echte Ausschaltung gibt, entsprechende Kennung ausgeben

static void FsAusschalten()
	{
	BefehlMark = true;
	BreakSignal = false;	

	// falls eben noch geschrieben wurde oder Störungen auf der Leitung waren
	while (TimerVal(&RuheTimer) < 500)
		FernschrIO(false);

	LokalCodeAusgabeS(AusschaltZeichen, true);

	clr_FS_AKTIV(); // optionalen Motorschalter ausschalten
	
	// noch eine weitere 1/4 Sekunde warten
	while (TimerVal(&RuheTimer) < 250)
		FernschrIO(false);
	
	BreakSignal = false;	
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
		FernschrIO(true);
		SeriellUmsetzung(MeldungMark, &BefehlMark);
		if (SerUmEmpfBitNr == SerUmEmpfFertig)
			{
			c = CodeZuZeichen(SerUmEmpfDaten, &BuZiMode);
			SerUmEmpfBitNr = SerUmEmpfWarte;
			if (c != '\0')
				return c;
			}
		if (BreakSignal)
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
		SeriellUmsetzung(true, &BefehlMark); // Empfagspegel wird ignoriert
		FernschrIO(true);
		}
	}


/////////////////////////////////////////////////////////////

//! Gibt ein Zeichenfolge am angeschlossenen Fs aus.
//--------------------------------------------------
//! \param code Zeichen im Baudot-Code

static void LokalCodeAusgabeS(uint8_t *codep, bool StopIfCalled)
	{
	static uint8_t CRLFCode[] = {TtyCodeWR, TtyCodeZL, 255};
	uint8_t i;

	for (i = 0 ; i < MaxCodefolgeLaenge ; i++)
		{
		if (codep[i] > 0x1F)
			break;
		if (StopIfCalled && KoEinschalten())
			{
			codep = CRLFCode;
			i = 0;
			StopIfCalled = false; // to prevent reset of codep after print of first character
			}
		LokalCodeAusgabe(codep[i]);
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
			
		
///////////////////////////////////////////////////////////////

//! Dialog-Abfrage für einen Text der als 5-Bit-Code abgespeichert wird.
//----------------------------------------------------------------------
//! Abschluss nur mit WR oder ZL.
//! WR, ZL oder Leerzeichen am Anfang wird ignoriert. 
//! neuer Text muss durch druckbare Begrenzungszeichen eingeschlossen werden. z.B. xhallox für hallo
//! . (Punkt) als einziges Zeichen = alten Wert behalten.
//! \param[out] buf Puffer des eingegebenen Textes.
//! \param[in] maxcodes Anzahl erlaubter codes bei der Eingabe, auch Puffergröße.
//! \retval 0 abbruch
//! \retval 1 unverändert
//! \retval 2 eingabe erfolgt
//! \todo mal nach KonfigDialog verschieben, da aber LokalZeichenLesen nicht verwendet werden kann, muss eine größere Umstellung gemacht werden.


uint8_t LokalCodefolgeEingabe(PGM_P Prompt, uint8_t* buf, uint8_t maxcodes)
	{
	uint8_t Pos = 0; 
	char TrennZeichen = '\0'; // Zeichen für noch nicht belegt.

	if (Prompt != NULL)
		LokalTextAusgabeP(Prompt);
	
	while (true)
		{ // Schleifendurchlauf einmal je Taste
		uint8_t code;
		char zeichen; 
		
		while (true)
			{ // Schleifendurchlauf bis ein Zeichen eingegeben oder Abbruch
			FernschrIO(true);
			SeriellUmsetzung(MeldungMark, &BefehlMark);
			if (SerUmEmpfBitNr == SerUmEmpfFertig)
				{
				code = SerUmEmpfDaten;
				SerUmEmpfBitNr = SerUmEmpfWarte;
				break;
				}
			if (BreakSignal)
				{
				if (Pos > 0)
					{
					if (Pos < maxcodes-1)
						buf[Pos] = CodefolgeEndeMarke;
					return 2;
					}
				else
					return 0;
				}
			}

		zeichen = CodeZuZeichen(code, &BuZiMode);
		
		if (TrennZeichen != '\0')
			{ // Zeichenfolge wurde bereits begonnen.
			if (zeichen == TrennZeichen)
				{
				if (Pos < maxcodes-1)
					buf[Pos] = CodefolgeEndeMarke;
				LokalTextAusgabeP(OkStrP);
				return 2;
				}
			else
				{
				if (Pos < maxcodes-1)
					buf[Pos++] = code;
				}
			} // if TrennZeichen != '\0'
		else // TrennZeichen == '\0'
			{ // Trennzeichen wurde noch nicht wirksam eingegebenen
			if (zeichen == '.')
				{ // vorhandenen Wert beibehalten
				LokalTextAusgabeP(OkStrP);
				return 1;
				}
			else if (zeichen != '#' && zeichen > ' ') // nicht ungültig und kein Leerzeichen
				TrennZeichen = zeichen;
			} // else Trennzeichen == '\0'

		} // while true
	} // LokalCodefolgeEingabe

		
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
	FernschrIO(false);

	set_LEDGRUEN();
	set_LEDROT();
	
	if (!FsEinschalten())
		{ // Timeout...
		clr_LEDGRUEN();
		GeAusschalten(true);
		KommendSperren(SperreStoerung);
		clr_LEDROT();
		return;
		}

	clr_LEDROT();
	
	if (GeEinschalten() != GeEinschAnrufquitt)
		{
		GeAusschalten(true);
		FsAusschalten();
		}
	else
		VerbindungSteht(false); // Keine automatische Kennungsgeber-Abfrage
		
	}
	

/////////////////////////////////////////////////////////////

//! Wickelt die gehende Wahl ab bei nicht vorhandener Wählscheibe.
//----------------------------------------------------------------
//! Tastaturwahl. 
//! \retval true bei erfolgreichem Verbindungsaufbau.

static bool WahlMitTastatur()
	{
	char c;
	bool EsWurdeGewaehlt;
	int Falschziffern;
	
	if (!FsEinschalten())
		return false;

	SeriellUmsetzInit();
		
	// Wahlaufforderung ausgeben
	// -------------------------
	LokalCodeAusgabeS(WahlaufforderungZeichen, false);
		
	// Wahlziffern entgegennehmen, Break bricht ab
	// -------------------------------------------
	EsWurdeGewaehlt = false;
	Falschziffern = 0;
	BuZiMode = ZiMode; // Annehmen, dass die Ziffern-Ebene aktiv ist.

	SendeUmsetzModus = UmsetzLokal;
	EmpfUmsetzModus = UmsetzFern; // Vorbereitend für den Zustand nach Verbindungsaufbau
	
	while (true)
		{
		FernschrIO(true);
		
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
			}

		while (Falschziffern > 0 && TimerVal(&RuheTimer) >= 100)
			{
			LokalZeichenAusgabe('?');
			Falschziffern--;
			}

		if (BreakSignal || KoAusschalten())
			{
			// FsAusschalten() macht die aufrufende Routine
			return false;
			}
			
		if (KoEinschalten())
			{
			uint8_t i; // index in die "Verbunden"-Zeichenfolge
			bool Abbruch;

			BefehlMark = true;
			MeldungMark = true;
			Abbruch = false;

			GeSendeMark(true);

			// Im Modul TxP2-Endgerät ist nun der Zustand "Eingeschaltet" bereits erreicht, 
			// somit läuft die SeriellUmsetzung mit dem "SendePuffer".
			
			SendeUmsetzModus = UmsetzLokal;
			EmpfUmsetzModus = UmsetzFern;
			
			i = 0;
			while (true)
				{
				if (!PufferLeer(&EmpfPuffer))
					Abbruch = true; 
					// es wurde ein sinnvolles Zeichen von der Gegenstelle gesendet, da kommen bestimmt noch mehr.
				
				set_LEDROT();
				if (SerUmSendBitNr == SerUmSendWarte)
					{
					if (Abbruch)
						{
						PufferInit(&SendePuffer); // löscht noch ggf. vorhandene Zeichen im Puffer
						break; // Gegenstelle sendet, daher selbst nicht mehr schreiben.
						}
					else if (PufferLeer(&SendePuffer))
						{ // nächstes Zeichen ist dran
						if (i >= MaxCodefolgeLaenge || VerbindungHergestelltZeichen[i] > 0x1F)
							break; // nichts mehr zu senden
						else
							PufferSpeich(&SendePuffer, VerbindungHergestelltZeichen[i++]);
						}
					}
				
				BefehlMark = KoEmpfMark(); 
					// aufgrund der laufenden Umsetzung wird hier jetzt 
					// das Bitefolge von VerbindungHergestelltZeichen an den Fs weitergegeben.
				FernschrIO(true);
				}

			clr_LEDROT();
			
			return true;
			}

		if (TimerVal(&RuheTimer) > (EsWurdeGewaehlt ? 30000 : 15000)) // 15 / 30 Sekunden nicht gewählt
			{ 
			// GeAusschalten() und FsAusschalten() macht die aufrufende Routine
			return false;
			}

		} // while (true)

	} // WahlMitTastatur()
	
  
/////////////////////////////////////////////////////////////

//! Wickelt ausgehende Verbdindungen vollständig ab.
//--------------------------------------------------
//! Ruft VerbindungSteht() auf. Kehrt erst nach Verbindungsabbau wieder zurück.

static void VerbindungGehend()
	{
	if (BusEigenAdresse == BusAdrUngueltig)
		{
		FsAusschalten();
		return;
		}

	SperrzeitAussetzen();
		
	set_LEDGELB();
	
	switch (GeEinschalten())
		{ // hier nur break benutzen, wenn Einschaltung erfolgreich
		case GeEinschFehler:
			clr_LEDGELB();
			
			return; 

		case GeEinschWahl:
			if (WahlMitTastatur())
				// Einschalten macht WahlMitTastatur()
				break; // ist jetzt verbunden
				
			GeAusschalten(true);
			FsAusschalten();
			BreakSignal = false;
			
			if (KommendSperreWahl != 0 && LetzteInterneWahl() == KommendSperreWahl)
				{
				clr_LEDGELB();
				KommendSperren(SperreWahl);
				}
				
			return;

		default:
			FehlerStop(15); // TODO
			return;
		}

	VerbindungSteht(true); // automatische Kennungsgeber-Abfrage
	// VerbindungSteht(false); // HACK aufblenden zur Fehlersuche.
	
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
	TMsTimer KennungAbfrageTimer; // für die Kennungsgeber-Abfrage nach einem gehenden (aktiven) Verbindungsaufbau
	bool ErsteKennungAbfrage; // für die Kennungsgeber-Abfrage nach einem gehenden (aktiven) Verbindungsaufbau
	uint8_t KennungAusgabePhase; 
		// für die Simulation eines eingebauten Kennungsgebers:
		// 0 = Grundstellung / Buchstaben-Ebene, 1 = Ziffern-Ebene, 2 = letztes Zeichen war WerDa, warte noch Schreibpause ab.
	
	StartTimer(&KennungAbfrageTimer);
	ErsteKennungAbfrage = true;
	
	KennungAusgabePhase = 0;

	GeSendeMark(true); 
	BefehlMark = true;

	SendeUmsetzModus = UmsetzLokalUndFern; // Für Sendung des "WerDa" \todo das muss ich nochmal durchdenken...
	EmpfUmsetzModus = UmsetzLokalUndFern; // Für Empfang von "Antworten" 

	FernschrIO(true);
	
	while (true)
		{
		FernschrIO(true);

		BefehlMark = KoEmpfMark();
	
		GeSendeMark(MeldungMark); 
			
		// Auswertung des Empfangspuffers: 
		// a) Jedes Zeichen außer Buchstaben-Umschaltung beendet die Abfrage des 'fernen' Kennungsgebers.
		// b) Internen Kennungsgeber-'Simulator' ansteuern
		while (!PufferLeer(&EmpfPuffer))
			{
			uint8_t code = PufferAusg(&EmpfPuffer);
			if (code == TtyCodeBuUm)
				KennungAusgabePhase = 0; // Aufgabe b)
			else
				{
				AutoKennungAbfrage = false; // Aufgabe a)
				if (code == TtyCodeZiUm)
					KennungAusgabePhase = 1;
				else if (code == TtyCodeZiWerDa && KennungAusgabePhase == 1)
					KennungAusgabePhase = 2;
				else if (KennungAusgabePhase == 2)
					KennungAusgabePhase = 1; 
						// jedes andere Zeichen schaltet 'anstehende' Kennungsausgabe wieder ab.
				}
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

		// Kennungsgeber-Simulator bearbeiten:
		if (!MeldungMark) 
			KennungAusgabePhase = 0; 
				// sobald selbst geschrieben wird wird Kennungsgeber-Ausgabe wieder in Grundstellung
				// gesetzt.
				
		if (KennungAusgabePhase == 2 && TimerVal(&RuheTimer) > 800)
			{
			for (uint8_t i = 0 ; i < MaxCodefolgeLaenge && EigeneKennung[i] <= 0x1F; i++)
				PufferSpeich(&SendePuffer, EigeneKennung[i]);
				KennungAusgabePhase = 0;
			}
		
		// Test:
		// bset_LEDROT(AutoKennungAbfrage);
		
		//! \todo: Getippte Zeichen auswerten auf Ende-Zeichenfolge
		//---------------------------------------------------------
		
		if (BreakSignal)
			{
			GeAusschalten(false);
			while (!KoAusschalten())
				FernschrIO(false);
			FsAusschalten();
			BreakSignal = false;
			return;
			}

		if (KoAusschalten())
			{
			GeAusschalten(false); // da braucht auf nichts mehr gewartet zu werden
			FsAusschalten();
			return;
			}
		
		} // while true
		
	} // VerbindungSteht()


	
/////////////////////////////////////////////////////////////

//! Eingabe einer Zeichenfolge (Einfach)
// ---------------------------
//! \returns  false bei Abbruch

/* Zurückgestellt, da das Ergänzen der notwendigen Codes vor und nach der Texteingabe mittel aufwändig ist
static bool KonfigTextEingabeA(PGM_P Prompt, uint8_t* CodeBuf, uint8_t MaxCodes)
	{
	char EingabePuffer[40]; // Speichert Text-Eingaben in ASCII
	uint8_t p; // in EingabePuffer
	uint8_t cp; // in CodeBuf
	uint8_t mode; // Buchstaben oder Ziffern
	uint8_t c1, c2; // Code-Zeichen
	
	EingabePuffer[sizeof(EingabePuffer)-1] = '\0';
	
	LokalTextAusgabeP(Prompt);
	switch (LokalTextEingabe(EingabePuffer, sizeof(EingabePuffer)-1))
		{ 
		case 0: 
			return false;
		case 1:
			return true;
		case 2:
			mode = 0;
			cp = 0;
			for (p = 0 ; p < sizeof(EingabePuffer) && EingabePuffer[p] != '\0' && cp < MaxCodes ; p++)
				{
				if (ZeichenZuCode(EingabePuffer[p], &mode, &c1, &c2))
					{
					// c1 ist auf jeden fall gültig
					CodeBuf[cp++] = c1;
					if (cp < MaxCodes && c2 != 255)
						CodeBuf[cp++] = c2;
					}
				}
		
			return true;
		}
	
	return false; // kann eigentlich nicht sein
	}
*/

/////////////////////////////////////////////////////////////

//! Behandelt die Selbstkonfiguration des Moduls.
//-----------------------------------------------
//! Arbeitet mit dem angeschlossenen Endgerät zusammen.

static void Konfiguration()
	{
	uint8_t Res;
	
	SeriellUmsetzInit();
	BuZiMode = '\0';
	Aktivieren(false);
	set_LEDROT();
	
	if (!FsEinschalten())
		return;
	
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR("\r\n configuration FsOFsg version " SVNVERSION " date " __DATE__));
#else
	LokalTextAusgabeP(PSTR("\r\n konfiguration FsOFsg version " SVNVERSION " datum " __DATE__));
#endif //def SPRACHE_EN

	
	// Durchwahl...
	if (!KonfigurationAllgemein())
		return;

	if (BusEigenAdresse != eeprom_read_byte(&EEDaten.BusEigenAdresse))
		eeprom_update_byte(&EEDaten.BusEigenAdresse, BusEigenAdresse);
	
	// Einschaltung der Sperre für kommende Rufe durch Wahl von...
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

	if (LokalZahlEingabe(&KommendSperreWahl, 0) < 0)
		return;

	if (KommendSperreWahl != eeprom_read_byte(&EEDaten.KommendSperreWahl))
		eeprom_update_byte(&EEDaten.KommendSperreWahl, KommendSperreWahl);
	
	LokalTextAusgabeP(OkStrP);

#ifdef SPRACHE_EN
	Res = LokalCodefolgeEingabe(PSTR("\r\n software answerback:      "), EigeneKennung, MaxCodefolgeLaenge);
#else
	Res = LokalCodefolgeEingabe(PSTR("\r\n software kennungsgeber:      "), EigeneKennung, MaxCodefolgeLaenge);
#endif //def SPRACHE_EN
	if (Res == 2)
		eeprom_update_block(EigeneKennung, EEDaten.EigeneKennung, sizeof(EEDaten.EigeneKennung));
	if (Res == 0 || BreakSignal)
		return;
	
#ifdef SPRACHE_EN
	Res = LokalCodefolgeEingabe(PSTR("\r\n prompt to dial:      "), WahlaufforderungZeichen, MaxCodefolgeLaenge);
#else
	Res = LokalCodefolgeEingabe(PSTR("\r\n wahlaufforderung:      "), WahlaufforderungZeichen, MaxCodefolgeLaenge);
#endif //def SPRACHE_EN
	if (Res == 2)
		eeprom_update_block(WahlaufforderungZeichen, EEDaten.WahlaufforderungZeichen, sizeof(EEDaten.WahlaufforderungZeichen));
	if (Res == 0 || BreakSignal)
		return;
	
#ifdef SPRACHE_EN
	Res = LokalCodefolgeEingabe(PSTR("\r\n connection confirmation:      "), VerbindungHergestelltZeichen, MaxCodefolgeLaenge);
#else
	Res = LokalCodefolgeEingabe(PSTR("\r\n verbindungsbestaetigung:      "), VerbindungHergestelltZeichen, MaxCodefolgeLaenge);
#endif //def SPRACHE_EN
	if (Res == 2)
		eeprom_update_block(VerbindungHergestelltZeichen, EEDaten.VerbindungHergestelltZeichen, sizeof(EEDaten.VerbindungHergestelltZeichen));
	if (Res == 0 || BreakSignal)
		return;
	
#ifdef SPRACHE_EN
	Res = LokalCodefolgeEingabe(PSTR("\r\n connection closed sign:      "), AusschaltZeichen, MaxCodefolgeLaenge);
#else
	Res = LokalCodefolgeEingabe(PSTR("\r\n meldung verbindungsabbau:      "), AusschaltZeichen, MaxCodefolgeLaenge);
#endif //def SPRACHE_EN
	if (Res == 2)
		eeprom_update_block(AusschaltZeichen, EEDaten.AusschaltZeichen, sizeof(EEDaten.AusschaltZeichen));
	if (Res == 0 || BreakSignal)
		return;

	if (!SperrzeitEingabeDialog())
		return;
	
	SperrzeitSpeicherEeprom(&EEDaten.SperrzeitDaten);
	
	// Ende-Kennung druckt FsAusschalten()
	} // Konfiguration()


/////////////////////////////////////////////////////////////

//! Beendet die Selbstkonfiguration des Moduls.
//-----------------------------------------------
//! Arbeitet mit dem angeschlossenen Endgerät zusammen.

static void KonfigurationEnde()
	{
	BreakSignal = false;
	FsAusschalten();
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
			
		FernschrIO(false);
		
		if (!MeldungMark) // Taste am Fernschreiber gedrückt --> raus aus der Sperre
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

	
/////////////////////////////////////////////////////////////

//! Liest aus dem EEPROM einen Datenblock als Codefolge, prüft ob dieser Block
//! korrekt ist (nur Werte von 0 bis 31 und CodefolgeEndeMarke) und initialisiert
//! ggf. ungültige Codefolgen
//------------------------------------------------------------

void CodefolgeLadenPruefenInitialisieren(uint8_t* cf, uint8_t size, uint8_t* cf_eep, uint8_t* cf_default, uint8_t def_size)
	{
	eeprom_read_block(cf, cf_eep, size);
	
	for (uint8_t i = 0 ; i < size ; i++)
		{
		if (cf[i] == CodefolgeEndeMarke) 
			break; // alles ist schön
		else if (cf[i] > 31)
			{
			memcpy_P(cf, cf_default, def_size);
			cf[def_size] = CodefolgeEndeMarke;
			return;
			}
		}
	} // CodefolgeLadenPruefenInitialisieren()


/////////////////////////////////////////////////////////////

//! Das Hauptprogramm der Fernschreiber-Schnittstelle.
//---------------------------------------------------------


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
	init_FS_AUSG();
	init_FS_AKTIV();
	init_FS_EING(); 
#ifdef PARALLELAUSGABE
	init_FS2_AUSG();
	init_FS2_EING(); 
#endif //def PARALLELAUSGABE
	init_TASTE();
	//init_TASTE2();

	set_LEDROT();

	// Timer initialisieren
	MsTimerInit();
	
	BusEigenAdresse = eeprom_read_byte(&EEDaten.BusEigenAdresse) & 0xFE;
	if (BusEigenAdresse < BusAdrMin || BusEigenAdresse > BusAdrMax)
		BusEigenAdresse = 35 << 1; // Standardwert 
		//! \todo Besser BusAdrUngueltig testen
	BusEigenAdrMehrfach = 1;
	
	KommendSperreWahl = eeprom_read_byte(&EEDaten.KommendSperreWahl);
	if (KommendSperreWahl > 99)
		KommendSperreWahl = 0;

	CodefolgeLadenPruefenInitialisieren(AusschaltZeichen, sizeof(AusschaltZeichen), EEDaten.AusschaltZeichen, 
										AusschaltZeichenDefault, sizeof(AusschaltZeichenDefault));
	CodefolgeLadenPruefenInitialisieren(WahlaufforderungZeichen, sizeof(WahlaufforderungZeichen), EEDaten.WahlaufforderungZeichen, 
										WahlaufforderungZeichenDefault, sizeof(WahlaufforderungZeichenDefault));
	CodefolgeLadenPruefenInitialisieren(VerbindungHergestelltZeichen, sizeof(VerbindungHergestelltZeichen), EEDaten.VerbindungHergestelltZeichen, 
										VerbindungHergestelltZeichenDefault, sizeof(VerbindungHergestelltZeichenDefault));
	CodefolgeLadenPruefenInitialisieren(EigeneKennung, sizeof(EigeneKennung), EEDaten.EigeneKennung, NULL, 0);
	
	BefehlMark = true;
	MeldungMark = true;
	BreakSignal = false;

	SperrzeitInit();

	SperrzeitLadeEeprom(&EEDaten.SperrzeitDaten);

	KommInit();

	FernschrIO(false);
	
	TMsTimer Timer;
	StartTimer(&Timer);

	sei();
	
	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 250)
		;
	
	// Bei Tastendruck Watchdog AUS
	if (!get_TASTE())
		{ // Gedrückt = LOW	
		wdt_disable();
		while (!get_TASTE())
			; // Warten, bis Taste wieder losgelassen
		set_LEDGELB();
		StartTimer(&Timer);
		}

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

	BusEigenAdressePruefenUndSetzen(BusEigenAdresse);

/*/ Selbsttest

	BefehlEinschalten = false;
	MeldungEingeschaltet = false;
	BefehlMark = false;
	MeldungMark = false;

	while (1)
		{
		BefehlMark = BIT_IS_SET(TAST_IPORT, TAST_BIT);
			// Gedrückt = LOW

		FernschrIO();

		if (BefehlEinschalten) 		LED_EIN(ROT); 	else LED_AUS(ROT);
		if (MeldungEingeschaltet) 	LED_EIN(GELB); 	else LED_AUS(GELB);
		if (BefehlMark) 			LED_EIN(GRUEN); else LED_AUS(GRUEN);
		if (MeldungMark)			LED_EIN(BLAU); 	else LED_AUS(BLAU);

		BefehlEinschalten = MeldungEingeschaltet;

		}

// Selbsttest Ende */

	BefehlMark = true;

	while (1)
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
		FernschrIO(false);

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
			Deaktivieren();
			BreakSignal = false;
			Tastendruck = NichtGedr;
			}

		if (!MeldungMark)
			{
			VerbindungGehend();
			Tastendruck = NichtGedr; // falls die Taste als Break-Ersatz benutzt wurde.
			}
			
		if (KoEinschalten())
			{
			VerbindungKommend();
			Tastendruck = NichtGedr; // falls die Taste als Break-Ersatz benutzt wurde.
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


