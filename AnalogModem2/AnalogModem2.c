// **************************************************************************
// V.21-Leitungsschnittstelle für das TxP2-System
// **************************************************************************
// für Platine LeitungAnalog2
// **************************************************************************

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/wdt.h>
#include <inttypes.h>

#include "bits.h"
#include "Ports.h"
#include "EepromTools.h"

#include "TwiEvents.h"

#include "TxP2-Defs.h"
#include "Taste.h"
#include "MsTimer.h"
#include "BaudotCode.h"
#include "BusKomm.h"
#include "SeriellUmsetz.h"
#include "FernDialog.h"

#include "73K221.h"

#include "../SvnVersion.h"


// Einstellung der Platinen-Version
// ================================

// Schalter für Code-Erzeugung
// ===========================

#define FALSCHKDO_FEHLERSTOP // Fehlerstop, wenn falsche Kommandos empfangen werden 

// #define NOWATCHDOG // Watchdog abgeschaltet


const char Identifier[] PROGMEM = "___TxP2_LeitungAnalog2___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";


// Konfigurationsdaten
// ===================

uint8_t KonfigBits; 

#define KonfigBit_FesterHauptanschluss 0 
	// wenn Bit = 1 wird immer der Hauptanschluss bei kommenden Anrufen benutzt. Sonst wird die  
	// Nebenstelle des letzten abgehenden Anrufen benutzt
#define KonfigBit_AnrufMithoeren 1
	// wenn Bit = 1 wird jeder durch Telefon angenommene Anruf mitgehört und bei Trägerton
	// in die Verbindung eingetreten
#define KonfigBit_AnrufNachhoeren 2
	// wenn Bit = 1 nach jedem durch Telefon angenommene Anruf nachträglich ein
	// Verbindungsversuch gemacht, wenn das Telefon weniger als 20 Sekunden an war.
#define KonfigBit_DurchwahlErlaubt 3		 
	// wenn Bit = 0 wird eine eingehende Durchwahl ignoriert und auf jeden Fall der Hauptanschluss
	// ausgewertet
#define KonfigBit_SucheAlternativBeiBesetzt 4
	// wenn Bit = 1 wird eine eingehender Anruf auf einen anderen Apparat gelenkt, wenn Hauptanschluss
	// besetzt ist
#define KonfigBit_Schnellstart 5
	// wenn Bit = 1 wird nach "fehlgeschlagenen Anrufen" die Anzahl der Klingelzeichen bis zum
	// Abheben auf 1 gesetzt. "fehlgeschlagener Anruf": Noch definieren
	// TODO: ingesamt noch nicht implementiert
								
uint8_t AnnahmeKlingelzeichen; 

enum { AnzJustierWahlziffern = 10 } ;

uint8_t JustierWahlziffern[AnzJustierWahlziffern]; // Ziffer, die am Anfang einer Leitungsjustierung gewählt werden
	
uint8_t JustierWahlVerzoegerung; // Verzögerung in 1/10 sek. zwischen Schleifenschluss und Wahl der Ziffern

uint8_t JustierNeustartPause; // Verzögerung in 1/10 sek. zwischen Schleifenunterbrechung und Schleifenschluss 
							// (also wie lange wird der Hörer aufgelegt)
	
uint8_t VerbindungsaufbauVerzoegerung;	// Verzögerung in 1/10 sek. zwischen letzter Ziffer und Sendung des Kenntons (Originate Mark)

uint8_t NebenstellenTabelle[10]; // Nummern der Maschine, die bei Anrufen die Nachricht annehmen soll 
				// (im Bereich 10 bis 99)
				// [0]: 	Hauptstelle, die bei Anwahl ohne Nebenstellennummer oder bei besetzter direkt 
				//		angewählter Endstelle benutzt wird. wird bei Programmstart nach AktuellEmpfaenger 
				//		kopiert mit * 2.
				// [1] bis [9]: Nebenstellen-Durchwahlen für direkte Durchwahlen				

#define Hauptanschluss NebenstellenTabelle[0]
				// siehe oben 
				
uint8_t AktuellEmpfaenger;
	// aktuell bei kommenden Rufen zu verwendedendes Endgerät. 
	// Hier wird bei GEHENDEN Anrufen auch die Endgeräte-Nummer gespeichert.
	// Bei Flag KonfigBit_FesterHauptanschluss wird aber möglichst immer der Eintrag "Hauptanschluss" verwendet
	// * 2 für I²C-Adressierung ist bei AktuellEmpfaenger bereits enthalten


// ****************************************************************
// Eeprom
// ****************************************************************

// Erläuterung der Variablen-Inhalte siehe oben...

uint8_t BusEigenAdresse_EE EEMEM = 110 << 1; 

uint8_t KonfigBits_EE EEMEM = (1<<KonfigBit_FesterHauptanschluss) | (1<<KonfigBit_SucheAlternativBeiBesetzt);

uint8_t AnnahmeKlingelzeichen_EE EEMEM = 1;

uint8_t JustierWahlziffern_EE[AnzJustierWahlziffern] EEMEM = { 5, 10 }; // 10 beendet die Liste

uint8_t JustierWahlVerzoegerung_EE EEMEM = 8;

uint8_t JustierNeustartPause_EE EEMEM = 12;

uint8_t VerbindungsaufbauVerzoegerung_EE EEMEM = 60; 

uint8_t NebenstellenTabelle_EE[10] EEMEM = { 31 } ; // alles mit 0 initialisiert


enum { DiagnoseSpeicherLen = 50 } ;

uint8_t DiagnoseSpeicher[DiagnoseSpeicherLen] EEMEM = { 0x11, 0x22, 0x33, 0x44, 0x55 } ;

// Inhalte:
// 0x01 + Code: PruefeBusSchluss hat ungültigen Befehlscode erkannt.
// 0x02 + Code: HandshakeGehend hat nicht funktioniert
// 0x03 + Code: VerbindungGehend hat unplanmäßig abgebrochen

static uint8_t DiagnoseSpeicherPos = 0;
	
void DiagDatenSpeichern(uint8_t d)
	{
	if (DiagnoseSpeicherPos < DiagnoseSpeicherLen)
		{
		eeprom_write_byte(&DiagnoseSpeicher[DiagnoseSpeicherPos], d);
		DiagnoseSpeicherPos++;
		}
	}

	
void DiagDatenLoeschen()
	{
	for (uint8_t i = 0 ; i < DiagnoseSpeicherLen ; i++)
		eeprom_write_byte(&DiagnoseSpeicher[i], 0x55);
	DiagnoseSpeicherPos = 0;
	}
	

static void Seriell300Baud()
	{
	// Serielles Handshake mit 300 Baud aktivieren
	#define BAUD 300
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
	#undef BAUD
	}
	
	
static void SeriellAus()
	{
	UCSR0A = 0;
	UCSR0B = 0;
	UCSR0C = 0;
	}
	
	

// FehlerStop
// ==========
// Programmstop nach Fehler. In Fehlercode-Nummer zur Anzeige mit den LED
// Reset mit langem Tastendruck
// verwendete Fehlercodes:

//	1: Bus-Empfang trotz Sperre oder TWI-Fehler
//	2: Falscher TWI-Event (z.B. General Call)
//	3: ungültiges Bus-Kommando bei kommender Verbindung
//	4: ungültiges Bus-Kommando bei Konfiguration oder gehender Verbindung
//	5: ungültiges Bus-Kommando bei gehender Verbindung (Wahlzustand)
//	-6: noch nicht programmierter Code für Annahme eines Anrufs während der
//		Anrufphase (durch Einschaltung eines Endgeräts) aufgerufen
//	7: ungültiges Bus-Kommando nach Einschalt-Kommando an Endgerät bei 
//		kommender Verbindung (erwartet wird BusQuittEin)
//	8: keine freie Bus-Adresse gefunden
//	9: frei
//	10: kein Endgerät für kommenden Anruf gefunden
//  11: frei
//	-12: Mehrfach-Busadresse bei inkompatiblen Chips
//	13: ungültiges Bus-Kommando bei aufgebauter Verbindung
// 	14: Kann keine freie Adresse mehr finden...


void FehlerStop(int Nummer)
	{
	TWCR = (1<<TWINT) | (0<<TWEA) | (0<<TWSTA) | (1<<TWSTO) | (1<<TWEN) | (0<<TWIE);
	
	uint8_t TasteZ = 0;
	bool TasteWirk = false;
	TMsTimer TasteTimer;
	StartTimer(&TasteTimer);
	clr_GABEL();
	clr_TELAUS();
	ModemReset();
	while (true)
		{
		wdt_reset();

		if (TimerVal(&TasteTimer) > 400)
			{
			StartTimer(&TasteTimer);
			if (get_TASTE())
				{ // Taste nicht gedrückt (schaltet gegen Masse)
				if (TasteZ > 0)
					{
					TasteZ--;
					if (TasteZ == 0 && TasteWirk)
						{
						cli();
						wdt_enable(WDTO_1S);
						while (true)
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


void Grundstellen(bool SchlussQuittSenden)
	{
	void SeriellAus();

	uint8_t Code;
	while (GetEmpfByte(&Code))
		; // einfach den Puffer leeren

	clr_LEDGELB();
	clr_LEDGRUEN();
	clr_LEDBLAU();
	ModemReset();
	SeriellAus();
	if (SchlussQuittSenden && BusVerbPartner != 0)
		{
		BusSenden(BusQuittSchluss);
		}
	if (get_GABEL() || get_TELAUS())
		{
		TMsTimer Timer;
		set_LEDROT();
		clr_GABEL();
		clr_TELAUS();
		StartTimer(&Timer);
		while (TimerVal(&Timer) < 500)
			wdt_reset(); // Abwarten, dass Störimpule durch Schleifentrennung abgeklungen sind
		}
	Tastendruck = NichtGedr;
	BusWarteFertig();
	BusVerbPartner = 0;
	Status = (1 << StatBit_Frei) | (1 << StatBit_LeitungKennung);
	wdt_reset();
	clr_LEDROT();
	}
	
	
// Handshake300
// ============
// Unterprogramm für den Austausch von 300-Baud-Zeichen t-x-p-O-K
// vor Aufruf: Variablen Hs300SendeZeichen1, Hs300SendeZeichen2 und Hs300WarteZeichen belegen
// Die beiden zu sendenden Zeichen werden abwechselnd alle 0,3Sekunden versucht


static char Hs300ExtraZeichen;
static uint8_t Hs300AnzahlExtra;

static bool Handshake300(char Zeichen1, char Zeichen2, char WarteZeichen, uint8_t Wiederholungen)
	{
	Hs300ExtraZeichen = '\0';
	Hs300AnzahlExtra = 0;
	
	while (!BIT_IS_SET(UCSR0A, UDRE0))
		; // blockiert zwar, kann aber nicht lange dauern...

	if (Zeichen1 == '\0')
		Zeichen1 = Zeichen2;
		
	for (uint8_t Wiederholung = 0 ; Wiederholung < Wiederholungen ; Wiederholung++)
		{
		if (BIT_IS_SET(Status, StatBit_AngerufenBelegt))
			// kommend
			wdt_reset(); 

		SendeLebenszeichen();
	
		TMsTimer Timer;
		StartTimer(&Timer);
		while (TimerVal(&Timer) < 300)
			{
			if (BIT_IS_SET(UCSR0A, RXC0)) // Zeichen empfangen
				{
				byte Stat = UCSR0A;
				char Empf = UDR0;
				if (!BIT_IS_SET(Stat, FE0))
					{
					if (Empf == WarteZeichen)
						return true;
					else
						{
						Hs300ExtraZeichen = Empf;
						Hs300AnzahlExtra++;
						}
					}		
				} // if Zeichen empfangen

			} // while 0,3 Sek

		if (Zeichen1 != '\0')
			UDR0 = (Wiederholung & 1) ? Zeichen2 : Zeichen1;
		// am Ende, damit mindestens einmal 300 ms gewartet wurde
		
		} // for Wiederholung
	return false;
	}
	

// VerbAusg
// ========
// Kommando über I²C-Bus empfangen --> auswerten und entscheiden


// VerbindungKommend: Aufruf über Leitung empfangen
// =================================================
// behandelt auch klingelndes Telefon und Durchschaltung des Telefons
// Siehe auch Diagramm VerbindungKommend.ppt


bool MithoerenUndAufTraegerWarten()
	{
	// TODO entsprechend Flags und was weiß ich nicht entscheiden...
	return false;
	}
	
	
void WarteEndeTelefonat()
	{
	set_LEDROT();
	clr_LEDGELB();
	clr_LEDGRUEN();
	TMsTimer EndeTimer;
	StartTimer(&EndeTimer);
	sei(); // Workaround?
	while (TimerVal(&EndeTimer) < 3000)
		{
		if (!get_TELAKTIV()) // negiertes Signal
			{ // noch abgenommen
			StartTimer(&EndeTimer);
			set_LEDBLAU();
			}
		else
			clr_LEDBLAU();
		wdt_reset();
		}
	Grundstellen(false);
	}
	
	
bool FreiesEndgeraetFuerAnruf()
// sucht ein freies Endgerät im Falle eines kommenden Rufs. Liefert false, falls kein Anschluss frei
// 1. Hauptanschluss (bei Flag KonfigBit_FesterHauptanschluss)
// oder
// 2. Letzes Gerät, von dem ein abgehendes Telefonat geführt wurde (wenn Flag KonfigBit_FesterHauptanschluss nicht gesetzt)
// sowie
// 3. bei besetztem Gerät nach 1. bzw. 2. NÄCHSTER freier Anschluss
// gefundener Anschluss steht nachher in AktuellEmpfaenger
	{
	if (BIT_IS_SET(KonfigBits, KonfigBit_FesterHauptanschluss))
		if (Hauptanschluss != 0)
			AktuellEmpfaenger = Hauptanschluss * 2;
		else
			AktuellEmpfaenger = BusAdrMin;
	else // kein fester Hauptanschluss
		if (AktuellEmpfaenger != 0)
			; // die aktuelle Einstellung behalten
		else
		 	if (Hauptanschluss != 0)
				AktuellEmpfaenger = Hauptanschluss * 2;
			else
				AktuellEmpfaenger = BusAdrMin;
		
	if (AktuellEmpfaenger != Hauptanschluss * 2 || BIT_IS_SET(KonfigBits, KonfigBit_SucheAlternativBeiBesetzt))
		{ // jetzt anderes Endgerät suchen, keine Leitung und kein Spezialgerät
		wdt_reset();
		SendeLebenszeichen();
		uint8_t Start = AktuellEmpfaenger;
		while (true)
			{
			int16_t Stat = GetStatus(AktuellEmpfaenger);
			if (Stat >= 0 
				&& BIT_IS_SET(Stat, StatBit_Frei) 
				&& !BIT_IS_SET(Stat, StatBit_LeitungKennung)
				&& !BIT_IS_SET(Stat, StatBit_SpezialGeraetKennung))
				break; // gefunden!
			AktuellEmpfaenger += 2;
			if (AktuellEmpfaenger > BusAdrMax)
				AktuellEmpfaenger = BusAdrMin;
			if (AktuellEmpfaenger == Start) // alle einmal probiert
				return false;
			wdt_reset();
			}
		}
	if (Hauptanschluss == 0)
		Hauptanschluss = AktuellEmpfaenger / 2;
	return true;
	}
			
		
bool Anruferkennung()
	// Liefert true, wenn ein Anruf durch das TxP angenommen werden soll
	// Liefert false, wenn der Anruf beendet wurde oder es ein Telefonat war.
	// Funktion blockiert also während eines Telefonanrufs
	{
	#define Klingelsignal() (!get_ANRUF())
	
	if (BusEigenAdresse == BusAdrUngueltig)
		return false;

	// TODO: Schnellstart?
	CLR_BIT_Status(StatBit_Frei);
	CLR_BIT_Status(StatBit_LeitungKennung); 
	SET_BIT_Status(StatBit_AngerufenBelegt);
	
	set_LEDGELB();
	set_LEDBLAU();

	TMsTimer T1, T2;
	
	StartTimer(&T1);
	while (TimerVal(&T1) < 100 || !Klingelsignal()) // Schleifenabbruch, wenn nach 100 ms ein Klingelsignal erkannt wird
		{
		if (TimerVal(&T1) > 1000)
			{
			WarteEndeTelefonat();
			return false;
			}
		wdt_reset();
		}
			
	clr_LEDBLAU();
	set_LEDGRUEN();
	uint8_t KlingelZaehler = 0;
	
	while (true)
		{ // Anzahl Klingelzeichen zählen
		// KlingelAnf
		StartTimer(&T1); // T1 misst die Länge ab Beginn des Klingelzeichens
		StartTimer(&T2); // T2 misst die Länge zwischen den Klingelzeichen (kann aber auch eine Pause zwischen 
						 // Einzelimpulsen eines Klingelzeichens sein
		
		// KlingelPruef
		while (TimerVal(&T2) < 700)
			{ // Schleife wartet auf mindestens 700 ms Pause zwischen Klingelimpulsen
			if (Klingelsignal())
				{
				StartTimer(&T2);
				set_LEDGELB();
				}
			else
				clr_LEDGELB();
			wdt_reset();
			}
				
		// KlingelPause begonnen
		if (TimerVal(&T1) >= 700 + 700) // 700 ms mindest Klingellänge + 700 ms Pausenbeginn (aus While-Schleife oben)
			KlingelZaehler++;

		if (KlingelZaehler >= AnnahmeKlingelzeichen || !get_TASTE()) // HACK Taste (schaltet gegen Masse) nimmt Anruf an 
			return true;
			
		while (!Klingelsignal())
			{
			if (TimerVal(&T1) > 20000)
				return false; // Ende des Klingelns, keiner hatte Angenommen
				
			if (!get_TELAKTIV()) // negiertes Signal
				{ // Telefonhörer abgenommen?
				// TelMithPruef
				set_LEDROT();
				StartTimer(&T1);
				while (!get_TELAKTIV() && !Klingelsignal()) // immer noch abgehoben...
					{
					if (TimerVal(&T1) > 500)
						{
						if (MithoerenUndAufTraegerWarten())
							return true;
						WarteEndeTelefonat();
						return false;
						}
					wdt_reset();
					}
				clr_LEDROT();
				// entweder hat es wieder angefangen zu Klingeln oder es wurde aufgelegt (oder der Schleifenschluss war nur kurz)
				} // if Abgehoben
			wdt_reset();
			} // while !Klingelsignal
		} // while true
	} // Anruferkennung
						
// SchnellstartSetzen: Nächsten kommenden Anruf sofort bearbeiten
// ==============================================================

/*

SchnellstartSetzen:
	lds temp, KonfigBits
	rjrc temp, KonfigBit_Schnellstart, SchnellstartLoeschen

	lds temp, VerbKommendFlags
	andi temp, (1<<VKF_Schnellstart) | (1<<VKF_SchnellstartSperre)
	brne SchnellstartSetzenWdh	// wenn schon Schnellstart ODER Sperre gesetzt, dann Sperre setzen
	ldi temp, (1<<VKF_Schnellstart)
	sts VerbKommendFlags, temp
	sts SchnellstartLoeschTimer, TCNT1XH
	ret

SchnellstartSetzenWdh:
	ldi temp, (1<<VKF_SchnellstartSperre)
	sts VerbKommendFlags, temp
	sts SchnellstartLoeschTimer, TCNT1XH
	ret


// SchnellstartLoeschen: Nächsten kommenden Anruf wieder normal bearbeiten
// =======================================================================

SchnellstartLoeschen:
	ldi temp, 0
	sts VerbKommendFlags, temp
	ret

*/


static bool PruefeBusSchluss(uint8_t Code, uint8_t FehlerCode)
	// Wenn Code != 0 wurde Empfangs-Code schon vorher aus puffer geholt 
	// liefert true, wenn abgebrochen werden soll.
	{
	if (Code == 0)
		if (!GetEmpfByte(&Code))
			return false;
		
	if (Code == BusKdoSchluss || Code == BusQuittSchluss)
		{
		Grundstellen(Code == BusKdoSchluss);
		return true;
		}

	DiagDatenSpeichern(0x01);
	DiagDatenSpeichern(Code);

#ifdef FALSCHKDO_FEHLERSTOP
	FehlerStop(FehlerCode);
#endif //def FALSCHKDO_FEHLERSTOP
	return false; 
	}
	

static bool PruefeTimerAbbruch(TMsTimer *Timer, uint16_t MaxTimer)
	{
	if (TimerVal(Timer) > MaxTimer)
		{ // es braucht zu lange...
		set_LEDROT();
		BusSenden(BusKdoSchluss);
		WarteSchlussQuittung(1500);
		Grundstellen(false);
		return true;
		}
	return false;
	}
	
	
static bool HandshakeKommend(uint8_t *Nebenstelle)
	{
	// 't' senden und auf Zeichen 'x' warten
	if (!Handshake300('t', 't', 'x', 10 * 10/3)) // 10 Sekunden
		return false;

	set_LEDBLAU();
	
	// 'p' senden und auf Zeichen 'O' warten (ggf. kommt vor dem O auch eine Nebenstellen-Durchwahl
	Hs300ExtraZeichen = '\0';
	*Nebenstelle = 0;
	if (!Handshake300('p', 'p', 'O', 10 * 10/3))
		return false;
		
	clr_LEDBLAU();
	
	if (Hs300ExtraZeichen >= '0' && Hs300ExtraZeichen <= '9')
		*Nebenstelle = Hs300ExtraZeichen - '0';

	return true;
	}
	

void VerbindungKommend()
	{
	if (!FreiesEndgeraetFuerAnruf())
		{
		// FehlerStop(15); //HACK
		WarteEndeTelefonat();
		return;
		}
	// potenzieller Verbindungspartner steht jetzt in AktuellEmpfaenger

	TMsTimer Timer, Abbruch;

	set_GABEL(); // abnehmen
	StartTimer(&Timer);
	while (TimerVal(&Timer) < 100)
		;
	set_TELAUS(); // Telefon abschalten

	// kurz Warten...
	
	StartTimer(&Timer);
	while (TimerVal(&Timer) < 500)
		wdt_reset();

	set_LEDGRUEN(); // bleibt ein als Zeichen für kommenden Ruf
		
	// Answer-Tone senden
	StartAnswerTone();
	
	// Auf Träger des Senders warten
	StartTimer(&Timer);
	StartTimer(&Abbruch);
	GetState(true);
	while (true)
		{
		if (BIT_IS_SET(GetState(false), STATEBIT_CARRIER))
			{
			set_LEDGELB();
			if (TimerVal(&Timer) > 300)
				break;
			}
		else
			{
			clr_LEDGELB();
			StartTimer(&Timer);
			}

		if (TimerVal(&Abbruch) > 1500)
			break; // einfach weitermachen, auch wenn Gegenstelle noch keinen Träger sendet...
			
		wdt_reset();
		}

	StopSpecialTone();
	
	StartTimer(&Timer);
	while (TimerVal(&Timer) < 50)
		wdt_reset();
		
	// V.21 einschalten
	StartV21(false);
	
	Seriell300Baud();
	
	uint8_t Nst;
	if (!HandshakeKommend(&Nst))
		{
		clr_LEDGELB();
		Grundstellen(false);
		return;
		}

	set_LEDROT();
	BusWarteFertig();
		
	if (Nst >= 1 && Nst <= 9 
	    && BIT_IS_SET(KonfigBits, KonfigBit_DurchwahlErlaubt)
		&& NebenstellenTabelle[Nst] != 0
		&& NebenstellenTabelle[Nst] << 1 != AktuellEmpfaenger)
		{ // direkt angewählte Nebenstelle versuchen...
		uint8_t GewaehlteNebenstelle = NebenstellenTabelle[Nst] << 1;
		int16_t Stat = GetStatus(GewaehlteNebenstelle);
		if (Stat >= 0 && BIT_IS_SET(Stat, StatBit_Frei) && !BIT_IS_SET(Stat, StatBit_LeitungKennung))
			// hier darf es auch ein Spezialgerät sein
			BusVerbPartner = GewaehlteNebenstelle; // beide sind schon << 1 für I²C-Adressen
		}
	else 
		BusVerbPartner = AktuellEmpfaenger; // beide sind schon << 1 für I²C-Adressen
		
	BusSenden(BusEigenAdresse >> 1);
	BusWarteFertig();
	
	if (BusErgebnis == Ok)
		{ // Auswahl erfolgreich 
		}
	else 
		{ // vorhanden, aber besetzt (war eben noch frei!) ODER verschwunden
		Grundstellen(false);
		return;
		}

	BusErgebnis = Ok; // um spätere Probleme zu vermeiden
	BusAuftrag = Nichts;
	
	BusSenden(BusKdoEin);
	BusWarteFertig();
	
	StartTimer(&Timer);
	while (true)
		{
		SendeLebenszeichen();

		uint8_t Code;
		if (GetEmpfByte(&Code))
			{
			if (Code == BusQuittEin)
				break; // der einzige normale Ausstieg aus dieser Schleife
			else if (PruefeBusSchluss(Code, 7))
				return;
			} // if BusKdo empfangen
		
		if (PruefeTimerAbbruch(&Timer, 5000))
			// Einschalt-Quittung braucht zu lange... Achtung: Umschaltung aus Lokal-Betrieb berücksichtigen
			return;

		} // while true

	clr_LEDROT();
		
	// 3 mal 'K' senden
	Handshake300('K', 'K', '\0', 3);

	// warten, bis Stop-Bit gesendet...
	StartTimer(&Timer);
	while (TimerVal(&Timer) < 100)
		SendeLebenszeichen();

	SeriellAus();
	
	clr_LEDGELB();
	
	void VerbindungHergestellt();
	VerbindungHergestellt();	

	}
	

/*
void VerbindungKommendSimulieren()
	{
	if (!FreiesEndgeraetFuerAnruf())
		{
		WarteEndeTelefonat();
		return;
		}
	// potenzieller Verbindungspartner steht jetzt in AktuellEmpfaenger

	set_LEDROT();
	
	// Warten... (Verbindungsaufbau nachbilden)
	TMsTimer Timer;
	
	StartTimer(&Timer);
	while (TimerVal(&Timer) < 3000)
		wdt_reset();
		
	set_LEDGELB(); // und ROT

	BusWarteFertig();
	
	BusVerbPartner = AktuellEmpfaenger; // beide sind schon << 1 für I²C-Adressen

	BusSenden(BusEigenAdresse >> 1);
	
	BusWarteFertig();

	clr_LEDROT(); // bleibt GELB
	
	bool IstBesetzt;
	
	if (BusErgebnis == Ok)
		{ // Auswahl erfolgreich 
		IstBesetzt = false;
		}
	else if (BusErgebnis == Besetzt)
		{ // vorhanden, aber besetzt (war eben noch frei!)
		IstBesetzt = true;
		}
	else
		{ // interner Teilnehmer nicht existent
		// FehlerStop(14); // HACK
		return;
		}

	BusErgebnis = Ok; // um spätere Probleme zu vermeiden
	BusAuftrag = Nichts;

	if (IstBesetzt)
		{
		Grundstellen(false);
		return;
		}

	BusSenden(BusKdoEin);
	BusWarteFertig();
	
	set_LEDGRUEN(); // und GELB
	
	StartTimer(&Timer);
	while (true)
		{
		SendeLebenszeichen();
		
		uint8_t Code;
		if (GetEmpfByte(&Code))
			{
			if (Code == BusQuittEin)
				break; // der einzige Ausstieg aus dieser Schleife
			else if (PruefeBusSchluss(Code, 13))
				return;

			} // if BusKdo empfangen
		
		if (PruefeTimerAbbruch(&Timer, 1500)) // Einschalt-Quittung braucht zu lange...
			return;

		}
		
	clr_LEDGELB(); // bleibt GRUEN

	StartTimer(&Timer);
	bool TasteAlt = true;
	
	while (true) // Simulation der Verbindung
		{
		uint8_t Code;
		if (GetEmpfByte(&Code))
			{
			if (PruefeBusSchluss(Code, 15))
				return;
			}
		
		bset_LEDBLAU(!BusEmpfMark);
		
		if (TasteAlt == get_TASTE())
			{
			if (TimerVal(&Timer) > 934 && BusAuftrag == Fertig && BusFrei)
				{
				BusSenden(TasteAlt ? BusKdoMarkWdh : BusKdoSpaceWdh);
				StartTimer(&Timer);
				}
			}
		else if (BusAuftrag == Fertig && BusFrei)
			{
			TasteAlt = get_TASTE();
			bset_LEDROT(!TasteAlt);
			BusSenden(TasteAlt ? BusKdoMark : BusKdoSpace);
			StartTimer(&Timer);
			}
			
		} // while true
	}
	
//*/


void ZifferWaehlen(uint8_t Ziffer)
	{
	TMsTimer Timer;
	StartTimer(&Timer);
	StartDTMF(Ziffer);
	while (TimerVal(&Timer) < 200)
		;
	StopDTMF();
	while (TimerVal(&Timer) < 600) 
		;
	}
	

void NummerWaehlen(char *Nummer)
	{
	while (*Nummer >= '0' && *Nummer <= '9')
		{
		ZifferWaehlen(*Nummer - '0');
		Nummer++;
		wdt_reset();
		}
	}

	
// Test-Routine für Tonerkennung
// =============================

/*

static void DetectionTest(bool Orig, char *Nummer)
	{
	bool IstEin; // Träger

	if (Nummer != NULL)
		{
		set_GABEL();
		set_TELAUS(); // Telefon abschalten
		TMsTimer Timer;
		StartTimer(&Timer);
		while (TimerVal(&Timer) < 1000)
			wdt_reset();
		NummerWaehlen(Nummer);
		}
	
	StartDetection(Orig); // damit der Answer-Tone auch erkannt wird
	IstEin = false;
	clr_LEDBLAU();
	GetState(true);
	while (true)
		{
		uint8_t Stat = GetState(false);
		bset_LEDROT(BIT_IS_SET(Stat, STATEBIT_CALLPROGRESS));
		bset_LEDGELB(BIT_IS_SET(Stat, STATEBIT_ANSWERTONE));
		bset_LEDGRUEN(BIT_IS_SET(Stat, STATEBIT_CARRIER));

		TastePruefen();
		wdt_reset();

		if (Tastendruck != NichtGedr) // Taste gedrückt	
			{
			if (Tastendruck == Lang)
				{
				Tastendruck = NichtGedr;
				clr_GABEL();
				clr_TELAUS(); // Telefon anschalten
				ModemReset();
				return;
				}
			Tastendruck = NichtGedr;
			if (!IstEin)
				{
				StartV21(Orig);
				IstEin = true;
				set_LEDBLAU();
				}
			else
				{
				StartDetection(Orig);
				IstEin = false;
				clr_LEDBLAU();
				} // else IstEin
			} // if Tastendruck != NichtGedr
		} // while true
	} // DetectionTest

//*/
	

static bool HandshakeGehend(uint8_t Nebenstelle)
	{
	// auf Zeichen 't' warten
	if (!Handshake300('\0', '\0', 't', 15 * 10/3)) // 15 Sekunden
		{
		DiagDatenSpeichern(0x02); DiagDatenSpeichern(0x01); 
		return false;
		}
		
	set_LEDBLAU();
	
	// 'x' senden und auf Zeichen 'p' warten
	if (!Handshake300('x', 'x', 'p', 6 * 10/3))
		{
		DiagDatenSpeichern(0x02); DiagDatenSpeichern(0x02); 
		return false;
		}

	set_LEDROT();
	
	// Nebenstellen-Nummer und 'O' senden und auf Zeichen 'K' warten
	if (!Handshake300((Nebenstelle > 0) ? ('0' + Nebenstelle) : 'O', 'O', 'K', 6 * 10/3))
		{
		DiagDatenSpeichern(0x02); DiagDatenSpeichern(0x03); 
		return false;
		}

	clr_LEDBLAU();
	clr_LEDROT();
		
	return true;
	}


// Test-Routine für abgehenden Anruf...
// ====================================

/*

void TestAnruf(char* Nummer)
	{
	clr_LEDROT();
	clr_LEDGELB();
	clr_LEDGRUEN();

	TMsTimer Timer;
	TMsTimer Abbruch;

	set_GABEL(); // abnehmen
	StartTimer(&Timer);
	while (TimerVal(&Timer) < 100)
		;
	set_TELAUS(); // Telefon abschalten
	
	// Wählton prüfen (max. 5 Sekunden)
	StartTimer(&Timer);
	StartTimer(&Abbruch);
	StartDetection(true);
	GetState(true);
	//set_LEDGELB();
	while (true)
		{
		if (BIT_IS_SET(GetState(false), STATEBIT_CALLPROGRESS))
			{
			//set_LEDROT();
			if (TimerVal(&Timer) > 1000)
				break;
			}
		else
			{
			//clr_LEDROT();
			StartTimer(&Timer);
			}

		if (TimerVal(&Abbruch) > 5000)
			{
			ModemReset();
			clr_GABEL();
			clr_TELAUS(); // Telefon anschalten
			return; // kein Wählton
			}
		wdt_reset();
		}

	//set_LEDGRUEN();
	//clr_LEDROT();

	// Wählen...
	NummerWaehlen(Nummer);

	//clr_LEDGELB();
		
	// Answer-Tone prüfen
	StartTimer(&Timer);
	StartTimer(&Abbruch);
	StartDetection(true);
	GetState(true);
	while (true)
		{
		if (BIT_IS_SET(GetState(false), STATEBIT_ANSWERTONE) || BIT_IS_SET(GetState(false), STATEBIT_CARRIER))
			{
			set_LEDROT();
			if (TimerVal(&Timer) > 600)
				break;
			}
		else
			{
			clr_LEDROT();
			StartTimer(&Timer);
			}

		if (TimerVal(&Abbruch) > 15000)
			{
			Grundstellen(false);
			return; // kein Answer-Tone
			}
		wdt_reset();
		}
	
	set_LEDGRUEN();
	//set_LEDGELB();
	clr_LEDROT();

	// V.21 einschalten
	Transmit(true); // setzt nur "Mark".
	StartV21(true);

	// Auf Träger von der Gegenstelle warten
	StartTimer(&Timer);
	StartTimer(&Abbruch);
	GetState(true);

	while (true)
		{
		if (BIT_IS_SET(GetState(false), STATEBIT_CARRIER) && !BIT_IS_SET(GetState(false), STATEBIT_ANSWERTONE))
			{ // Answer-Tone setzt auch Carrier-Flag, daher auf Wegbleiben des Answer-Tones warten
			set_LEDROT();
			if (TimerVal(&Timer) > 150)
				break;
			}
		else
			{
			clr_LEDROT();
			StartTimer(&Timer);
			}

		if (TimerVal(&Abbruch) > 10000)
			{
			ModemReset();
			clr_GABEL();
			clr_TELAUS(); // Telefon anschalten
			return; // kein Answer-Tone
			}
		wdt_reset();
		}
	
	clr_LEDGELB();
	clr_LEDGRUEN();
	set_LEDROT();

	Seriell300Baud();
	
	// kurz Warten...
	StartTimer(&Timer);
	while (TimerVal(&Timer) < 200)
		wdt_reset();
		
	set_LEDGRUEN();

	if (!HandshakeGehend(0))
		{
		set_LEDGELB();
		clr_GABEL();
		clr_TELAUS(); // Telefon anschalten
		ModemReset();
		SeriellAus();
		return;
		}
	SeriellAus();

	set_LEDGRUEN();
	clr_LEDGELB();
	clr_LEDROT();

	// kurz Warten...
	StartTimer(&Timer);
	while (TimerVal(&Timer) < 1000)
		wdt_reset();
		
	SeriellUmsetzInit();
	char *p = "\r\nhallo, hier ist wieder fred mit einem kurzen verbindungstest...\r\n\027"; // \027 = ^W = WerDa
	bool ModeZifferE = false, ModeZifferS = false;
	uint8_t Code1 = 255, Code2 = 255;
	bool SendMark = true;

	StartTimer(&Timer);
	
	while (true)
		{
		wdt_reset();
		TastePruefen();
		if (Code1 == 255) // Umsetzen
			{
			if (*p != '\0')
				{
				ZeichenZuCode2(*p, &ModeZifferS, &Code1, &Code2);
				p++;
				}
			else if (Tastendruck != NichtGedr) 
				{ 
				Tastendruck = NichtGedr;
				break; // nichts mehr zu senden
				}
			}
		if (Code1 != 255 && SerUmSendBitNr == SerUmSendWarte)
			{
			SerUmSendDaten = Code1;
			SerUmSendBitNr = SerUmSendStart;
			Code1 = Code2;
			Code2 = 255;
			}
		bset_LEDGELB(!ReceiveMark());
		SeriellUmsetzung(ReceiveMark(), &SendMark);
		Transmit(SendMark);
		if (SerUmEmpfBitNr == SerUmEmpfFertig)
			{
			char c;
			c = CodeZuZeichen(SerUmEmpfDaten, &ModeZifferE);
			SerUmEmpfBitNr = SerUmEmpfWarte;
			}

		if (BIT_IS_SET(GetState(false), STATEBIT_CARRIER))
			{ 
			StartTimer(&Timer);
			clr_LEDROT();
			}
		else
			{ // Träger ist weg
			set_LEDROT();
			if (TimerVal(&Timer) > 1500)
				break;
			}
			
		} // while true

	clr_GABEL();
	clr_TELAUS(); // Telefon anschalten
	ModemReset();
	}
	
//*/

// Gehende Verbindung
// ==================

static void VerbindungGehend()
	{
	// Empfangenen Befehl auswerten
	// ----------------------------
	uint8_t Code;
	if (!GetEmpfByte(&Code))
		return;

	if (Code == BusQuittSchluss || Code == BusKdoSchluss)
		return;

	if (Code > BusKdoVerbAufnahme)
		{
#ifdef FALSCHKDO_FEHLERSTOP
		FehlerStop(4); 
#else		
		return;
#endif
		}

	set_LEDGELB(); // bleibt als Zeichen für gehende Verbindung dauernd ein
			
	BusVerbPartner = Code << 1;
	CLR_BIT_Status(StatBit_Frei);
	CLR_BIT_Status(StatBit_LeitungKennung); 
	CLR_BIT_Status(StatBit_AngerufenBelegt);

	if (Hauptanschluss == 0)
		Hauptanschluss = BusVerbPartner >> 1;
		
	AktuellEmpfaenger = BusVerbPartner;
		
	wdt_reset();
	
	// warte auf Einschalt-Kommando
	// ----------------------------
	set_LEDROT();
	while (true)
		{
		SendeLebenszeichen();

		uint8_t Code;
		if (GetEmpfByte(&Code))
			{
			if (Code == BusKdoEin)
				break; // der einzige Ausstieg aus dieser Schleife
			else if (PruefeBusSchluss(Code, 5))
				return;
	
			} // if BusKdo empfangen
		} // while true
		
	TMsTimer WahlAbstand;

	set_GABEL();
	StartTimer(&WahlAbstand); // leicht missbraucht...
	while (TimerVal(&WahlAbstand) < 100)
		;
	set_TELAUS(); // Telefon abschalten

	wdt_reset();
	
	clr_LEDROT();
	set_LEDBLAU();
	
	bool NachwahlzifferGespeichert = false;
	uint8_t Nachwahlziffer = 0;
	uint8_t Ziffer;
	bool ErsteZifferGewaehlt = false;
	TMsTimer StateFilterTimer;
	TMsTimer SignaltonTimer;
	TMsTimer TraegerTimer;
	TMsTimer TraegerAusTimer;
	TMsTimer AbbruchTimer;
	uint8_t LastState = 0;
	bool TonErkannt = false;
	bool TonGezaehlt = false;
	uint8_t BesetztZaehler = 0;
	//uint8_t AnzahlWahlZiffern = 0; wird nicht mehr gebraucht

	StartDetection(true);
	StartTimer(&TraegerTimer); 
	StartTimer(&SignaltonTimer);
	StartTimer(&AbbruchTimer);
	GetState(true);
	
	enum { PhWarteWaehlton, PhWahl, PhKlingeln, PhAnswertone, PhWarteTraeger, PhHandshake } Phase;
	Phase = PhWarteWaehlton;
	
	// Hauptschleife mit drei Aufgabe:
	// a) auf Telegramme reagieren
	// b) auf Zustandswechsel des Modems reagieren
	// c) nach 10 Sekunden Stillstand jeder Phase (außer Wahl) abbrechen
	
	while (Phase != PhHandshake)
		{ // LED: Grün = Wahlziffer, blau = Träger,  rot = Frei- oder Besetztton
		SendeLebenszeichen();

		// Aufgabe A: Telegramme bearbeiten
		// --------------------------------
		uint8_t Code;
		if (GetEmpfByte(&Code))
			{ // Bus-Kommando empfangen
			switch (Code)
				{
				case BusKdoWahlziffer0 ... BusKdoWahlziffer9:
					Ziffer = Code - BusKdoWahlziffer0; // Damit die Ziffer nicht mehr zerstört werden kann
					set_LEDGRUEN();
					
					if (Phase == PhWarteWaehlton)
						FehlerStop(5); // vor der Wahlfreigabe darf kein Wahlimpuls kommen...
						
					if (Phase == PhWahl)
						{
						StopDetection();
						if (NachwahlzifferGespeichert)
							{
							ZifferWaehlen(Nachwahlziffer);
							// AnzahlWahlZiffern++; wird nicht mehr gebraucht
							NachwahlzifferGespeichert = false;
							SendeLebenszeichen();
							wdt_reset(); // da der Wählimpuls 0,2 Sekunden gedauert hat.
							}
						if (ErsteZifferGewaehlt && TimerVal(&WahlAbstand) > 2500)
							{
							Nachwahlziffer = Ziffer;
							NachwahlzifferGespeichert = true;
							}
						else
							{
							ZifferWaehlen(Ziffer);
							// AnzahlWahlZiffern++; wird nicht mehr gebraucht
							SendeLebenszeichen();
							wdt_reset(); // da der Wählimpuls 0,2 Sekunden gedauert hat.
							}
						StartTimer(&WahlAbstand);
						ErsteZifferGewaehlt = true;
						StartDetection(true);
						GetState(true);
						LastState = 0;
						}
					else
						{ // Es hat schon geklingelt, also kann es nur eine Nachwahlziffer sein. Da gilt jetzt die letzte...
						Nachwahlziffer = Ziffer;
						NachwahlzifferGespeichert = true;
						} // else Phase >= PhKlingeln

					clr_LEDGRUEN();
					break; // case BusKdoWahlziffer
				
				default:
					if (PruefeBusSchluss(Code, 5))
						return;
					break;
				
				} // switch Code
				
			} // if GetEmpfByte
		
		// Aufgabe B: Zustandswechsel des Modems
		// -------------------------------------
		switch (Phase)
			{
			case PhWarteWaehlton:
				if (!BIT_IS_SET(GetState(false), STATEBIT_CALLPROGRESS))
					{
					clr_LEDROT();
					StartTimer(&SignaltonTimer);
					}
				else 
					{
					set_LEDROT();
					if (TimerVal(&SignaltonTimer) >= 500)
						{ // 0,5 Sekunden Wählton erkannt
						clr_LEDBLAU();
						BusSenden(BusKdoWahlFreigabe); // Wahlaufforderung senden
						Phase = PhWahl;
						}
					}
				break;
				
			case PhWahl:
			case PhKlingeln:
				StartTimer(&AbbruchTimer); // bewirkt, dass es keinen Timer-Abbruch gibt

				// Kurzzeitige Wechsel in den Status-Bits (Detection-Register) ausfiltern...
				uint8_t NewState = GetState(false);
				if (NewState == LastState)
					StartTimer(&StateFilterTimer);
				else if (TimerVal(&StateFilterTimer) > 50)
					LastState = NewState;
				
				bset_LEDROT(BIT_IS_SET(LastState, STATEBIT_CALLPROGRESS));
				bset_LEDBLAU(BIT_IS_SET(LastState, STATEBIT_CARRIER) || BIT_IS_SET(LastState, STATEBIT_ANSWERTONE));
											
				if (!ErsteZifferGewaehlt || TimerVal(&WahlAbstand) < 800)
					{
					LastState = 0; 
					GetState(false);
					StartTimer(&TraegerTimer);
					}
				else
					{ // erst nach der Ersten Ziffer ist das Hören auf Träger oder Besetzt sinnvoll...
					if (BIT_IS_SET(LastState, STATEBIT_CALLPROGRESS))
						{ // es wird ein Ton empfangen (Besetzt oder Wählton ist unklar...)
						if (!TonErkannt)
							{
							StartTimer(&SignaltonTimer);
							TonErkannt = true;
							}
						else if (!TonGezaehlt && TimerVal(&SignaltonTimer) > 200) 
							{
							TonGezaehlt = true;
							BesetztZaehler++;
							Phase = PhKlingeln;
							}
						} // Signalton ist ein
					else
						{ // Signalton ist aus
						if (TonErkannt)
							{ // Wechsel auf aus
							StartTimer(&SignaltonTimer);
							TonErkannt = false;
							}
						else if (TonGezaehlt && TimerVal(&SignaltonTimer) > 200)
							{
							TonGezaehlt = false; // das Rücksetzen erst in der langen Pause bewirkt, dass kurze Unterbrechungen
								// nicht gezählt werden
							}
						} // else Signalton ist aus
					
					if (BesetztZaehler >= 6)
						{
						set_LEDROT();
						clr_LEDGELB();
						clr_LEDBLAU();
						DiagDatenSpeichern(0x03); DiagDatenSpeichern(0x01); 
						BusSenden(BusKdoSchluss);
						WarteSchlussQuittung(2500);
						Grundstellen(false);
						return;
						}
				
					// TODO probeweise Träger senden...
					
					if (BIT_IS_SET(LastState, STATEBIT_CARRIER) || BIT_IS_SET(LastState, STATEBIT_ANSWERTONE))
						{
						if (TimerVal(&TraegerTimer) > 700)
							{ // Träger ODER Answerton ist vorhanden, WECHSEL zum Warten auf Träger allein...
							// jetzt eigenen Träger senden
							Transmit(true); // setzt nur "Mark".
							StartV21(true);

							// danach auf Ende des Answer-Tones warten, dazu Timer starten
							StartTimer(&TraegerAusTimer);
							StartTimer(&AbbruchTimer);
							GetState(true);

							set_LEDGRUEN();
							clr_LEDBLAU();
							clr_LEDROT();
							
							// TODO Answer-Tone auch in Org-Richtung (für VoIP?)
							
							Phase = PhAnswertone; 
							}
						} // Carrier vorhanden
					else
						{ // kein Carrier vorhanden
						StartTimer(&TraegerTimer);
						clr_LEDBLAU();
						}

					} // else ErsteZifferGewaehlt && TimerVal(&WahlAbstand) >= 800
		
				break; // case PhWahl, case PhKlingeln
				
			case PhAnswertone:
				if (!BIT_IS_SET(GetState(false), STATEBIT_ANSWERTONE))
					{ 
					clr_LEDBLAU();
					if (TimerVal(&TraegerAusTimer) > 150)
						{
						StartTimer(&TraegerTimer);
						StartTimer(&AbbruchTimer);
						GetState(true);
						Phase = PhWarteTraeger;
						}
					}
				else
					{
					set_LEDBLAU();
					StartTimer(&TraegerAusTimer);
					}
					
				break; // case PhAnswertone
				
			case PhWarteTraeger:
				if (BIT_IS_SET(GetState(false), STATEBIT_CARRIER))
					{
					set_LEDGRUEN();
					if (TimerVal(&TraegerTimer) > 250)
						{
						StartTimer(&AbbruchTimer);
						Phase = PhHandshake;
						}
					}
				else
					{
					clr_LEDGRUEN();
					StartTimer(&TraegerTimer);
					}
				break;

			case PhHandshake:
				break;
				
			} // switch Phase
		
		// Aufgabe C: ggf. Abbruch nach 10 Sekunden
		// ----------------------------------------
		if (PruefeTimerAbbruch(&AbbruchTimer, 10000))
			{
			DiagDatenSpeichern(0x03); DiagDatenSpeichern(0x02); 
			return; // Answer-Tone steht zu lange...
			}
		
		} // Ende der Hauptschleife: while (Phase != PhHandshake)
		
	Seriell300Baud();

	// kurz Warten...
	StartTimer(&TraegerTimer);
	while (TimerVal(&TraegerTimer) < 200)
		SendeLebenszeichen();
		
	// gelb und grün leuchten...

	if (!HandshakeGehend(NachwahlzifferGespeichert ? Nachwahlziffer : 0))
		{
		// DiagDatenSpeichern erfolgt schon durch HandshakeGehend
		set_LEDROT();
		clr_LEDGELB();
		BusSenden(BusKdoSchluss);
		WarteSchlussQuittung(2500);
		Grundstellen(false);
		return; // kein erfolgreiches Handshake
		}

	SeriellAus();
	set_LEDBLAU();
	clr_LEDROT(); // ist noch vom Handshake an
	
	// kurz Warten... TODO warum eigentlich?
	StartTimer(&TraegerTimer);
	while (TimerVal(&TraegerTimer) < 100)
		SendeLebenszeichen();

	// rufendes Endgerät einschalten		
	BusSenden(BusQuittEin);
	
	wdt_reset();
	
	BusWarteFertig();

	clr_LEDBLAU();
	clr_LEDGRUEN();
	// gelb bleibt an...
	
	void VerbindungHergestellt();
	VerbindungHergestellt();	
	} // VerbindungGehend()


void VerbindungHergestellt()
	{ // Endgerät ist ein und Verbindung ist kommend ODER gehend fertig aufgebaut...
	TMsTimer TraegerPruefTimer; // alle 0,02 Sekunden wird Träger geprüft. Bei 50 x nein Verbindungsabbau...
	TMsTimer PegelWdhTimer;
	TMsTimer LongDistancePruefTimer;
	bool AltEmpfMark;
	bool PegelSchnellWdh;
	//uint8_t DiagnoseSpeicherPos = 0;
	uint8_t TelegrammFehlerZaehler = 0;
	uint8_t TraegerFehlZaehler = 0;
	
	StartTimer(&PegelWdhTimer);
	StartTimer(&LongDistancePruefTimer);
	StartTimer(&TraegerPruefTimer);

	AltEmpfMark = false;
	PegelSchnellWdh = false;
	
	SET_BIT_Status(StatBit_Verbunden);

	Transmit(true); // Initial-Pegel setzen
	
	while (true)
		{ // LED: rot = schlechter Pegel oder verlorenes Telegramm, gelb = Sende Space, blau = empfange Space
		uint8_t Code;

		if (GetEmpfByte(&Code))
			{
			switch (Code)
				{
				case BusKdoSchluss:
					Grundstellen(true);
					//eeprom_write_byte(&DiagnoseSpeicher[DiagnoseSpeicherLen-1], DiagnoseSpeicherPos);
					return;
					
				case BusQuittSchluss:
					Grundstellen(false);
					return; // eigentlich komisch, dass die Quittung einfach so kommt...
				
				case BusQuittEin:
					break; // Rest von der gerade passierten Einschaltung
										
				default:
#ifdef FALSCHKDO_FEHLERSTOP
					FehlerStop(13);
#endif //def FALSCHKDO_FEHLERSTOP
					break;

				} // switch Code
			} // if GetEmpfByte
			
		// vom Bus kommandierten Pegel an Modem geben
		if (BusEmpfMarkwechsel)
			{
			Transmit(BusEmpfMark);
			if (BusEmpfMark)
				{
				if (BIT_IS_SET(Status, StatBit_AngerufenBelegt))
					clr_LEDGELB();
				else
					clr_LEDGRUEN();
				SET_BIT_Status(StatBit_FsBefEin);
				}
			else
				{
				if (BIT_IS_SET(Status, StatBit_AngerufenBelegt))
					set_LEDGELB();
				else
					set_LEDGRUEN();
				CLR_BIT_Status(StatBit_FsBefEin);
				}
			BusEmpfMarkwechsel = false;
			}

		// vom Modem empfangenen Pegel an Endgerät weitergeben
		if (ReceiveMark())
			{
			clr_LEDBLAU();
			if (!AltEmpfMark && BusAuftrag == Nichts)
				{
				BusSenden(BusKdoMark);
				SET_BIT_Status(StatBit_FsMeldEin);
				}
			} // if ReceiveMark()
		else
			{
			set_LEDBLAU();
			if (AltEmpfMark && BusAuftrag == Nichts)
				{
				BusSenden(BusKdoSpace);
				CLR_BIT_Status(StatBit_FsMeldEin);
				}
			} // else !ReceiveMark()
			
		// Wiederholungssendung des Pegels?
		if (BusAuftrag == Nichts && TimerVal(&PegelWdhTimer) >= (PegelSchnellWdh ? 4 : 652))
			{
			BusSenden(AltEmpfMark ? BusKdoMarkWdh : BusKdoSpaceWdh);
			}
			
		// Gesendete (über I²C) Daten angekommen?
		if (BusAuftrag == Fertig)
			{
			if (BusErgebnis == Ok)
				{
				switch (BusSendeDaten)
					{
					case BusKdoMark:
						AltEmpfMark = true;
						PegelSchnellWdh = true;
						break;
						
					case BusKdoSpace:
						AltEmpfMark = false;
						PegelSchnellWdh = true;
						break;
						
					case BusKdoMarkWdh:
						AltEmpfMark = true;
						PegelSchnellWdh = false;
						break;
						
					case BusKdoSpaceWdh:
						AltEmpfMark = false;
						PegelSchnellWdh = false;
						break;
					} // switch (BusSendeDaten)
					
				if (TelegrammFehlerZaehler > 0)
					TelegrammFehlerZaehler--;
					
				} // if BusErgebnis == Ok
			else
				{
				TelegrammFehlerZaehler++;
				if (TelegrammFehlerZaehler > 5)
					{ // Abbruch wegen schlechter interner Verbindung
					set_LEDROT();
					BusSenden(BusKdoSchluss);
					WarteSchlussQuittung(2500);
					Grundstellen(false);
					return; // Träger ist weg...
					}
				}
			StartTimer(&PegelWdhTimer);
			BusAuftrag = Nichts;
			} // if BusAuftrag == Fertig

		// Träger noch ok?
		if (TimerVal(&TraegerPruefTimer) >= 20) // alle 20 Millisekunden prüfen
			{
			if (BIT_IS_SET(GetState(false), STATEBIT_CARRIER))
				{ // Träger ist ok
				if (BIT_IS_SET(Status, StatBit_AngerufenBelegt))
					set_LEDGRUEN();
				else
					set_LEDGELB();
				if (TraegerFehlZaehler > 0)
					TraegerFehlZaehler--;
				}
			else
				{ // Träger ist weg
				if (BIT_IS_SET(Status, StatBit_AngerufenBelegt))
					clr_LEDGRUEN();
				else
					clr_LEDGELB();
				if (++TraegerFehlZaehler > 1500 / 20)
					return; // Träger ist weg...
				}
			StartTimer(&TraegerPruefTimer);
			} // if TimerVal(&TraegerPruefTimer) >= 20
	
		if (TimerVal(&LongDistancePruefTimer) > 250)
			{
			if (BIT_IS_SET(GetState(true), STATEBIT_LONGLOOP))
				set_LEDROT();
			else
				clr_LEDROT();
			StartTimer(&LongDistancePruefTimer);
			}
			
		} // while true
	} // VerbindungHergestellt
	

static void TasteFunktion()
	{
	bool Lange;

	CLR_BIT_Status(StatBit_Frei); // im Hauptprogramm macht Grundstellen wieder "Frei"
	CLR_BIT_Status(StatBit_LeitungKennung);
	
	set_LEDROT();
	Lange = WarteTaste();
	clr_LEDROT();
	if (Lange)
		{
		/* TestAnruf("05314287741"); */
		return;
		}

	set_LEDGELB();
	Lange = WarteTaste();
	clr_LEDGELB();
	if (Lange)
		{
		/* TestAnruf("05312502174"); */
		return;
		}

	set_LEDGRUEN();
	Lange = WarteTaste();
	clr_LEDGRUEN();
	if (Lange)
		{
		/* VerbindungKommendSimulieren(); //*/
		return;
		}

	set_LEDBLAU();
	Lange = WarteTaste();
	clr_LEDBLAU();
	if (Lange)
		{
		/* DetectionTest(true, "05314287741"); //*/
		return;
		}
	}


// Konfiguration über angeschlossenes Endgerät...
// -------------------------------------------


void FernDialogCallback()
	{
	bset_LEDGELB(!BIT_IS_SET(Status, StatBit_FsBefEin));
	bset_LEDBLAU(!BIT_IS_SET(Status, StatBit_FsMeldEin));
	}


static uint8_t NeuEigenAdresse;	
	

static bool AmtswahlAbfrage()
	{
	uint8_t AktAmtswahl, AktAmtswahlZiffern;
	AktAmtswahl = AdresseZuWahl(BusEigenAdresse, &AktAmtswahlZiffern);
	
	while (true)
		{ // solange Durchwahl abfragen, bis gültige Eingabe erfolgt
		uint8_t neu = AktAmtswahl;
		if (!ZahlAbfrageFern(PSTR("amtswahl"), &neu, AktAmtswahlZiffern))
			return false; // Abbruch
			
		if (ZahlEmpfangenFernAnzahlZiffern > 2) 
			{
			if (!TextAusgabeFern(PSTR("\r\n maximal 2 stellen")))
				return false; // Abbruch
			continue; // nochmal;
			}
			
		if (ZahlEmpfangenFernAnzahlZiffern > 0)
			NeuEigenAdresse = WahlZuAdresse(neu, ZahlEmpfangenFernAnzahlZiffern);
		else
			{
			NeuEigenAdresse = BusEigenAdresse;
			ZahlEmpfangenFernAnzahlZiffern = AktAmtswahlZiffern;
			}
		
		if (!TextAusgabeFern(PSTR("\r\n pruefe: "))
			|| !ZahlAusgabeFern(neu, ZahlEmpfangenFernAnzahlZiffern))
			return false;

		if (GetStatus(NeuEigenAdresse) < 0)
			{
			return TextAusgabeFern(PSTR(" ok. "));
			}

		// Adresse schon belegt...
		if (!TextAusgabeFern(PSTR(" schon vergeben, andere waehlen!")))
			return false; // Abbruch

		}
		
	}


static bool DurchwahlenAbfrage()
	{
	if (!TextAusgabeFern(PSTR("\r\n nebenstellen fuer durchwahlziffer...")))
		return false;
		
	for (uint8_t i = 1 ; i <= 7 ; i += 3)
		{
		if (!TextAusgabeFern(PSTR("\r\n ... ist"))
			|| !CodeAusgabeFern(TtyCodeZiUm))
			return false;

		for (uint8_t j = i ; j <= i+2 ; j++)
			if (!CodeAusgabeFern(ZeichenZuCode(' ', true)) // true = ModeZiffern
				|| !ZahlAusgabeFern(j, 1)
				|| !CodeAusgabeFern(ZeichenZuCode('=', true)) // true = ModeZiffern
				|| !ZahlAusgabeFern(NebenstellenTabelle[j], 2))
				return false;

		if (!TextAusgabeFern(PSTR("  neu:    "))
			|| !CodeAusgabeFern(TtyCodeZiUm))
			return false;
			
		for (uint8_t j = i ; j <= i+2 ; j++)
			{
			if (!CodeAusgabeFern(ZeichenZuCode(' ', true)) // true = ModeZiffern
				|| !ZahlAusgabeFern(j, 1)
				|| !CodeAusgabeFern(ZeichenZuCode('=', true)) // true = ModeZiffern
				|| !ZahlEmpfangenFern(&NebenstellenTabelle[j]))
				return false;
			// Bereichs-Check erfolgt in der Funktion Anruf kommend
			}
		}
	return true;
	}
	
	
static bool JustierWahlziffernAbfragen()
	{
	uint8_t i;
	
	if (!TextAusgabeFern(PSTR("\r\n wahlziffern fuer justierung. mit + liste beenden.\r\n ist = ")))
		return false;	
	for (i = 0 ; i < AnzJustierWahlziffern ; i++)
		{
		if (JustierWahlziffern[i] >= 10)
			break;
		if (!CodeAusgabeFern(ZeichenZuCode('0' + JustierWahlziffern[i], true)))
			return false;
		}
	if (!TextAusgabeFern(PSTR("+  neu = "))
		|| !CodeAusgabeFern(TtyCodeZiUm))
		return false;

	bool ModeZiffern = true;
	char c;
	i = 0;
	while (i < AnzJustierWahlziffern)
		{
		if (!ZeichenEmpfangenFern(&c, &ModeZiffern))
			return false;
		switch (c)
			{
			case '0' ... '9': 
				JustierWahlziffern[i++] = c - '0'; 
				break;
			case '+':
			case 'z': 		  
				JustierWahlziffern[i++] = 99; 
				i = AnzJustierWahlziffern; // für schleifenabbruch
				break;
			case '.':
			case '-':
			case ':':
			case '/':
				i = AnzJustierWahlziffern; // für schleifenabbruch
				break;
			}
		}
	return true;
	}
	
	
static void Einstellen()
// im Hauptprogramm kommt danach Grundstellen()
	{
	if (!FernDialogVerbinden(Hauptanschluss))
		{
		PruefeBusSchluss(0, 7);
		Grundstellen(false);
		return;
		}

	NeuEigenAdresse = BusEigenAdresse;

	set_LEDGRUEN();			

	if (TextAusgabeFern(PSTR("\r\n konfiguration leitungsschnittstelle analog version " SVNVERSION " datum " __DATE__))
		&& ZahlAbfrageFern(PSTR("anzahl klingelzeichen bis annahme"), &AnnahmeKlingelzeichen, 1)
		&& AmtswahlAbfrage()
		&& BitAbfrageFern(PSTR("feste hauptstelle"), &KonfigBits, 1 << KonfigBit_FesterHauptanschluss)
		&& (!BIT_IS_SET(KonfigBits, KonfigBit_FesterHauptanschluss) // folgende Abfrage nur bei FesterHauptanschluss
		    || ZahlAbfrageFern(PSTR("nummer der hauptstelle"), &Hauptanschluss, 2))
		&& BitAbfrageFern(PSTR("alternativ-suche bei besetzt"), &KonfigBits, 1 << KonfigBit_SucheAlternativBeiBesetzt)
		&& BitAbfrageFern(PSTR("kommende durchwahl zulassen"), &KonfigBits, 1 << KonfigBit_DurchwahlErlaubt)
		&& (!BIT_IS_SET(KonfigBits, KonfigBit_DurchwahlErlaubt) // folgende Abfrage nur bei nicht gesperrter Durchwahl
		    || DurchwahlenAbfrage())
		&& ZahlAbfrageFern(PSTR("verzoegerung letzte ziffer - beginn kennton ...\r\n ... (x/10 sek)"), &VerbindungsaufbauVerzoegerung, 1)
		&& JustierWahlziffernAbfragen()
		&& ZahlAbfrageFern(PSTR("justierung verzoegerung abheben - erste ziffer ...\r\n ... (x/10 sek)"), &JustierWahlVerzoegerung, 1)
		&& ZahlAbfrageFern(PSTR("justierung verzoegerung auflegen - abheben nach taste ...\r\n ... (x/10 sek)"), &JustierNeustartPause, 1)
		&& TextAusgabeFern(PSTR("\r\n fertig +++\r\n")))
		{ // kein Abbruch, daher ordnungsgemäß abstellen
		BusSenden(BusKdoSchluss);
		WarteSchlussQuittung(2500);
		Grundstellen(false);
		}
	else
		{ // es wurde ein Kommando empfangen, welches nicht Mark oder Space befahl... Abbruch?
		PruefeBusSchluss(0, 7); 
		}

	BusEigenAdressePruefenUndSetzen(NeuEigenAdresse);
	
	set_LEDROT();
	set_LEDGELB();

	// Variablen in EEPROM speichern
	wdt_reset();
	eeprom_write_byte(&KonfigBits_EE, KonfigBits);
	eeprom_write_byte(&AnnahmeKlingelzeichen_EE, AnnahmeKlingelzeichen);
	for (uint8_t i = 0 ; i < AnzJustierWahlziffern ; i++)
		eeprom_write_byte(&JustierWahlziffern_EE[i], JustierWahlziffern[i]);
	wdt_reset();
	eeprom_write_byte(&JustierWahlVerzoegerung_EE, JustierWahlVerzoegerung);
	eeprom_write_byte(&JustierNeustartPause_EE, JustierNeustartPause);
	eeprom_write_byte(&VerbindungsaufbauVerzoegerung_EE, VerbindungsaufbauVerzoegerung); 
	for (uint8_t i = 0 ; i < 10 ; i++)
		eeprom_write_byte(&NebenstellenTabelle_EE[i], NebenstellenTabelle[i]);
	eeprom_write_byte(&BusEigenAdresse_EE, BusEigenAdresse);

	wdt_reset();

	}

	
static void Justieren()
	{
	TMsTimer Timer;

	set_LEDROT();
	while (true)
		{
		Tastendruck = NichtGedr;
		set_GABEL();
		set_TELAUS();
		
		StartTimer(&Timer);
		while (TimerVal(&Timer) < JustierWahlVerzoegerung * 100)
			wdt_reset();
			
		set_LEDBLAU();
		
		for (uint8_t i = 0 ; i < AnzJustierWahlziffern ; i++)
			if (JustierWahlziffern[i] >= 10)
				break;
			else
				ZifferWaehlen(JustierWahlziffern[i]);
				
		clr_LEDBLAU();
		TastePruefen();
		while (Tastendruck == NichtGedr)
			{
			StartV21(true);
			Transmit(false); // ja, Space-Ton!
			StartTimer(&Timer);
			set_LEDGELB();
			while (TimerVal(&Timer) < 1000 && Tastendruck == NichtGedr)
				{
				wdt_reset();
				TastePruefen();
				}
			clr_LEDGELB();

			StartV21(false);
			Transmit(true); // jetzt aber Mark...
			StartTimer(&Timer);
			set_LEDGRUEN();
			while (TimerVal(&Timer) < 1000 && Tastendruck == NichtGedr)
				{
				wdt_reset();
				TastePruefen();
				}
			StopV21();
			clr_LEDGRUEN();

			StartTimer(&Timer);
			while (TimerVal(&Timer) < 300 && Tastendruck == NichtGedr)
				{
				wdt_reset();
				TastePruefen();
				}
			} // while (Tastendruck == NichtGedr
		if (Tastendruck == Lang)
			{
			Tastendruck = NichtGedr;
			return; // Grundstellen macht das aufrufende Programm
			}
		clr_GABEL(); // TELAUS bleibt aktiv!
		StartTimer(&Timer);
		while (TimerVal(&Timer) < JustierNeustartPause * 100)
			{
			wdt_reset();
			}
		
		// Tastendruck = NichtGedr am Anfang der Schleife
		} // while (true)
	}
	
	
static void HauptEinstellungen()
// wechselt je nach Tastendruck kurz oder lang in andere Funktionen:
// kurz = Konfiguration, Lang = Justierung, Anruf = Justierung
// im Hauptprogramm kommt Grundstellen(false)
	{
	set_LEDROT();

	Tastendruck = NichtGedr;

	CLR_BIT_Status(StatBit_Frei); // im Hauptprogramm macht Grundstellen wieder "Frei"
	CLR_BIT_Status(StatBit_LeitungKennung);

	DiagDatenLoeschen();
	
	while (true)
		{
		wdt_reset();
		TastePruefen();
		if (Tastendruck == Kurz)
			{
			Tastendruck = NichtGedr;
			Einstellen();
			return;
			}
			
		if (Tastendruck == Lang || !get_ANRUF()) // get_ANRUF ist aktiv low
			{
			Tastendruck = NichtGedr;
			Justieren();
			return;
			}
		
		}
	}

	
// Deaktivierung: Schnittstelle kann nicht mehr angesprochen werden
// ================================================================

// TODO offen


// Hauptprogramm
// =============


int main()
	{
	// Watchdog initialisieren
#ifndef NOWATCHDOG
	wdt_reset();
	wdt_enable(WDTO_2S);
#endif //NOWATCHDOG

	// PORTS initialisieren 
	init_TASTE();
	init_LEDROT(); set_LEDROT();
	init_LEDGELB(); clr_LEDGELB();
	init_LEDGRUEN(); clr_LEDGRUEN();
	init_LEDBLAU(); clr_LEDBLAU();
	init_GABEL();
	init_ANRUF();
	init_TELAKTIV();
	
	init_TASTE();

	// Variablen aus EEPROM initialisieren
	BusEigenAdresse = eeprom_read_byte(&BusEigenAdresse_EE) & 0xFE;
	BusEigenAdrMehrfach = 1;
	
	KonfigBits = eeprom_read_byte(&KonfigBits_EE);
	AnnahmeKlingelzeichen = eeprom_read_byte(&AnnahmeKlingelzeichen_EE);
	for (uint8_t i = 0 ; i < AnzJustierWahlziffern ; i++)
		JustierWahlziffern[i] = eeprom_read_byte(&JustierWahlziffern_EE[i]);
	JustierWahlVerzoegerung = eeprom_read_byte(&JustierWahlVerzoegerung_EE);
	JustierNeustartPause = eeprom_read_byte(&JustierNeustartPause_EE);
	VerbindungsaufbauVerzoegerung = eeprom_read_byte(&VerbindungsaufbauVerzoegerung_EE); 
	for (uint8_t i = 0 ; i < 10 ; i++)
		NebenstellenTabelle[i] = eeprom_read_byte(&NebenstellenTabelle_EE[i]);

	// Module initialisieren
	MsTimerInit();
	SeriellUmsetzInit();
	ModemInit();
	// TWI erst nach 0,25 Sek. initialisieren!
	
	Status = (1 << StatBit_Frei) | (1 << StatBit_LeitungKennung);

	TMsTimer Timer;
	StartTimer(&Timer);

	sei();

	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 250)
		;

	clr_LEDROT();
	set_LEDGELB();

	wdt_reset();
	
	ModemReset();

	TwiInit();

	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 500)
		;

	clr_LEDGELB();
	set_LEDGRUEN();
	wdt_reset();
	

	// 0,25 Sek. warten
	while (TimerVal(&Timer) < 750)
		;

	clr_LEDGRUEN();
	set_LEDBLAU();
	wdt_reset();
	
	// TWI nochmal resetten
	TWCR = (1<<TWINT) | (0<<TWEA) | (0<<TWSTA) | (1<<TWSTO) | (0<<TWEN) | (0<<TWIE);

	while (TimerVal(&Timer) < 760)
		;
		
	TWCR = (1<<TWINT) | (1<<TWEA) | (0<<TWSTA) | (0<<TWSTO) | (1<<TWEN) | (1<<TWIE);
	wdt_reset();

	while (TimerVal(&Timer) < 1000 + BusEigenAdresse)
		;

	// Busadresse prüfen
	BusEigenAdressePruefenUndSetzen(BusEigenAdresse);

	clr_LEDBLAU();

	//static bool ResetFlag = true; // HACK

	while (true)
		{ // Hauptschleife
		wdt_reset();

		// rot blinkende LED wenn Busadresse ungültig ist...
		if (TimerVal(&Timer) > 200)
			{
			clr_LEDROT();
			clr_LEDGELB();
			clr_LEDGRUEN();
			clr_LEDBLAU();
			}
		else
			{
			bset_LEDROT(BusEigenAdresse == BusAdrUngueltig);
			//bset_LEDBLAU(ResetFlag);
			}

		if (TimerVal(&Timer) > 1600)
			StartTimer(&Timer);

		TastePruefen();
		if (Tastendruck == Kurz)
			{
			//ResetFlag = false;
			Tastendruck = NichtGedr;
			TasteFunktion();
			Grundstellen(false);
			}
		else if (Tastendruck == Lang)
			{
			//ResetFlag = false;
			Tastendruck = NichtGedr;
			HauptEinstellungen();
			Grundstellen(false);
			}

		if (!get_ANRUF() || !get_TELAKTIV())
			{ // beide Signale sind Aktiv LOW
			if (Anruferkennung())
				{
				VerbindungKommend();
				}
			Grundstellen(false);
			} // if Anruf oder Telefon aktiv
			
		if (!EmpfPufferLeer()) // auf TWI-Bus empfangenes Kommando
			{
			VerbindungGehend();
			Grundstellen(false);
			}

		} // Hauptschleife endlos
	} // main
	
	
