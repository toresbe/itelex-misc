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

typedef enum { Ausgeschaltet, Reserviert, Wahl, EinschaltungKo, 
               Eingeschaltet, AusschaltungKo, AusschaltungGe, FremdKonfig,
			   Inaktiv } TFsBetriebsart;
// ...Ko = durch fremdes Ereignis (Einschaltung durch Anruf, Ausschaltung durch Schluß von Gegenstelle)
// ...Ge = durch eigenes Ereignis (Ruftaste, Schlußtaste des direkt angeschlossenen Geräts)

static char AusschaltCode; // Grund für Abschaltung

static volatile TFsBetriebsart FsBetriebsart;
	
static volatile bool PegelGehend; // wird von Schnittstellenprogramm gesetzt (vom Fernschreiber)


// Debug-Speicher
// --------------

#ifdef TWI_DEBUG

#define DebugBufLen 300

static uint8_t DebugBuf[DebugBufLen];

static volatile uint8_t* DebugBufP;

#endif //TWI_DEBUG

#ifdef SPEICHER_TEST

// Puffer für Echo
// ---------------

#define MaxSpeicher 500

static uint8_t Speicher[MaxSpeicher];
static uint16_t SpeicherPos;

#endif 


// Debug-Hilfen
// ------------

#ifdef TWI_DEBUG

void DebSp(uint8_t x)
	{
	if (DebugBufP < DebugBuf + DebugBufLen - 1)
		{
		*DebugBufP = x;
		DebugBufP++;
		}
	}
		
#endif //TWI_DEBUG


void FehlerStop(int Nummer);


#ifdef TWI_DEBUG

static void AblaufMark(uint8_t Code)
	{
	cli();
	DebSp(0xFF);
	DebSp(Code);
	sei();
	}
	
#else //TWI_DEBUG

#define AblaufMark(Code) while(0)

#endif //TWI_DEBUG


static volatile bool BusKommSperre = false;
	// sperrt das Interrupt-Basierte Bearbeiten ggf. eintreffender Bus-Kommandos
	
static volatile uint8_t LetzteWahlZiffer;

static volatile uint8_t WahlZifferAnzahl;

static volatile uint8_t InterneNummer;

static volatile enum { Mark1, Mark2, Space1, Space2 } GesendeterPegelStatus = Mark2;

static volatile enum { WahlGesperrt, WahlIntern, WahlPause, WahlExtern } WahlPhase;
	// WahlGesperrt = nicht wählen
	// WahlIntern = interner Verbindungspartner noch nicht definiert
	// WahlPause = interner Verbindungspartner definiert, Wahl nach Extern noch nicht möglich
	// WahlExtern = Wahl nach Extern möglich

static TMsTimer PegelWdhTimer;

TPuffer SendePuffer, EmpfPuffer;


bool BetriebsartAusgeschaltet()
	{
	return FsBetriebsart == Ausgeschaltet;
	}


void BetriebsartWechsel(TFsBetriebsart neu)
	{
	if (neu == FsBetriebsart)
		return;

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
			PegelGehend = true;
			BusEmpfMark = true;
			BusVerbPartner = 0;
			break;

		case Reserviert:
			CLR_BIT(Status, StatBit_Frei);
			CLR_BIT(Status, StatBit_SpezialGeraetKennung); // weil dieses Bit nur bei StatBit_Frei = 1 erlaubt ist
			break;

		case Wahl:
			CLR_BIT(Status, StatBit_Frei);
			CLR_BIT(Status, StatBit_SpezialGeraetKennung); // weil dieses Bit nur bei StatBit_Frei = 1 erlaubt ist
			LetzteWahlZiffer = 0;
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
			PegelGehend = true;
			BusEmpfMark = true;
			break;

		case FremdKonfig:
			SET_BIT(Status, StatBit_Verbunden);
			break;

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

	sei();

	}
	

#define TIMER0_CS TCCR_DIV(0, 8)
#define TIMER0_PRESCALER 8
#define TIMER0_FREQ (F_CPU / TIMER0_PRESCALER)

// f_Timer0OVF = (f_CPU / Prescaler) / (256 - Startwert)
// --> Startwert = 256 - (f_CPU / Prescaler) / f_Timer0OVF

#define TIMER0_OVFFREQ 10000
#define TIMER0_START (256 - TIMER0_FREQ / TIMER0_OVFFREQ)


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
	}
	

bool KoEinschalten()
	{
	return (FsBetriebsart == EinschaltungKo || FsBetriebsart == Eingeschaltet);
	}
	

#ifndef FUER_TW39

uint8_t KoAnwahlnummer()
	// im Regelfall 0. Kann bei Mehrfach-Endgerät 0 bis BusEigenAdrMehrfach-1 sein
	{
	return BusAnrufSubAdresse;
	}

#endif //def FUER_TW39

	
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

	cli();
	CLR_BIT(Status, StatBit_Frei);
	CLR_BIT(Status, StatBit_SpezialGeraetKennung); // weil dieses Bit nur bei StatBit_Frei = 1 erlaubt ist
	CLR_BIT(Status, StatBit_AngerufenBelegt);
	sei();
	
	BetriebsartWechsel(Wahl);
	BusKommSperre = false;
	return GeEinschWahl;
	}


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
		if (WahlZifferAnzahl >= 2)
			{ // hat keinen Sinn weiter zu wählen
			AblaufMark(0x2d);
			BusVerbPartner = 0;
			BetriebsartWechsel(AusschaltungKo);
			AusschaltCode = 'x'; // nicht existent
			}
		else
			BusVerbPartner = 0; // sonst schläge der Watchdog zu
		return;
		}

	// Einschalten 
	// -----------
	AblaufMark(0x22);
	BusSenden(BusKdoEin);
	BusWarteFertig();
	// TODO BusErgebnis prüfen...

	// Rest (Auswertung von BusKdoWahlFreigabe / BusQuittEin) geschieht in der Funtion BusKomm
	
	// TODO Abbruch wenn keine Reaktion erfolgt...

	StartTimer(&PegelWdhTimer);
	} // if zulässige Nummer


void GeWaehlen(uint8_t Ziffer)
	{
	if (FsBetriebsart == Wahl && WahlPhase != WahlGesperrt)
		{
		PufferSpeich(&SendePuffer, Ziffer); // Pufferüberlauf wird ignoriert
		LetzteWahlZiffer = Ziffer;
		}
	else
		FehlerStop(9);
	}
	

uint8_t LetzteInterneWahl()
	{
	return InterneNummer;
	}
	
	
bool KoEmpfMark() // true bei Mark
	{
	return BusEmpfMark;
	}
	
	
void GeSendeMark(bool Mark)
	{
	PegelGehend = Mark;
	}


#ifndef FUER_TW39
	
bool KoEmpfCode(uint8_t *Code) // wenn Zeichen empfangen wurde, wird dieses in Code gespeichert und true zurückgegeben
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


bool GeSendeCode(uint8_t Code) // true, wenn Sendepuffer nicht voll
	{
	if (Code == TtyCodeBuUm)
		SendePuffer.BuZiMode = BuMode;
	else if (Code == TtyCodeZiUm)
		SendePuffer.BuZiMode = ZiMode;

	return PufferSpeich(&SendePuffer, Code);
	}


#ifndef FUER_TW39

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


bool GeSendePufferVoll()
	{
	return PufferVoll(&SendePuffer);
	}


bool GeSendePufferLeer()
	{
	return PufferLeer(&SendePuffer);
	}

#endif //def FUER_TW39



void GeAusschalten()
	{
	if (FsBetriebsart == Ausgeschaltet)
		return;
	BusKommSperre = true;
	AblaufMark(0x49);
	if (FsBetriebsart == AusschaltungKo)
		{ // nur noch quittieren
		BusSenden(BusQuittSchluss);
		BusWarteFertig();
		}
	else
		{ // aktiv ausschalten
		BusSenden(BusKdoSchluss);
		WarteSchlussQuittung(3000);
		}
	BetriebsartWechsel(Ausgeschaltet);
	BusKommSperre = false;
	}


bool KoAusschalten()
	{
	return (FsBetriebsart == Ausgeschaltet || FsBetriebsart == AusschaltungKo);
	}


void Aktivieren(bool Aktiv)
	{
	if (FsBetriebsart != Ausgeschaltet && FsBetriebsart != Inaktiv)
		FehlerStop(10);
	if (Aktiv)
		BetriebsartWechsel(Ausgeschaltet);
	else
		BetriebsartWechsel(Inaktiv);
	}
		

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
			case BusQuittSchluss:
				if (FsBetriebsart == AusschaltungGe)
					BetriebsartWechsel(Ausgeschaltet);
				else if (FsBetriebsart != Ausgeschaltet)
					BetriebsartWechsel(AusschaltungKo);
				Bearbeitet = true;
				break;

			case 0xF0 ... 0xFF :
				if (FsBetriebsart == FremdKonfig)
					{
					PufferSpeich(&EmpfPuffer, Kdo);
					Bearbeitet = true;
					}
				break;
				
			} // case BusEmpfDaten

		if (!Bearbeitet)
			{ 
#ifdef FALSCHKDO_FEHLERSTOP
			FehlerStop(3);
#endif
			}
		} // if GetEmpfByte(&Kdo)

	// selbsttätige Aktionen... (ggf. eingeleitet vom Endgerät oder durch Telegramme)
	// ------------------------
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

			SeriellUmsetzung(BusEmpfMark, (bool*) &PegelGehend);

			if (PegelGehend)
				SET_BIT_Status(StatBit_FsMeldEin);
			else
				CLR_BIT_Status(StatBit_FsMeldEin);

			if (SerUmEmpfBitNr == SerUmEmpfFertig)
				{
				PufferSpeich(&EmpfPuffer, SerUmEmpfDaten);
				SerUmEmpfBitNr = SerUmEmpfWarte;
				}

			if (BusFrei && BusAuftrag == Nichts)
				{ // überhaupt fähig zu senden
				if (PegelGehend)
					{ // Mark
					if (GesendeterPegelStatus == Space1 || GesendeterPegelStatus == Space2)
						BusSenden(BusKdoMark);
					else if (GesendeterPegelStatus == Mark1)
						BusSenden(BusKdoMarkWdh);
#ifdef WIEDERHOLUNGSSENDUNGEN
					else if (TimerVal(&PegelWdhTimer)) // mind. alle 0,4 Sek senden
						BusSenden(BusKdoMarkWdh);
#endif //WIEDERHOLUNGSSENDUNGEN
					} // Mark
				else
					{ // Space
					if (GesendeterPegelStatus == Mark1 || GesendeterPegelStatus == Mark2)
						BusSenden(BusKdoSpace);
					else if (GesendeterPegelStatus == Space1)
						BusSenden(BusKdoSpaceWdh);
#ifdef WIEDERHOLUNGSSENDUNGEN
					else if (TimerVal(&PegelWdhTimer) > 400) // mind. alle 0,4 Sek senden
						BusSenden(BusKdoSpaceWdh);
#endif //WIEDERHOLUNGSSENDUNGEN
					} // Space

				} // Bus ist sendefähig	

			break;

		case FremdKonfig:
			break;
		case AusschaltungKo:
			break;
		case AusschaltungGe:
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

	// TODO Prüefen, was das soll... Eigentlich braucht es keine Lebenszeichen, wenn die Verbindung steht...
	if (BusVerbPartner > 0 && FsBetriebsart != FremdKonfig)
		SendeLebenszeichen();
	else
		wdt_reset();

	//HACK LED_AUS(ROT);
	}
	

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


