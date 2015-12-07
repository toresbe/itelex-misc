//================================================================
// Fernschreiber-Schnittstelle Einfachstrom ohne Fernschaltgerät für TxP2-System
//	für ATmega8 auf Platine FernschrTW39
//================================================================
// Fernschreiber muss über einen Zeitschalter (Motorschalter) verfügen.
// Tastaturwahl, nach drücken der ersten Taste wird eine Wahlaufforderung in form von "ga" gesendet. 
// Verbindungsende wahlweise durch "break"-Signal, durch NNNN oder durch +++

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


//! Marker im Code als Identifikation
PROGMEM const char Identifier[] = "___TxP2_OhneFSG___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";


typedef enum { EndeNurBreak, EndeNachNNNN, EndeNach3Plus } TVerbindungsEndeKriterium;


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

enum { MaxCodefolgeLaenge = 30 }; //!< Maximale Länge von #AusschaltZeichen, #Wahlaufforderung, #VerbindungHergestelltZeichen

uint8_t AusschaltZeichen[MaxCodefolgeLaenge+1]; 
	//!< Druck-Sequenz als Zeichen für Ende der Verbindung.
	//!< Irgendwas > 0x1F (also mehr als 5 Bit) markiert Sequenz-Ende.

PROGMEM uint8_t AusschaltZeichenDefault[] = { TtyCodeBuUm, TtyCodeBuUm, TtyCodeBuUm, TtyCodeWR, TtyCodeZL, 6, 6, 6, 6, TtyCodeWR, TtyCodeZL, TtyCodeZL, 255 } ; // NNNN
	//!< Standardwert für #AusschaltZeichen.
	// Manuell prüfen, dass es nicht mehr als MaxCodefolgeLaenge Zeichen sind!
	
uint8_t WahlaufforderungZeichen[MaxCodefolgeLaenge+1];
	//!< Druck-Sequenz als Zeichen jetzt zu wählen

PROGMEM uint8_t WahlaufforderungZeichenDefault[] = { TtyCodeBuUm, TtyCodeWR, TtyCodeZL, 
													 6, 10, TtyCodeLeer, TtyCodeLeer, TtyCodeLeer, TtyCodeLeer, TtyCodeLeer, TtyCodeZiUm, 255 } ; // NR _ _ _ _ _
	//!< Standardwert für #WahlaufforderungZeichen.
	// Manuell prüfen, dass es nicht mehr als MaxCodefolgeLaenge Zeichen sind!

uint8_t VerbindungHergestelltZeichen[MaxCodefolgeLaenge+1];
	//!< Druck-Sequenz nach Eingang der Verbindungsbestätigung
	
PROGMEM uint8_t VerbindungHergestelltZeichenDefault[] = { TtyCodeBuUm, TtyCodeLeer, 14, 3, 6, TtyCodeWR, TtyCodeZL, 255 } ; // CON
	//!< Standardwert für #VerbindungHergestelltZeichen.
	// Manuell prüfen, dass es nicht mehr als MaxCodefolgeLaenge Zeichen sind!
	
uint8_t EigeneKennung[MaxCodefolgeLaenge+1] = { TtyCodeBuUm, TtyCodeWR, TtyCodeZL, TtyCodeZiUm, 30, 26, 16, TtyCodeBuUm, TtyCodeLeer, 19, 9, 28, 19, TtyCodeBuUm, 255 };
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
	} TEepromDaten;


EEMEM TEepromDaten EEDaten; 
	// keine Initialisierung, da beim Einlesen der EEDaten eine Prüfung und ggf. Initialisierung mit 
	// Default-Werten stattfindet.


	
	
///////////////////////////////////////////////////////////////////////////////

//! bedient Hardware-IO entsprechend der aktuellen Zustände.
//----------------------------------------------------------
//! setzt FS_AUSG entsprechend BefehlMark,
//! setzt MeldungMark und BreakSignal entsprechend FS_EING,
//! steuert die Status-LEDs.

static void FernschrIO()
	{
	if (BefehlMark)
		{
		clr_LEDBLAU();
		set_FS_AUSG();
		
		if (get_FS_EING())
			{ // Schleifenstrom ist aus (negierter Eingang)
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

	} // FernschrIO
	
	

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
	
	BefehlMark = false;
	StartTimer(&AnlaufTimer);
	while (TimerVal(&AnlaufTimer) < 20)
		FernschrIO();
	BefehlMark = true;
	FernschrIO();
		
	StartTimer(&AbbruchTimer);
	while (TimerVal(&RuheTimer) < 1200) //! \todo Konfigurierbar
		{
		FernschrIO(); // Bearbeitet auch #RuheTimer
		if (MeldungMark)
			StartTimer(&AbbruchTimer); // bei Dauer-Space: kein Gerät angeschlossenen
		
		if (TimerVal(&AbbruchTimer) > 7000)
			return false;
		}
		
	return true;
	}
		

static void LokalCodeAusgabe(uint8_t code); // kommt erst später...

static void LokalCodeAusgabeS(uint8_t *codep); // kommt erst später...


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
		FernschrIO();

	LokalCodeAusgabeS(AusschaltZeichen);

	// noch eine weitere Sekunde warten
	while (TimerVal(&RuheTimer) < 1000)
		FernschrIO();
	
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
		FernschrIO();
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
		SeriellUmsetzung(MeldungMark, &BefehlMark);
		FernschrIO();
		}
	}


/////////////////////////////////////////////////////////////

//! Gibt ein Zeichenfolge am angeschlossenen Fs aus.
//--------------------------------------------------
//! \param code Zeichen im Baudot-Code

static void LokalCodeAusgabeS(uint8_t *codep)
	{
	uint8_t i;

	for (i = 0 ; i < MaxCodefolgeLaenge ; i++)
		{
		if (codep[i] > 0x1F)
			break;
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
	uint8_t code;
	
	if (c >= 'A' && c <= 'Z')
		c += 'a'-'A';

	if ((code = ZeichenZuCode(c, BuZiMode)) != 255)
		{
		LokalCodeAusgabe(code);
		}
	else if ((code = ZeichenZuCode(c, BuMode)) != 255)
		{
		LokalCodeAusgabe(TtyCodeBuUm);
		LokalCodeAusgabe(code);
		BuZiMode = BuMode;
		}
	else if ((code = ZeichenZuCode(c, ZiMode)) != 255)
		{
		LokalCodeAusgabe(TtyCodeZiUm);
		LokalCodeAusgabe(code);
		BuZiMode = ZiMode;
		}
	}
			
		
///////////////////////////////////////////////////////////////

//! Dialog-Abfrage für einen Text der als 5-Bit-Code abgespeichert wird.
//----------------------------------------------------------------------
//! Abschluss nur mit WR oder ZL.
//! WR, ZL oder Leerzeichen am Anfang wird ignoriert. 
//! nur Leerzeichen und WR oder ZL = löschen
//! . (Punkt) als einziges Zeichen vor WR oder ZL = alten Wert behalten.
//! \param[out] s Puffer des eingegebenen Textes.
//! \param[in] maxbuchst Anzahl erlaubter Zeichen bei der Eingabe, auch Puffergröße.
//! \retval 0 abbruch
//! \retval 1 unverändert
//! \retval 2 eingabe erfolgt
//! \todo mal nach KonfigDialog verschieben, da aber LokalZeichenLesen nicht verwendet werden kann, muss eine größere Umstellung gemacht werden.

/*
uint8_t LokalCodefolgeEingabe(uint8_t* s, uint8_t maxbuchst)
	{
	uint8_t Pos = 0; 
	char ErstesZeichen = 255; // Zeichen für noch nicht belegt.
	char BuZiMode = '\0';

	EmpfUmsetzModus = UmsetzLokal; // sicherheitshalber

	while (true)
		{ // Schleifendurchlauf einmal je Taste
		uint8_t code;
		
		while (true)
			{
			FernschrIO();
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
					s[Pos] = '\0';
				return 0;
				}
			}

		switch (code)
			{
			case TtyCodeWR:
			case TtyCodeZL:
				if (Pos == 0)
					if (ErstesZeichen == 255)
						break; // CR oder LF am Anfang ignorieren
					else
						if (ErstesZeichen == TtyCodeZiPunkt)
							return 1; // keine Änderung
						else if (ErstesZeichen == TtyCodeLeer)
							{
							s[0] = 255;
							return 2;
							}
						else
							{
							s[0] = ErstesZeichen;
							s[1] = '\0';
							return 2;
							}
				else // Pos > 0
					{
					s[Pos] = '\0';
					return 2;
					}

			case '#':
				// ungültig
				break;

			case ' ':
				if (Pos == 0)
					{
					ErstesZeichen = ' ';
					break; // hier abbrechen, sonst weiter wie bei Buchstaben...
					}

			default:
				if (Zeichen >= ' ' && Pos < maxbuchst)
					{
					if (Pos == 0)
						if (ErstesZeichen == '\0') 
							// erstes eingegebenes Zeichen
							ErstesZeichen = Zeichen;
						else if (ErstesZeichen == ' ')
							s[Pos++] = Zeichen;
						else
							{
							s[Pos++] = ErstesZeichen;
							s[Pos++] = Zeichen;
							}
					else
						s[Pos++] = Zeichen;
					}
				break;
					
			} // switch Zeichen
		} // while true
	} // LokalTextEingabe

/**/

		
static void VerbindungSteht(bool AutoKennungAbfrage);

static void Deaktivieren(bool WegenTimeout);


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
	
	if (!FsEinschalten())
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
	LokalCodeAusgabeS(WahlaufforderungZeichen);
		
	// Wahlziffern entgegennehmen, Break bricht ab
	// -------------------------------------------
	EsWurdeGewaehlt = false;
	Falschziffern = 0;
	BuZiMode = ZiMode; // Annehmen, dass die Ziffern-Ebene aktiv ist.

	SendeUmsetzModus = UmsetzLokal;
	EmpfUmsetzModus = UmsetzFern; // Vorbereitend für den Zustand nach Verbindungsaufbau
	
	while (true)
		{
		FernschrIO();
		
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
			
			// ganz brutal:
			//SeriellUmsetzInit(); // ggf noch Puffer leeren?
			
			i = 0;
			while (true)
				{
				if (!PufferLeer(&EmpfPuffer))
					{
					if (PufferAusg(&EmpfPuffer) != TtyCodeBuUm) 
						// Buchstaben-Umschaltung wird ignoriert, da dies auch ein 
						// Störimpuls gewesen sein kann.
						Abbruch = true;
					}
				
				set_LEDROT();
				if (PufferLeer(&SendePuffer) && SerUmSendBitNr == SerUmSendWarte)
					{ // nächstes Zeichen ist dran
					if (i >= MaxCodefolgeLaenge || VerbindungHergestelltZeichen[i] > 0x1F)
						break; // Auch ohne Ende-Zeichen ist die Zeichenkette jetzt beendet.
					if (Abbruch)
						break; // Gegenstelle sendet, daher selbst nicht mehr schreiben.
					PufferSpeich(&SendePuffer, VerbindungHergestelltZeichen[i++]);
					}
				BefehlMark = KoEmpfMark(); 
					// aufgrund der leufenden Umsetzung ist hier jetzt 
					// das Bitefolge von VerbindungHergestelltZeichen gemeldet.
				FernschrIO();
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
	
  
static void KommendSperren();


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
				
			GeAusschalten();
			FsAusschalten();
			
			clr_LEDGELB();

			if (KommendSperreWahl != 0 && LetzteInterneWahl() == KommendSperreWahl)
				KommendSperren(); 
				// Funktion kommt erst zurück wenn Taste gedrückt oder neu gewählt.
				
			return;

/*
		case GeEinschSofortEin:
			TW39Einschalten();
			break; // ist jetzt Verbunden

		case GeEinschFremdKonfig:
			TW39Einschalten();
// passt nicht mehr...			LeitungsSstKonfigurationsDialog();
			TW39Ausschalten();
			GeAusschalten();
			return; // keine normale Verbindung
*/

		default:
			FehlerStop(15); // TODO
			return;
		}

	// HACK VerbindungSteht(true); // automatische Kennungsgeber-Abfrage
	VerbindungSteht(false); // HACK aufblenden zur Fehlersuche.

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

	SendeUmsetzModus = UmsetzFern; // Für Sendung des "WerDa"
	EmpfUmsetzModus = UmsetzLokalUndFern; // Für Empfang von "Antworten" 
	
	while (true)
		{
		FernschrIO();
		TastePruefen();

		if (BreakSignal || KoAusschalten() || Tastendruck != NichtGedr) // HACK Tastendruck wegen fehlender Break-Taste
			{
			BreakSignal = false;
			Tastendruck = NichtGedr;
			FsAusschalten();
			GeAusschalten();
			return;
			}

		BefehlMark = KoEmpfMark();
	
		// HACK: dies 'if' sollte nicht mehr erforderlich sein nach der Umstellung in 
		// HACK: TxP2-Endgeraet.c...: if (SerUmSendBitNr <= SerUmSendStart) // Start oder Warten...
			GeSendeMark(MeldungMark); // Nur Fs-Pegel direkt auf Bus, wenn nicht seriell gesendet wird...
			
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
				// HACK else if (code == TtyCodeZiWerDa && KennungAusgabePhase == 1)
				else if (code == TtyCodeZiKlingel && KennungAusgabePhase == 1)
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
	
	if (!FsEinschalten())
		return;
	
	LokalTextAusgabeP(PSTR("\r\n configuration FsOFsg version " SVNVERSION " date " __DATE__));

	// Durchwahl...
	if (!KonfigurationAllgemein())
		return;

	if (BusEigenAdresse != eeprom_read_byte(&EEDaten.BusEigenAdresse))
		eeprom_write_byte(&EEDaten.BusEigenAdresse, BusEigenAdresse);
	
	// Einschaltung der Sperre für kommende Rufe durch Wahl von...
	LokalTextAusgabeP(PSTR("\r\n block incoming calls by: (cur. "));
	if (KommendSperreWahl != 0)
		LokalZahlAusgabe(KommendSperreWahl, 2);
	else
		LokalTextAusgabeP(PSTR("no"));
	LokalTextAusgabeP(PSTR(") new (0 = no):     "));

	if (LokalZahlEingabe(&KommendSperreWahl, 0) == 0)
		return;

	if (KommendSperreWahl != eeprom_read_byte(&EEDaten.KommendSperreWahl))
		eeprom_write_byte(&EEDaten.KommendSperreWahl, KommendSperreWahl);

	LokalTextAusgabeP(OkStrP);

	//! \todo Zeichenfolgen for Signalisierung editierbar machen. Aber welches Ende-Zeichen?
	
	// weitere Eingaben

	// Ende-Kennung druckt FsAusschalten()
	}


/////////////////////////////////////////////////////////////

//! Beendet die Selbstkonfiguration des Moduls.
//-----------------------------------------------
//! Arbeitet mit dem angeschlossenen Endgerät zusammen.

static void KonfigurationEnde()
	{
	FsAusschalten();
	Aktivieren(true);
	clr_LEDROT();
	}
	
	
/////////////////////////////////////////////////////////////

//! Schaltet das Modul in einen Modus, der keine kommenden Verbindungen zulässt.
//------------------------------------------------------------------------------
//! Kann durch Wahl einer entsprechenden Ziffernfolge aufgerufen werden oder
//! durch Tastendruck an der Platine.
	
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
			
		FernschrIO();
		
		if (!MeldungMark) // Taste am Fernschreiber gedrückt --> raus aus der Sperre
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
	
	
DEFPORTOUT		(FS_AKTIV,	D, 7) 
// HACK: Zum Test mit der TW39 muss bei angeschlossenem Fernschaltgerät dieses auf Dauer-Ein geschaltet werden.


/////////////////////////////////////////////////////////////

//! Schaltet das Modul in einen Modus, der keine kommenden und keine gehenden 
//! Verbindungen zulässt.
//------------------------------------------------------------------------------
//! Kann nur durch Tastendruck an der Platine aktiviert werden.
	
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


/////////////////////////////////////////////////////////////

//! Das Hauptprogramm der Fernschreiber-Schnittstelle.
//---------------------------------------------------------


int main()
	{
#ifndef NOWATCHDOG
	wdt_enable(WDTO_2S);
#endif //NOWATCHDOG

	// nur für den Simulator:
	PINB = 0xFF;
	PINC = 0xFF;
	PIND = 0xFF;

	// Ports initialisieren
	init_LEDROT();
	init_LEDGELB();
	init_LEDGRUEN();
	init_LEDBLAU();
	init_FS_AUSG();
	init_FS_EING(); 
#ifdef PARALLELAUSGABE
	init_FS2_AUSG();
	init_FS2_EING(); 
#endif //def PARALLELAUSGABE
	init_TASTE();
	//init_TASTE2();

	init_FS_AKTIV(); // HACK
	
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

	//! \todo Zeichenfolgen aus EEPROM holen
	memcpy_P(AusschaltZeichen, AusschaltZeichenDefault, sizeof(AusschaltZeichenDefault));
	memcpy_P(WahlaufforderungZeichen, WahlaufforderungZeichenDefault, sizeof(WahlaufforderungZeichenDefault));
	memcpy_P(VerbindungHergestelltZeichen, VerbindungHergestelltZeichenDefault, sizeof(VerbindungHergestelltZeichenDefault));
	
	BefehlMark = true;
	MeldungMark = true;
	BreakSignal = false;

	KommInit();

	FernschrIO();
	
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
		FernschrIO();

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

		if (!MeldungMark)
			{
			VerbindungGehend();
			}
			
		if (KoEinschalten())
			{
			VerbindungKommend();
			}
		
		} // while (1)
	} // main()


