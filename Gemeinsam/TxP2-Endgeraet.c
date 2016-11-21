#include "TxP2-Endgeraet.h"

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>

#include "bits.h"
#include "timercs.h"

#include "TxP2-Defs.h"

#include "MsTimer.h"
#include "BusKomm.h"
#include "FifoPuffer.h"
#include "BaudotCode.h"
#include "SeriellUmsetz.h"


// HACK (Gilt nur für TW39):
//#define HACK_ROT_EIN SET_BIT(PORTD,1);
//#define HACK_ROT_AUS CLR_BIT(PORTD,1);


// Allgemeine (typunabhägige) Schnittstelle zum Fernschreiber 
// ----------------------------------------------------------

typedef enum { Ausgeschaltet,   //!< Grundstellung = Ausgeschaltet
			   Reserviert,      //!< von Gegenstelle aktiviert aber noch nicht eingeschaltet.
			   Wahl, 			//!< es darf gewählt werden. Interne und Externe Wahl wird hier nicht unterschieden.
			   EinschaltungKo,  //!< von Gegenstelle eingeschaltet.
               Eingeschaltet,   //!< ist eingeschaltet, Verbindung steht. (keine Unterscheidung ob kommend oder gehend).
			   AusschaltungKo,  //!< Gegenstelle hat Verbindungsabbau eingeleitet.
			   AusschaltungGe,  //!< An Gegenstelle wurde Wunsch zum Verbindungsabbau gesendet, noch keine Bestätigung erhalten.
			   //FremdKonfig,     // wird nicht mehr benutzt.
			   Inaktiv 			//!< Deaktiviert (nicht erreichbar).
			   } TFsBetriebsart; //!< Möglichkeiten für den aktuellen Betriebszustand des Endgeräts.
			   

static char AusschaltCode; //!< Grund für Abschaltung

volatile TFsBetriebsart FsBetriebsart; //!< Aktuelle Phase der Verbindung.
	
static volatile bool FsEingMark; 
	//!< wird von Schnittstellenprogramm gesetzt (vom Fernschreiber)
	//!< Also das von der Tastatur kommende Signal (gehend)

static volatile bool FsAusgMark; 
	//!< wird an das Schnittstellenprogramm gemeldet (an Fernschreiber)
	//!< Also das an das Schreibwerk gehende Signal (kommend)

// in BusKomm.h definiert: bool BusEmpfMark	
	// per TWI-Bus empfangenes Signal (zum Fernschreiber, kommend)

// nur lokal in Funktion BusKomm definiert: bool BusSendMark
	// per TWI-Bus gesendetes Signal (vom Fernschreiber, gehend)


// Variablen für Testfunktionen, die ein "spezielles" Verhalten des Endgeräts erzwingen
// ------------------------------------------------------------------------------------

#ifdef TESTFUNKTIONEN

//! Index der Testfunktion (siehe #TestfnNormal und folgende).
uint8_t TestFunktion; 

#define TestfunktionAktiv(x) (TestFunktion == (x))


//! Künstliche Verzögerungszeit in 0,1-Sekunden-Einheiten für 
//! #TestfnEinschaltVerzoegerung 
//! und #TestfnAusschaltVerzoegerung und #TestfnEinschaltAblehnung 
uint8_t TestVerzoegerung;

//! Zeitmesser für die künstlichen Verzögerungen bei Tests.
TMsTimer TestVerzTimer;


#else //!def TESTFUNKTIONEN

#define TestfunktionAktiv(x) false

#define TestVerzoegerung 0
	
#endif //def TESTFUNKTIONEN



// Debug-Speicher
// --------------

#ifdef TWI_DEBUG

//! Maximale Anzahl von Ereignissen im DebugBuf.
#define DebugBufLen 300

//! Speichert die Ereignisfolge.
static uint8_t DebugBuf[DebugBufLen];
	
//! Aktuelle Schreibposition in DebugBuf.
static volatile uint8_t* DebugBufP;

#endif //TWI_DEBUG



// Debug-Hilfen
// ------------

#ifdef TWI_DEBUG

//! Speichert ein Ereignis in DebugBuf.
void DebSp(uint8_t x)
	{
	if (DebugBufP < DebugBuf + DebugBufLen - 1)
		{
		*DebugBufP = x;
		DebugBufP++;
		}
	}

static void AblaufMark(uint8_t Code)
	{
	uint8_t SregAlt = SREG;
	cli();
	DebSp(0xFF);
	DebSp(Code);
	SREG = SregAlt;
	}
	
#else //TWI_DEBUG

#define AblaufMark(Code) while(0)

#endif //TWI_DEBUG


extern void FehlerStop(int Nummer);
// Funktion ist durch Applikation zu definieren.


//! sperrt das Interrupt-Basierte Bearbeiten ggf. eintreffender Bus-Kommandos.
static volatile bool BusKommSperre = false;

//! Anzahl gewählte Ziffern.
static volatile uint8_t WahlZifferAnzahl;

//! TWI-Adresse für interne Verbindungen.
static volatile uint8_t InterneNummer;


//! Speichert, welcher Pegelzustand der Gegenstelle gemeldet wurde.
static volatile enum { Mark1, 	//!< Es wurde BusKdoMark gesendet, aber noch nicht BusKdoMarkWdh.
					   Mark2,	//!< Es wurde BusKdoMark und BusKdoMarkWdh gesendet.
					   Space1,  //!< Es wurde BusKdoSpace gesendet, aber noch nicht BusKdoSpaceWdh.
					   Space2   //!< Es wurde BusKdoSpace und BusKdoSpaceWdh gesendet.
					   } GesendeterPegelStatus = Mark2; 

//! Unterscheidung von Phasen bei der Wahl.					   
static volatile enum { WahlGesperrt,  	//!< Wählen zur Zeit nicht erlaubt.
					   WahlIntern, 		//!< interner Verbindungspartner noch nicht definiert.
					   WahlPause, 		//!< interner Verbindungspartner definiert, Wahl nach Extern noch nicht möglich
										//!< (interner Verbindungspartner hat noch nicht BusKdoWahlFreigabe gesendet).
					   WahlExtern    	//!< Wahlziffern werden an internen Verbindungspartner weitergegeben.
					   } WahlPhase;	

					   
//! Zeitgeber für die Sendung von BusKdoMarkWdh und BusKdoSpaceWdh.					   
static TMsTimer PegelWdhTimer;

//! Bei selbst initiierter Ausschaltung wird nur begrenzt auf die Bestätigung der Ausschaltung gewartet.
static TMsTimer AusschaltQuittTimer;

//! Puffert an die Gegenstelle zu sendende a) Wahlziffern und b) Baudot-Codes.
TPuffer SendePuffer;

//! Während des Empfangs werden empfangene Zeichen auch dekodiert und in diesem Puffer
//! abgelegt (Baudot-Codes).
TPuffer EmpfPuffer;


//! Wo werden die aus dem #SendePuffer auszugebenden Zeichen gedruckt.
TUmsetzModus SendeUmsetzModus;

//! Welche Seite wird ausgewertet um den #EmpfPuffer zu füllen.
TUmsetzModus EmpfUmsetzModus;



//! Ist das Endgerät gerade ausgeschaltet?
bool BetriebsartAusgeschaltet()
	{
	return FsBetriebsart == Ausgeschaltet;
	}


//! Umschaltung der Betriebsart.
//------------------------------
//! Wirkt letztendlich auf die globale Variable FsBetriebsart, macht aber auch
//! Plausibilitätsprüfungen, setzt die Variable Status (die über den TWI-Bus abgefragt
//! werden kann) und sendet ggf. Kommandos an den aktuellen Verbindungspartner.
//! \param neu Gewünschte Betriebsart. Darf auch die schon eingestellte Betriebsart sein.
	
void BetriebsartWechsel(TFsBetriebsart neu)
	{
	if (neu == FsBetriebsart)
		return;

	uint8_t SregAlt = SREG;
	cli();

	WahlPhase = WahlGesperrt; // wird vielleicht bei neu == Wahl nochmal anders gesetzt
	
	switch (neu)
		{
		case Inaktiv:
			CLR_BIT(Status, StatBit_Frei); // kein CLR_BIT_Status, weil sonst das Interrupt-Flag wieder gesetzt wird
			CLR_BIT(Status, StatBit_SpezialGeraetKennung); // weil dieses Bit nur bei StatBit_Frei = 1 erlaubt ist
			BusVerbPartner = 0;
			break;

		case Ausgeschaltet:
			Status &= (1<<StatBit_BusKdoEmpfangen); // alle anderen Bits löschen
			SET_BIT(Status, StatBit_Frei); // kein SET_BIT_Status, weil sonst das Interrupt-Flag wieder gesetzt wird
			CLR_BIT(Status, StatBit_LeitungKennung); // es ist keine Leitung, löscht auch ggf. StatBit_AngerufenBelegt
				// StatBit_SpezialGeraetKennung muss vom Hauptprogramm gesetzt werden!
			FsEingMark = true;
			FsAusgMark = true;
			BusVerbPartner = 0;
			break;

		case Reserviert:
			CLR_BIT(Status, StatBit_Frei);
			CLR_BIT(Status, StatBit_SpezialGeraetKennung); // weil dieses Bit nur bei StatBit_Frei = 1 erlaubt ist
			break;

		case Wahl:
			CLR_BIT(Status, StatBit_Frei);
			CLR_BIT(Status, StatBit_SpezialGeraetKennung); // weil dieses Bit nur bei StatBit_Frei = 1 erlaubt ist
			WahlZifferAnzahl = 0;
			InterneNummer = 0;
			WahlPhase = WahlIntern;
			PufferInit(&SendePuffer); // da kommen jetzt die Wahlziffern rein
			break;

		case EinschaltungKo:
			CLR_BIT(Status, StatBit_Frei);
			CLR_BIT(Status, StatBit_SpezialGeraetKennung); // weil dieses Bit nur bei StatBit_Frei = 1 erlaubt ist
			SET_BIT(Status, StatBit_AngerufenBelegt);
			SET_BIT(Status, StatBit_FsBefBetrieb);
			BusEmpfMark = true; // schon mal vorsorglich.
			FsAusgMark = true; // schon mal vorsorglich.
			break;

		case Eingeschaltet:
			if (BIT_IS_SET(Status, StatBit_Frei))
				{
				CLR_BIT(Status, StatBit_Frei);
				CLR_BIT(Status, StatBit_SpezialGeraetKennung); // weil dieses Bit nur bei StatBit_Frei = 1 erlaubt ist
				SET_BIT(Status, StatBit_AngerufenBelegt);
				}
			SET_BIT(Status, StatBit_Verbunden);
			SET_BIT(Status, StatBit_FsMeldBetrieb);
			SET_BIT(Status, StatBit_FsBefEin);
			SET_BIT(Status, StatBit_FsMeldEin);
			GesendeterPegelStatus = Mark2;
			PufferInit(&SendePuffer);
			PufferInit(&EmpfPuffer);
			SeriellUmsetzInit();
			BusEmpfMark = true;
			FsEingMark = true;
			FsAusgMark = true;
			break;

		// case FremdKonfig:
			// SET_BIT(Status, StatBit_Verbunden);
			// break;

		case AusschaltungKo:
			CLR_BIT(Status, StatBit_Verbunden);
			CLR_BIT(Status, StatBit_FsBefBetrieb);
			break;

		case AusschaltungGe:
			CLR_BIT(Status, StatBit_Verbunden);
			CLR_BIT(Status, StatBit_FsMeldBetrieb);
			break;

		}

	FsBetriebsart = neu;

	SREG = SregAlt;

	}
	

#define TIMER0_CS TCCR_DIV(0, 8)
#define TIMER0_PRESCALER 8
#define TIMER0_FREQ (F_CPU / TIMER0_PRESCALER)

// f_Timer0OVF = (f_CPU / Prescaler) / (256 - Startwert)
// --> Startwert = 256 - (f_CPU / Prescaler) / f_Timer0OVF

#define TIMER0_OVFFREQ 10000
#define TIMER0_START (256 - TIMER0_FREQ / TIMER0_OVFFREQ)


//! Initialisierung der Schnittstelle.
void KommInit()
	{
#ifdef TCCR0A
	TCCR0A = 0;
	TCCR0B = TIMER0_CS; 
	SET_BIT(TIMSK0, TOIE0);
#else
	TCCR0 = TIMER0_CS;
	SET_BIT(TIMSK, TOIE0);
#endif //def TCCR0A

	// sonstige Initialisierungen
	Status = (1 << StatBit_Frei); // StatBit_SpezialGeraetKennung muss vom Hauptprogramm gesetzt werden
	SeriellUmsetzInit();
	BetriebsartWechsel(Ausgeschaltet);
	PufferInit(&SendePuffer);
	PufferInit(&EmpfPuffer);
	BusKommSperre = false;
	
	SendeUmsetzModus = UmsetzFern;
	EmpfUmsetzModus = UmsetzFern;
	
#ifdef TESTFUNKTIONEN
	TestFunktion = 0;
	TestVerzoegerung = 0;
#endif //def TESTFUNKTIONEN
	
	}
	

//! Besteht ein Einschaltwunsch, der von einer Gegenstelle ausgelöst wurde?	
bool KoEinschalten()
	{
#ifdef TESTFUNKTIONEN
	if (FsBetriebsart == Eingeschaltet)
		return true;
	else if (FsBetriebsart == EinschaltungKo)
		if (TestfunktionAktiv(TestfnEinschaltVerzoegerung))
			return TimerVal(&TestVerzTimer) >= TestVerzoegerung * 100;
				// solange Timer nicht abgelaufen ist vortäuschen,
				// dass kein Einschaltauftrag anliegt
		else if (TestfunktionAktiv(TestfnEinschaltAblehnung))
			if (TimerVal(&TestVerzTimer) >= TestVerzoegerung * 100)
				GeAusschalten(true); // TODO prüfen ob das reicht
			else
				return false; // vortäuschen dass kein Einschaltauftrag anliegt
		else
			return true;
	else
		return false;
#else		
	return (FsBetriebsart == EinschaltungKo || FsBetriebsart == Eingeschaltet);
#endif 
	}
	

#ifndef FUER_TW39

//! Unter welcher Unter-Adresse wurde dieses Endgerät gerade angewählt?
uint8_t KoAnwahlnummer()
	// im Regelfall 0. Kann bei Mehrfach-Endgerät 0 bis BusEigenAdrMehrfach-1 sein
	{
	return BusAnrufSubAdresse;
	}

#endif //def FUER_TW39


#ifdef TESTFUNKTIONEN

// prüfen ob überhupt gebraucht
/*
static void TestVerzoegerungAbwarten()
	{
	TMsTimer VerzTimer;
	StartTimer(&VerzTimer);
	while (TimerVal(&VerzTimer) < TestVerzoegerung * 100)
		;
	}
*/

#else

	/*
#define	TestVerzoegerungAbwarten() while (0)
	*/

	
#endif //def TESTFUNKTIONEN

	
//! Bestätigung der Einschaltung des eigenen Gerätes
//--------------------------------------------------
//! Funktion ist in zwei Situationen aufzurufen: 
//! 1. Als Bestätigung der eigenen Einschaltung bei einem kommenden Anruf 
//! (Aktuelle Betriebsart ist #EinschaltungKo). 
//! 2. Als Wunsch des Verbindungsaufbaus vom eigenen Gerät.
//! \retval GeEinschAnrufquitt Verbindung ist fertig aufgebaut (Situation 1)
//! \retval GeEinschWahl Es darf nun gewählt werden (Situation 2)
//! \retval GeEinschFehler Es ist ein Fehler aufgetreten.

TGeEinschResultat GeEinschalten()
	{
	if (FsBetriebsart == EinschaltungKo)
		{
		BusKommSperre = true;
		BusSenden(BusQuittEin);
		BusWarteFertig();
		BusSenden(BusLebenszeichen); // wartet, dass BusQuittEin auch angekommen ist...
		BetriebsartWechsel(Eingeschaltet);
		BusKommSperre = false;
		return GeEinschAnrufquitt;
		}

	if (BusEigenAdresse == BusAdrUngueltig)
		return GeEinschFehler;

	if (FsBetriebsart != Ausgeschaltet)
		FehlerStop(8);

	BusKommSperre = true;

	uint8_t SregAlt = SREG;
	cli();

	CLR_BIT(Status, StatBit_Frei);
	CLR_BIT(Status, StatBit_SpezialGeraetKennung); // weil dieses Bit nur bei StatBit_Frei = 1 erlaubt ist
	CLR_BIT(Status, StatBit_AngerufenBelegt);

	SREG = SregAlt;
	
	BetriebsartWechsel(Wahl);
	BusKommSperre = false;
	return GeEinschWahl;
	}


//! Prüft, ob die bisher gewählten Ziffern einen internen Partner ergeben.
//------------------------------------------------------------------------
//! Als Resultat erfolgt ggf. ein Zustandswechsel.
static void InternWahlPruefen()
	{
	BusWarteFertig();
	BusVerbPartner = WahlZuAdresse(InterneNummer, WahlZifferAnzahl);
	
	BusSenden(BusEigenAdresse >> 1);
		// prüft vorher, ob Zielelement frei ist
	BusWarteFertig();

	StartTimer(&PegelWdhTimer);

	if (BusErgebnis == Ok)
		{ // erfolgreich verbunden
		// (interner Teilnehmer vorhanden und nicht besetzt)
		WahlPhase = WahlPause;
		AblaufMark(0x2b);
		}
	else if (BusErgebnis == Besetzt)
		{ // Partner vorhanden, aber besetzt
		AblaufMark(0x2c);
		BusErgebnis = Ok; // um spätere Probleme zu vermeiden		
		BusVerbPartner = 0;
		BetriebsartWechsel(AusschaltungKo);
		AusschaltCode = 'b'; // Besetzt
		return; 
		}
	else
		{ // Partner nicht existent
		BusErgebnis = Ok; // um spätere Probleme zu vermeiden		
		BusVerbPartner = 0; // weitere Kommunikation mit der gewählten Adresse sinnlos
		if (WahlZifferAnzahl >= 2)
			{ // hat keinen Sinn weiter zu wählen
			AblaufMark(0x2d);
			BetriebsartWechsel(AusschaltungKo);
			AusschaltCode = 'x'; // nicht existent
			}
		return;
		}

	// Einschalten 
	// -----------
	AblaufMark(0x22);
	BusSenden(BusKdoEin);
	BusWarteFertig();
	//! \todo BusErgebnis prüfen...

	// Rest (Auswertung von BusKdoWahlFreigabe / BusQuittEin) geschieht in der Funtion BusKomm
	
	//! \todo Abbruch wenn keine Reaktion erfolgt...

	StartTimer(&PegelWdhTimer);
	} // if zulässige Nummer


//! Funktion ist aufzurufen, wenn im Wahlzustand eine Ziffer gewählt wurde.
//-------------------------------------------------------------------------
//! \param Ziffer die gewählte Ziffer (0 bis 9).
void GeWaehlen(uint8_t Ziffer)
	{
	if (FsBetriebsart == Wahl && WahlPhase != WahlGesperrt)
		{
		PufferSpeich(&SendePuffer, Ziffer); // Pufferüberlauf wird ignoriert
		}
	else
		FehlerStop(9);
	}
	

//! Liefert die zuletzt gewählte Interne Nummer.
//----------------------------------------------
//! \returns Die zuletzt gewahlte Interne Nummer (nicht * 2).
uint8_t LetzteInterneWahl()
	{
	return InterneNummer;
	}
	
	
//! Liefert den aktuellen "Empfangspegel"
//---------------------------------------
//! Von dem aktuellen Verbindungspartner an das eigene Gerät.
//! \retval true bei Mark.	
bool KoEmpfMark() // true bei Mark
	{
	return FsAusgMark;
	}
	
	
//! Setzt den aktuellen "Sendepegel"
//---------------------------------------
//! Vom eigenen Gerät an den aktuellen Verbindungspartner.
//! \param Mark true bei Mark.	
void GeSendeMark(bool Mark)
	{
	FsEingMark = Mark;
	}


#ifndef FUER_TW39
	
//! Liefert zuletzt empfangenes Baudot-Zeichen.
//---------------------------------------------
//! Parallel zum Setzen von KoEmpfMark() wird der serielle Takt empfangen
//! und interpretiert und gepuffert (FIFO).
//! \param[out] Code empfangener Baudot-Code (nur gültig, wenn Funktionsergebnis true.
//! \returns Empfangspuffer war nicht leer. 

bool KoEmpfCode(uint8_t *Code) 
	{
	if (PufferLeer(&EmpfPuffer))
		return false;
	else
		{
		*Code = PufferAusg(&EmpfPuffer);
		return true;
		}
	}

#endif //ndef FUER_TW39


//! Bewirkt das Senden eines Baudot-Codes an den Verbindungspartner.
//------------------------------------------------------------------
//! Wird zunächst gepuffert (FIFO) und dann serialisiert und gesendet.
//! \param Code zu sendender Baudot-Code.
//! \returns Sendepuffer konnte das Zeichen noch aufnehmen. 

bool GeSendeCode(uint8_t Code) // true, wenn Sendepuffer nicht voll
	{
	if (PufferSpeich(&SendePuffer, Code))
		{ // erfolgreich
		if (Code == TtyCodeBuUm)
			SendePuffer.BuZiMode = BuMode;
		else if (Code == TtyCodeZiUm)
			SendePuffer.BuZiMode = ZiMode;
		return true;
		}
	else
		return false;
	}


#ifndef FUER_TW39

//! Liefert zuletzt empfangenes ASCII-Zeichen.
//---------------------------------------------
//! Übersetzt gepufferte Baudot-Codes (siehe KoEmpfCode) in ASCII.
//! \param[out] Zeichen empfangenes ASCII-Zeichen (nur gültig, wenn Funktionsergebnis true).
//! \returns Empfangspuffer war nicht leer. 

bool KoEmpfZeichen(char *Zeichen) // ASCII-Code
	{
	uint8_t code;

	while (true)
		{
		if (!KoEmpfCode(&code))
			return false;

		// war es vielleicht ein Sonderzeichen?
		if (EmpfPuffer.BuZiMode == ZiMode)
			{
			if (code == TtyCodeZiKlingel)
				{
				*Zeichen = CodeChrKlingel;
				return true;
				}
			else if (code == TtyCodeZiWerDa)
				{
				*Zeichen = CodeChrWerDa;
				return true;
				}
			}

		*Zeichen = CodeZuZeichen(code, (char*) &EmpfPuffer.BuZiMode);
		if (*Zeichen != '\0')
			return true;

		}
	}


//! Bewirkt das Senden eines ASCII-Zeichens an den Verbindungspartner.
//--------------------------------------------------------------------
//! Wird zunächst in Baudot übersetzt und dann mit GeSendeCode() abgeschickt.
//! \param c zu sendendes ASCII-Zeichen.
//! \returns Sendepuffer konnte das Zeichen noch aufnehmen. 
//! \remark Wenn das Zeichen nicht übersetzbar ist, wird auch true geliefert.

bool GeSendeZeichen(char c)
	{
	switch (c)
		{
		case CodeChrBuUm:
			if (!PufferSpeich(&SendePuffer, TtyCodeBuUm))
				return false;
			SendePuffer.BuZiMode = BuMode;
			return true;

		case CodeChrZiUm:
			if (!PufferSpeich(&SendePuffer, TtyCodeZiUm))
				return false;
			SendePuffer.BuZiMode = ZiMode;
			return true;

		case CodeChrKlingel:
			if (SendePuffer.BuZiMode != ZiMode && !PufferSpeich(&SendePuffer, TtyCodeZiUm))
				return false;
			SendePuffer.BuZiMode = ZiMode;
			return PufferSpeich(&SendePuffer, TtyCodeZiKlingel);

		case CodeChrWerDa:
			if (SendePuffer.BuZiMode != ZiMode && !PufferSpeich(&SendePuffer, TtyCodeZiUm))
				return false;
			SendePuffer.BuZiMode = ZiMode;
			return PufferSpeich(&SendePuffer, TtyCodeZiWerDa);

		}

	if (c >= 'A' && c <= 'Z')
		c += 'a' - 'A';

	uint8_t code1, code2;
	
	if (ZeichenZuCode2(c, (char *) &SendePuffer.BuZiMode, &code1, &code2))
		// (char*) schmeißt absichtlich das volatile weg
		{
		if (!PufferSpeich(&SendePuffer, code1))
			return false;
		if (code2 != 255 && !PufferSpeich(&SendePuffer, code2))
			return false;
		}
	return true;
	}


//! Der Sendepuffer ist nicht voll.
//---------------------------------
bool GeSendePufferVoll()
	{
	return PufferVoll(&SendePuffer);
	}


//! Der Sendepuffer ist nicht leer.
//---------------------------------
bool GeSendePufferLeer()
	{
	return PufferLeer(&SendePuffer);
	}

#endif //def FUER_TW39


///////////////////////////////////////////////////////////////////////////


static void BusKomm(); // wird gleich benötigt...


///////////////////////////////////////////////////////////////////////////

//! Bewirkt den Verbindungsabbau.
//---------------------------------
//! Funktion ist in zwei Situationen aufzurufen:
//! 1. Als Bestätigung der Ausschaltung, wenn von der Gegenstelle diese
//! angefordert wurde (dann war KoAusschalten() true).
//! 2. Als Ausschaltwunsch des eigenen Geräts. 
//! Die Funktion kehrt sofort zurück, außer Parameter WarteQuitt ist wahr.
//! Im Fall 2. kann mit KoAusschalten() abgefragt werden, ob die 
//! Ausschaltung erfolgreich vollzogen wurde.
//! Bei testweiser Verzögerung der Quittung wird die Verzögerung in 
//! dieser Funktion abgewartet.

void GeAusschalten(bool WarteQuitt)
	{
	if (FsBetriebsart == Ausgeschaltet)
		return;
	
	if (BusVerbPartner == 0)
		{
		BetriebsartWechsel(Ausgeschaltet); 
		return; // nicht verbunden, also auch nicht trennen.
		}
	
	BusKommSperre = true;
	AblaufMark(0x49);
	if (FsBetriebsart == AusschaltungKo)
		{ // nur noch quittieren
#ifdef TESTFUNKTIONEN	
		if (TestfunktionAktiv(TestfnAusschaltQuittVerzoegerung))
			{
			StartTimer(&TestVerzTimer);
			while (TimerVal(&TestVerzTimer) < TestVerzoegerung * 100)
				BusKomm(); // sicherheitshalber. TODO ungeprüft, was passieren kann.
			}
#endif //def TESTFUNKTIONEN	
		if (!TestfunktionAktiv(TestfnAusschaltOhneQuitt))
			BusSenden(BusQuittSchluss);
		BusWarteFertig();
		BetriebsartWechsel(Ausgeschaltet); 
		}
	else
		{ // aktiv ausschalten
		if (TestfunktionAktiv(TestfnAusschaltQuittStattKdo))
			BusSenden(BusQuittSchluss);  // Dies ist nur beim Testen
		else
			BusSenden(BusKdoSchluss); // korrektes Verhalten
		BetriebsartWechsel(AusschaltungGe);
		StartTimer(&AusschaltQuittTimer);
		}
	BusKommSperre = false;
	
	while (FsBetriebsart == AusschaltungGe && WarteQuitt)
		BusKomm(); // Wartet auf Quittung oder Timeout.
	
	}


//! Abfrage, ob ein Verbindungsabbau gewünscht wird.
//--------------------------------------------------
//! \retval true wenn die Gegenstelle einen Verbindungsabbau angefordert hat
//! oder der Verbindungsabbau erfolgreich vollzogen ist.

bool KoAusschalten()
	{
	return (FsBetriebsart == Ausgeschaltet || FsBetriebsart == AusschaltungKo);
	}


//! Temporäres Trennen vom System.
//--------------------------------
//! Funktion ist aufzurufen, um das eigene Gerät vorübergehend zu deaktivieren.
//! Wenn es deaktiviert ist, kann es für ankommende Rufe nicht erreicht werden.
//! Bevor eine Abgehende Verbindung aufgebaut werden soll, ist vorher wieder 
//! eine Aktivierung vorzunehmen.
//! \param Aktiv true, wenn das Gerät erreichbar sein soll.

void Aktivieren(bool Aktiv)
	{
	if (FsBetriebsart != Ausgeschaltet && FsBetriebsart != Inaktiv)
		FehlerStop(10);
	if (Aktiv)
		BetriebsartWechsel(Ausgeschaltet);
	else
		BetriebsartWechsel(Inaktiv);
	}
		

//! Durchführung der Kommunikation auf dem TWI-Bus.
//-------------------------------------------------
//! Diese Funktion wird durch einen Timer regelmäßig aufgerufen und erledigt die
//! Kommunikation entsprechend der mit den Funktionen Ge... gewünschten Zustandswechsel
//! und setzt die internen Variablen so, dass die Funktionen Ko... die aktuellen
//! Zustände abbildet.
		
static void BusKomm()
// darf nur bei aktivem Interrupt-Enable aufgerufen werden!
	{
	//HACK LED_EIN(ROT);
	
	uint8_t Kdo;

	if (GetEmpfByte(&Kdo))
		{
		bool Bearbeitet = true;

		if (FsBetriebsart == Inaktiv)
			{
			FehlerStop(1);
			}
		
		switch (Kdo)
			{
			case 0 ... BusKdoVerbAufnahme:
				if (FsBetriebsart == Ausgeschaltet)
					{
					BusVerbPartner = Kdo << 1;
					BetriebsartWechsel(Reserviert);
					Bearbeitet = true;
					}
				break;

			case BusKdoEin:
				if (FsBetriebsart == Reserviert)
					{ 
					AblaufMark(0xFF); 
					#ifdef TESTFUNKTIONEN
					if (TestfunktionAktiv(TestfnEinschaltVerzoegerung) || TestfunktionAktiv(TestfnEinschaltAblehnung))
						StartTimer(&TestVerzTimer);
					#endif //def TESTFUNKTIONEN
					BetriebsartWechsel(EinschaltungKo);
					Bearbeitet = true;
					}					
				break;

			case BusQuittEin: 
				if (FsBetriebsart == Wahl)
					{ // Gegenstelle läuft
					AblaufMark(0x2e);
					BetriebsartWechsel(Eingeschaltet);
					Bearbeitet = true;
					}					
				break; // case BusQuittEin

			// case BusQuittKonfig: TODO: im neuen Konzept könnte es vorkommen...

			case BusKdoWahlFreigabe:
				if (FsBetriebsart == Wahl)
					{ // Gegenstelle läuft
					AblaufMark(0xFF); // TODO ???
					WahlPhase = WahlExtern;
					Bearbeitet = true;
					}					
				break; // case BusKdoWahlFreigabe

			// case BusKdoWahlziffer0..9: darf nur an Leitungsschnittstellen gesendet werden

/*			case BusLebenszeichen: // sollte eigentlich schon beim Empfang ignoriert werden
				StatusKdoEmpfReset();
				break;
*/

			case BusKdoSchluss:
				if (FsBetriebsart != Ausgeschaltet)
					BetriebsartWechsel(AusschaltungKo);
				Bearbeitet = true;
				break;

			case BusQuittSchluss:
				if (FsBetriebsart == AusschaltungGe)
					BetriebsartWechsel(Ausgeschaltet);
				// sonst ignorieren, da es eine verspätete Meldung einer vorherigen 
				// Ausschaltung sein kann.
				Bearbeitet = true;
				break;

			// case 0xF0 ... 0xFF :
				// if (FsBetriebsart == FremdKonfig)
					// {
					// PufferSpeich(&EmpfPuffer, Kdo);
					// Bearbeitet = true;
					// }
				// break;
				
			} // case BusEmpfDaten

		if (!Bearbeitet)
			{ 
#ifdef FALSCHKDO_FEHLERSTOP
			FehlerStop(3);
#endif
			}
		} // if GetEmpfByte(&Kdo)

	// selbsttätige Aktionen... (ggf. eingeleitet vom Endgerät oder durch Telegramme)
	// -----------------------------------------------------------------------------
	switch (FsBetriebsart)
		{
		case Ausgeschaltet:
			break;

		case Inaktiv:
			break;

		case Reserviert:
			break;

		case Wahl:
			if (PufferLeer(&SendePuffer))
				break; // nichts zu tun...
				
			if (WahlPhase == WahlPause)
				break; // darf nichts tun...
				
			uint8_t Ziffer = PufferAusg(&SendePuffer);
			WahlZifferAnzahl++;
			if (WahlPhase == WahlIntern)
				{
				if (WahlZifferAnzahl == 1)
					{
					InterneNummer = Ziffer;
					InternWahlPruefen();
					}
				else if (WahlZifferAnzahl == 2)
					{
					InterneNummer = 10 * InterneNummer + Ziffer;
					InternWahlPruefen();
					}
				}
			if (WahlPhase == WahlExtern)
				{ 
				BusSenden(BusKdoWahlziffer0 + Ziffer);
				StartTimer(&PegelWdhTimer);
				}
			break;

		case EinschaltungKo:
			break;

		case Eingeschaltet:
			// Kommenden Pegel verarbeiten
			if (BusEmpfMarkwechsel)
				{
				if (BusEmpfMark)
					SET_BIT_Status(StatBit_FsBefEin);
				else
					CLR_BIT_Status(StatBit_FsBefEin);
				}

			// Gehenden Pegel vorbereiten
			if (!PufferLeer(&SendePuffer) && SerUmSendBitNr == SerUmSendWarte)
				{
				SerUmSendDaten = PufferAusg(&SendePuffer);
				SerUmSendBitNr = SerUmSendStart;
				}

			bool SerUmSendMark, BusSendMark;
			
			if (EmpfUmsetzModus == UmsetzLokal)
				SeriellUmsetzung(FsEingMark, &SerUmSendMark);
			else if (EmpfUmsetzModus == UmsetzFern)
				SeriellUmsetzung(BusEmpfMark, &SerUmSendMark);
			else if (EmpfUmsetzModus == UmsetzLokalUndFern)
				SeriellUmsetzung(BusEmpfMark && FsEingMark, &SerUmSendMark);
			else
				SeriellUmsetzung(true, &SerUmSendMark);

			if (SerUmEmpfBitNr == SerUmEmpfFertig)
				{
				PufferSpeich(&EmpfPuffer, SerUmEmpfDaten);
				SerUmEmpfBitNr = SerUmEmpfWarte;
				}
			
			FsAusgMark = BusEmpfMark; // wird vielleicht gleich wieder überschrieben.
			BusSendMark = FsEingMark;
			
			if (SerUmSendBitNr != SerUmSendWarte)
				{
				if (SendeUmsetzModus == UmsetzLokal)
					FsAusgMark = SerUmSendMark;
				else if (SendeUmsetzModus == UmsetzFern)
					BusSendMark = SerUmSendMark;
				else if (SendeUmsetzModus == UmsetzLokalUndFern)
					{
					FsAusgMark = SerUmSendMark;
					BusSendMark = SerUmSendMark;
					}
				else
					; 
					// die oben entsprechend BusEmpfMark oder FsEingMark
					// gesetzten Zustände bleiben erhalten
				}

			if (BusSendMark)
				SET_BIT_Status(StatBit_FsMeldEin);
			else
				CLR_BIT_Status(StatBit_FsMeldEin);

			if (BusFrei && BusAuftrag == Nichts)
				{ // überhaupt fähig zu senden
				if (BusSendMark)
					{ // Mark
					if (GesendeterPegelStatus == Space1 || GesendeterPegelStatus == Space2)
						{
						if (!TestfunktionAktiv(TestfnMarkNurWdh))
							BusSenden(BusKdoMark);
						else 
							BusSenden(BusKdoMarkWdh);
						}
					else if (GesendeterPegelStatus == Mark1)
						{
						if (!TestfunktionAktiv(TestfnMarkNichtWdh))
							BusSenden(BusKdoMarkWdh);
						else
							GesendeterPegelStatus = Mark2; // erfolgreiches Senden von BusKdoMarkWdh simulieren
						}
#ifdef WIEDERHOLUNGSSENDUNGEN
					else if (TimerVal(&PegelWdhTimer) > 400) // mind. alle 0,4 Sek senden
						BusSenden(BusKdoMarkWdh);
#endif //WIEDERHOLUNGSSENDUNGEN
					} // Mark
				else
					{ // Space
					if (GesendeterPegelStatus == Mark1 || GesendeterPegelStatus == Mark2)
						{
						if (!TestfunktionAktiv(TestfnSpaceNurWdh))
							BusSenden(BusKdoSpace);
						else
							BusSenden(BusKdoSpaceWdh);
						}
					else if (GesendeterPegelStatus == Space1)
						{
						if (!TestfunktionAktiv(TestfnSpaceNichtWdh))
							BusSenden(BusKdoSpaceWdh);
						else
							GesendeterPegelStatus = Space2; // erfolgreiches Senden von BusKdoSpaceWdh simulieren
						}
#ifdef WIEDERHOLUNGSSENDUNGEN
					else if (TimerVal(&PegelWdhTimer) > 400) // mind. alle 0,4 Sek senden
						BusSenden(BusKdoSpaceWdh);
#endif //WIEDERHOLUNGSSENDUNGEN
					} // Space

				} // Bus ist sendefähig	

			break;

		// case FremdKonfig:
			// break;
			
		case AusschaltungKo:
			break;
			
		case AusschaltungGe:
			// Empfang von BusQuittSchluss wird oben bearbeitet.
			if (TimerVal(&AusschaltQuittTimer) > 4000) // nach 4 Sekunden wird auch ohne Quittung ausgeschaltet.
				BetriebsartWechsel(Ausgeschaltet);
			break;
			
		} // switch Betriebsart

	if (BusAuftrag == Fertig)
		{ // letzte Sendung wurde abgeschlossen
		StartTimer(&PegelWdhTimer);
		if (BusErgebnis == Ok)
			{
#ifdef BUSFEHLER_ABBRUCH
			AnzFehlSend = 0;
#endif //BUSFEHLER_ABBRUCH
			switch (BusSendeDaten)
				{
				case BusKdoSpace:
					GesendeterPegelStatus = Space1;
					break;
				case BusKdoSpaceWdh:
					GesendeterPegelStatus = Space2;
					break;
				case BusKdoMark:
					GesendeterPegelStatus = Mark1;
					break;
				case BusKdoMarkWdh:
					GesendeterPegelStatus = Mark2;
					break;
				} // switch BusSendeDaten
			} // letzte Bus-Sendung war fehlerfrei
		else
			{
			AblaufMark(0x4A);
#ifdef BUSFEHLER_ABBRUCH
			if (AnzFehlSend >= 5)
				{ // 5 Fehl-Sendungen unmittelbar hintereinander
				BusSenden(BusKdoSchluss);
				Ende = true;
				BetriebsartWechsel(AusschaltungF);
				AusschaltCode = 'f';
				} 
			else
				AnzFehlSend++;
#endif //BUSFEHLER_ABBRUCH
			}
		BusAuftrag = Nichts;
		}

	if (BusVerbPartner > 0 
		&& BusFrei 
		&& (BusAuftrag == Nichts || BusAuftrag == Fertig) 
		&& TimerVal(&PegelWdhTimer) > (TestfunktionAktiv(TestfnLebenszeichenAbstand) ? (TestVerzoegerung * 100 + 10) : 785)) // mind. alle 0,785 Sek senden
		{
		BusSenden(BusLebenszeichen);
		StartTimer(&PegelWdhTimer);
		}
	
	if (BusVerbPartner == 0 || FsBetriebsart == AusschaltungGe || FsBetriebsart == AusschaltungKo)
		wdt_reset();

	//HACK LED_AUS(ROT);
	}
	

//! Interrupt-Routine des Timer0.
//-------------------------------
//! Einziger Zweck ist der Aufruf von BusKomm.
//! Falls BusKomm durch einen anderen Aufruf gerade läuft ist BusKommSperre gesetzt
//! und verhindert das "rekursive" Aufrufen.

ISR(TIMER0_OVF_vect)
	{
	if (BusKommSperre) 
		return;
	BusKommSperre = true;

#ifdef TCCR0A
	CLR_BIT(TIMSK0, TOIE0);
#else
	CLR_BIT(TIMSK, TOIE0);
#endif

	sei(); // Interrupts wieder erlauben
	BusKomm(); // Diese Funktion braucht die Interrupts
	
	// neuen Zyklus beginnen
	
	TCNT0 = TIMER0_START;

#ifdef TCCR0A
	TIFR0 = (1<<TOV0); // Interrupt-Flag löschen
	SET_BIT(TIMSK0, TOIE0);
#else
	TIFR = (1<<TOV0);
	SET_BIT(TIMSK, TOIE0);
#endif

	BusKommSperre = false;
	}


