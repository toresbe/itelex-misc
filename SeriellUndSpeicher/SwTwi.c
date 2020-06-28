// das Ganze nur, wenn es nicht abgeschaltet ist

//! \todo DOXYGEN noch nicht ganz fertig (Debugging fehlt)

#ifdef AF_ANRUFSPEICHER

#include <inttypes.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdbool.h>

#include "bits.h"

#include "BaudotCode.h"
#include "MsTimer.h"

#include "Ports.h"

#include "SwTwi.h"


// Schalter für Code-Varianten
// ===========================

//#define EXTEEPROM_DEBUG
	//!< Wenn definiert, kann interaktiv das Externe EEPROM ausgelesen werden.

//#define SWTWI_SHOWMSG
	// !< wenn definiert, wird der Eeprom-Zugriff protokolliert.


// Ausgabe-Funktionen für's Debuggen
// =================================

extern void LokalZahlAusgabe16(uint16_t i, int8_t minzif);
extern void LokalZahlAusgabe(uint8_t i, int8_t minzif);
extern void LokalZeichenAusgabe(char c);
extern void LokalHexAusgabe(uint8_t i);
extern void SerSendFlush();

extern void FehlerStop(uint8_t Code); // aus SeriellUndSpeicher.c


// ####################################################################################
// Software-TWI-Code
// =================
// Allgemein für alle Arten von TWI-Bausteinen
// ####################################################################################

#ifdef SWTWI_DEBUG 

// nur zur Verwendung im Sowftware-Debugger!
// =========================================

#define SDA_0 CLR_BIT(PORTB, 7)	
#define SDA_1 SET_BIT(PORTB, 7)
#define SDA_in BIT_IS_SET(PINB, 7)

#define SCL_0 CLR_BIT(PORTB, 6)
#define SCL_1 SET_BIT(PORTB, 6)
#define SCL_in BIT_IS_SET(PINB, 6)

#else //SWTWI_DEBUG

/* ALT OHNE externe Pullup-Widerstände

#define TWI_0(p,b) do { (p ## _OPORT) &= ~(1<<(b)); (p ## _DDR) |= (1<<(b)); } while (0)
#define TWI_1(p,b) do { (p ## _DDR) &= ~(1<<(b)); (p ## _OPORT) |= (1<<(b)); } while (0)

*/

#define SDA_0 clro_SWTWI_SDA()
#define SDA_1 inp_SWTWI_SDA()
#define SDA_in get_SWTWI_SDA()

#define SCL_0 clro_SWTWI_SCL()
#define SCL_1 inp_SWTWI_SCL()
#define SCL_in get_SWTWI_SCL()

#endif //SWTWI_DEBUG


//! Variablen für Software I²C-Bus
// ------------------------------
//!	nur ein Master, nur Master-Mode

typedef struct 
	{
	uint8_t Phase; //!< 0 = untätig, 1-254 = Transfer läuft, 255 = Transfer abgeschlossen
				   //!< Wechsel von 255 auf 0 muß durch Applikation erfolgen, vorher aber AnzDaten auf 0 setzen
	uint8_t Adresse; //!< TWI-Adresse des Slave
	uint8_t *Puffer; //!< Zeiger auf bereitgestellten / gefüllten Datenpuffer.
	uint8_t AnzDaten; //!< Soll-Anzahl Daten-Bytes, bei >= 1 geht es los. 
					  //!< 1 = Adresse + 1 Daten-Byte! 
	uint8_t Ergebnis; //!< Ist-Anzahl übertragene Bytes; ideal: AnzDaten + 1 (1 für Adresse)
	// intern:
	uint8_t AktByte; //!< Aktuell zu übertragenes Daten-Byte (ggf. auch Adresse)
	uint8_t BitNr; //!< Aktuell gesendetes / empfangenes Bit.
	uint8_t ByteNr; //!< wandert durch den Puffer
	} T_SwTwiTransferdaten;

// gültige "Zustände":
// Phase	AnzDaten	Beschreibung
//	0			0		nichts zu tun
//  0		   >=1		Transfer kann gestartet werden
// 1-254		x		Transfer läuft
//	255			x		Transfer abgeschlossen
			

#ifdef EXTEEPROM_SIMULATION 

#define XEEPROM_SIZE 256U //!< Für interne Simulation des externen EEPROM.

uint16_t XeeSimAdr = 0;

uint8_t XeeSimBuf[XEEPROM_SIZE];


void XeeSimAktion(volatile T_SwTwiTransferdaten *p)
	{
	if (p->Phase == 0)
		{
		if (p->AnzDaten > 0)
			{ // los geht's
			uint8_t i; // Kopier-Zeiger
			// TODO: Prüfen, ob EEPROM angesprochen
			if ((p->Adresse & 1) == 0) // Bit 0 in Adresse = 0 -> Write, = 1 -> Read
				{ // schreiben
				XeeSimAdr = (p->Puffer[0] << 8) + p->Puffer[1];
				for (i = 0 ; i < p->AnzDaten - 2 ; i++)
					XeeSimBuf[XeeSimAdr + i] = p->Puffer[i + 2];
				}
			else
				{ // lesen
				for (i = 0 ; i < p->AnzDaten ; i++)
					p->Puffer[i] = XeeSimBuf[XeeSimAdr + i];
				}
			p->Phase = 1;
			p->Ergebnis = 0;
			p->ByteNr = p->AnzDaten;
			} // p->AnzDaten > 0
		else
			return; // nichts zu tun!
		}
	else if (p->Phase == 255)
		return; // letzter Transfer fertig!
	else if (p->Phase > 10)
		{
		p->Ergebnis++;
		if (p->Ergebnis >= p->AnzDaten + 1)
			{
			p->Phase = 255;
			Stop
			}
		else
			p->Phase = 1;
		}
	else // p->Phase zwischen 1 und 10
		p->Phase++;
	}
	

	...das funktioniert eh nicht mehr so...
	
ISR(TIMER2_COMPA_vect)
	{
	XeeSimAktion(&EepromTwi);
	}


#else //!def EXTEEPROM_SIMULATION


#define BitDebugOut(x)



//! regelmäßig aufrufen für Software-TWI.
//------------------------------------------
//! maximale TWI-Frequenz = halbe Aufruf-Frequenz
void SwTwiAktion(volatile T_SwTwiTransferdaten *p)
	{
	switch (p->Phase)
		{
		case 0: // Bei Start: SDA auf 0
			if (p->AnzDaten == 0)
				break; // nix zu tun
			p->ByteNr = 0;
			p->BitNr = 7;
			p->AktByte = p->Adresse;
			SDA_0;
			p->Phase = 1;
			break;
		case 1: // Start 1: SDA 0 überprüfen
			if (!SDA_in)
				p->Phase = 2;
			break;
		case 2: // Start 2: SCL auf 0
			SCL_0;
			p->Phase = 3;
			break;
		case 3: // Start 3: SCL 0 überprüfen
			if (!SCL_in)
				p->Phase = 10;
			break;

		case 10: // Datenbit Ausgabe 1: SDA setzen
			if (p->AktByte & (1<<p->BitNr))
				{
				SDA_1;
				BitDebugOut('1');
				}
			else
				{
				SDA_0;
				BitDebugOut('0');
				}
			p->Phase = 11;
			break;
			
		case 11: // Datenbit Ausgabe 2: SDA prüfen, SCL auf 1
			if (p->AktByte & (1<<p->BitNr))
				{
				if (!SDA_in) break;
				}
			else
				{
				if (SDA_in) break;
				}
			SCL_1;
			p->Phase = 12;
			break;
			
		case 12: // Datenbit Ausgabe 3: SCL 1 prüfen
			if (SCL_in)
				p->Phase = 13;
			break;
			
		case 13: // Datenbit Ausgabe 4: SCL auf 0
			SCL_0;
			p->Phase = 14;
			break;
			
		case 14: // Datenbit Ausgabe 5: SCL 0 prüfen
			if (SCL_in)
				break;
			if (p->BitNr == 0)
				{ // gerade wurde Bit0 gesendet, jetzt ACK prüfen
				p->Phase = 20;
				}
			else
				{
				p->BitNr--;
				p->Phase = 10;
				}
			break;
			
		case 20: // ACK Empfang 1: SDA und SCL auf 1
			SDA_1;
			SCL_1;
			p->Phase = 21;
			break;
			
		case 21: // ACK Empfang 2: SCL prüfen
			if (SCL_in)
				p->Phase = 22;
			break;
			
		case 22: // ACK Empfang 3: SDA auswerten, SCL auf 0
			if (SDA_in) // DEBUGBREAK beim Debuggen PINB(1) auf 0
				{ // kein ACK empfangen
				p->Phase = 50;
				BitDebugOut('N');
				}
			else
				{ // ACK empfangen
				p->AktByte = p->Puffer[p->ByteNr]; // Vorbereitend, falls weiter gesendet wird
				p->ByteNr++;
				p->Ergebnis = p->ByteNr;
				p->BitNr = 7;
				p->Phase = 23;
				BitDebugOut('A');
				}
			SCL_0;
			break;

		case 23: // ACK Empfang 5: SCL 0 prüfen
			if (SCL_in)
				break;
			if (p->ByteNr > p->AnzDaten)
				p->Phase = 50;
			else 
				if ((p->Adresse & 1) == 0) // Bit 0 in Adresse = 0 -> Write, = 1 -> Read
					p->Phase = 10;
				else
					p->Phase = 30;
			break;

		case 30: // Datenbit Empfang 1: SCL auf 1
			if (p->BitNr == 7)
				p->AktByte = 0;
			SDA_1;
			SCL_1;
			p->Phase = 31;
			break;
			
		case 31: // Datenbit Empfang 2: SCL 1 prüfen
			if (SCL_in)
				p->Phase = 32;
			break;
			
		case 32: // Datenbit Empfang 3: SDA auswerten, SCL auf 0
			if (SDA_in) // DEBUGBREAK beim Debuggen PINB(1) auf 1 oder 0
				{ 
				p->AktByte |= (1<<p->BitNr);
				BitDebugOut('+');
				}
			else
				{
				BitDebugOut('-');
				}
			SCL_0;
			p->Phase = 33;
			break;
		
		case 33: // Datenbit Empfang 4: SCL 0 prüfen
			if (SCL_in)
				break;
			if (p->BitNr == 0)
				{ // gerade wurde Bit0 empfangen, jetzt ACK senden
				p->Phase = 40;
				}
			else
				{
				p->BitNr--;
				p->Phase = 30;
				}
			break;
			
		case 40: // ACK Ausgabe 1: SDA auf 0, außer bei letztem Byte
			p->Puffer[p->ByteNr - 1] = p->AktByte; // Byte-Nr steht auf 1 beim ersten Byte
			if (p->ByteNr < p->AnzDaten) 
				{
				SDA_0;
				BitDebugOut('a');
				}
			else
				{
				SDA_1;
				BitDebugOut('n');
				}
			p->Phase = 41;
			break;
			 
		case 41: // ACK Ausgabe 2: SDA prüfen, SCL auf 1
			if (p->ByteNr < p->AnzDaten) 
				{ // SDA soll 0 sein
				if (SDA_in) break;
				}
			else
				{
				if (!SDA_in) break;
				}
			SCL_1;
			p->Phase = 42;
			break;
			
		case 42: // ACK Ausgabe 3: SCL 1 prüfen
			if (SCL_in)
				p->Phase = 43;
			break;
			
		case 43: // ACK Ausgabe 4: SCL auf 0
			SCL_0;
			p->Phase = 44;
			break;
			
		case 44: // ACK Ausgabe 5: SCL 0 prüfen
			if (SCL_in)
				break;
			SDA_1; // auf 1 zu prüfen ist sinnlos, da 0 vom slave gesendet werden könnte
			p->ByteNr++;
			p->BitNr = 7;
			p->Ergebnis = p->ByteNr;
			if (p->ByteNr > p->AnzDaten)
				{
				p->Phase = 50;
				}
			else
				{
				p->Phase = 30;
				}
			break;
		
		case 50: // Stop 1: SDA auf 0
			SDA_0;
			p->Phase = 51;
			break;
			
		case 51: // Stop 2: SDA 0 prüfen
			if (!SDA_in)
				p->Phase = 52;
			break;
			
		case 52: // Stop 3: SCL auf 1
			SCL_1;
			p->Phase = 53;
			break;
			
		case 53: // Stop 4: SCL 1 prüfen
			if (SCL_in)
				p->Phase = 54;
			break;
			
		case 54: // Stop 5: SDA auf 1
			SDA_1;
			p->Phase = 55;
			break;
			
		case 55: // Stop 6: SDA 1 prüfen
			if (SDA_in)
				{
				// void StopSwTwi();

				p->Phase = 255;
				// StopSwTwi();
				}
			break;
			
		}
	}


#endif //else !def EXTEEPROM_SIMULATION


// extern void StartSwTwi();

// extern void StopSwTwi();

extern void DoSwTwi();



// EEPROM-Zugriff über SwTWI
// =========================

volatile T_SwTwiTransferdaten EepromTwi; //!< Die Arbeitsdaten für einen Software-TWI-Bus


//! Extern regelmäßig aufzurufende Funktion für den Ablauf des Softwarebasierten TWI-Masters.
//! Funktion kehrt sehr schnell zurück. (nur ein Bit je Aufruf).
void DoSwTwi()
	{
	SwTwiAktion(&EepromTwi);
	}


// ####################################################################################
// Zugriff auf EEPROM mit TWI-Schnittstelle über Software-TWI
// ####################################################################################

	

// Daten für Interface zum Externen Eeprom
// ---------------------------------------

#define XEEPROM_SIZE 32768U //!< Größe des externen EEPROM

#define XEEPROM_PUFFER_MAX 32 //!< Zwischengespeicherte Daten
//#define XEEPROM_PUFFER_MAX 8
	// muss 2er Potenz sein

#define XEEPROM_TWI_ADR 0xA0 //!< TWI-Adresse des EEPROM.

typedef struct
	{
	uint16_t StartAdr; //!< Start-Adresse im EEPROM
	uint8_t TransfBuf[2+XEEPROM_PUFFER_MAX]; //!< Datenpuffer
	uint8_t SchreibAnz; //!< Anzahl beschriebener Byte
	bool Gesperrt; //!< wird gerade gelesen oder geschrieben
	} TEeZwischenPuffer; //!< Daten für EEPROM-Zugriff.

enum { Frei, Auslesen, Schreiben } EeMode; //!< Art des EEPROM-Zugriffs

// gültige Zustände:
// EeMode		EZP.Gesperrt	Bedeutung
//	Frei		  x				Puffer frei, Inhalt zufällig
//	Auslesen	true			Puffer wird vom EEPROM gefüllt, nicht auslesen
//	Auslesen	false			Puffer ist fertig gefüllt, auslesen erlaubt
//	Schreiben	false			Puffer wird von Applikation gefüllt, noch nicht in EEPROM kopiert
//	Schreiben	true			Puffer wird in das EEPROM übertragen, nicht mehr beschreiben


TEeZwischenPuffer EZP; //!< Zwischen-Puffer für EEPROM-Zugriff.
TEeZwischenPuffer HilfEZP; //!< Hilfs-Zwischen-Puffer für EEPROM-Zugriff bei gleichzeitigem Lesen und Schreiben.


//! Initialisierung des EEPROM-Zugriffs.
void EeInit()
	{
	init_SWTWI_SDA();
	init_SWTWI_SCL();

	EeMode = Frei;
	EZP.SchreibAnz = 0;
	EZP.Gesperrt = false;
	HilfEZP.SchreibAnz = 0;
	HilfEZP.Gesperrt = false;
	for (uint8_t i = 0 ; i < 2 + XEEPROM_PUFFER_MAX ; i++)
		{
		EZP.TransfBuf[i] = 0x55;
		HilfEZP.TransfBuf[i] = 0x77;
		}
	}


//! Abbruch eines EEPROM-Zugriffs.
void EeAbbruch()
	{
	EeInit();
	}


//! Errechnet Anfang eines Blockes im EEPROM zu einer gegebenen Adresse.
static inline uint16_t EeBlockAnf(uint16_t Adresse) // Blockanfang
	{
	return (Adresse & ~(XEEPROM_PUFFER_MAX - 1));
	}

	
//! Beschränkt eine EEPROM-Speicheradresse auf den erlaubten Bereich.
static inline uint16_t EeAdrNorm(uint16_t adr)
	{
	return adr & (XEEPROM_SIZE - 1);
	}


//! Prüft, ob laufender EEPROM-Zugriff abgeschlossen ist.	
static void EeTransferPruefen()
	{
	if (EepromTwi.Phase != 255)
		return; // es gibt noch nichts zu prüfen...
		
#ifdef SWTWI_SHOWMSG
	LokalZeichenAusgabe('E');
	LokalZeichenAusgabe('=');
	LokalHexAusgabe(EepromTwi.Ergebnis);
	LokalZeichenAusgabe(' ');
	SerSendFlush();
#endif //SWTWI_SHOWMSG

	if (EepromTwi.Ergebnis != EepromTwi.AnzDaten + 1)
		{
#ifdef SWTWI_SHOWMSG
		LokalZeichenAusgabe('R');
		LokalZeichenAusgabe(' ');
		SerSendFlush();
#endif //SWTWI_SHOWMSG
		EepromTwi.Phase = 0;
		return;
		}

	EepromTwi.AnzDaten = 0;

	if (EeMode == Auslesen)
		{ // Lese-Transfer ... abgeschlossen?
		if ((EepromTwi.Adresse & 1) == 0) // Bit 0 in Adresse = 0 -> Write, = 1 -> Read
			{ // es ist erst der Adressen-Initialisierungsvorgang abgeschlossen, Lesevorgang starten...
			uint8_t SregAlt = SREG;
			cli();
			EepromTwi.Adresse = XEEPROM_TWI_ADR + 1; // Lesen
			EepromTwi.Puffer = &EZP.TransfBuf[2];
			EepromTwi.AnzDaten = XEEPROM_PUFFER_MAX;
			SREG = SregAlt;
#ifdef SWTWI_SHOWMSG
			LokalZeichenAusgabe('S');
			LokalZeichenAusgabe('=');
			LokalHexAusgabe(EepromTwi.Adresse);
			LokalZeichenAusgabe(':');
			LokalHexAusgabe(EepromTwi.AnzDaten);
			LokalZeichenAusgabe(' ');
			SerSendFlush();
#endif //SWTWI_SHOWMSG
			} // if (EepromTwi.Adresse & 1) == 0
		else // (EepromTwi.Adresse & 1) == 1 --> das war der Lesevorgang				
			EZP.Gesperrt = false;
			// EepromTwi.Anzdaten wurde oben schon auf 0 gesetzt
		}
	else if (EeMode == Schreiben)
		{
		EZP.SchreibAnz = 0;
		if (HilfEZP.SchreibAnz > 0)
			{ // gleich den Hilfspuffer ausgeben
			uint8_t i;
			uint8_t SregAlt = SREG;
			cli();
			EZP.StartAdr = HilfEZP.StartAdr;
			for (i = 2 ; i < 2 + HilfEZP.SchreibAnz ; i++)
				EZP.TransfBuf[i] = HilfEZP.TransfBuf[i];
			EZP.SchreibAnz = HilfEZP.SchreibAnz; 
			HilfEZP.SchreibAnz = 0;
			EZP.Gesperrt = false; // wird gleich wieder auf true gesetzt...
			SREG = SregAlt;
			}
		else // HilfEZP gar nicht benutzt...
			{ 
			EeMode = Frei;
			// EZP.SchreibAnz = 0 steht schon oben...
			EZP.Gesperrt = false;
			}
		}

	EepromTwi.Phase = 0; // startet nur den nächsten Transfer, wenn AnzDaten > 0

	// StartSwTwi();

	}
	

//! Startet einen TWI-Zugriff auf das EEPROM.	
static void EeStartTransfer()
	{
	#ifdef SWTWI_SHOWMSG
		LokalZeichenAusgabe('S');
	#endif //SWTWI_SHOWMSG

	if (EeMode == Frei || EepromTwi.Phase != 0)
		{
		#ifdef SWTWI_SHOWMSG
			LokalZeichenAusgabe('x');
		#endif //SWTWI_SHOWMSG
		return;
		}

	if (EZP.Gesperrt)
		{
		#ifdef SWTWI_SHOWMSG
			LokalZeichenAusgabe('y');
		#endif //SWTWI_SHOWMSG
		return;
		}
	
	if (EeMode == Schreiben && EZP.SchreibAnz == 0)
		{
		#ifdef SWTWI_SHOWMSG
			LokalZeichenAusgabe('z');
		#endif //SWTWI_SHOWMSG
		return;
		}

	uint8_t SregAlt = SREG;
	cli();
	EZP.Gesperrt = true;
	EZP.TransfBuf[0] = EZP.StartAdr >> 8;
	EZP.TransfBuf[1] = EZP.StartAdr & 0xFF;
	if (EeMode == Auslesen)
		{ // für den Lesevorgang muss erst mal die Lese-Adresse geschrieben (gesendet) werden
		EepromTwi.Adresse = XEEPROM_TWI_ADR; // ...daher nicht + 1
		EepromTwi.Puffer = &EZP.TransfBuf[0];
		EepromTwi.AnzDaten = 2; // nur die Adresse
		}
	else // bleibt nur Schreiben
		{
		EepromTwi.Adresse = XEEPROM_TWI_ADR; 
		EepromTwi.Puffer = &EZP.TransfBuf[0];
		EepromTwi.AnzDaten = 2 + EZP.SchreibAnz;
		}

	SREG = SregAlt;

	#ifdef SWTWI_SHOWMSG
		LokalZeichenAusgabe('=');
		LokalHexAusgabe(EepromTwi.Adresse);
		LokalZeichenAusgabe(':');
		LokalHexAusgabe(EepromTwi.AnzDaten);
		LokalZeichenAusgabe(' ');
		SerSendFlush();
	#endif //SWTWI_SHOWMSG
	// StartSwTwi();
	}
			

bool EeAbschliessen();

	
//! Macht einen Lesezugriff auf das EEPROM.
//--------------------------------------------	
//! optimiert auf sequentielles Lesen, das heißt: beim Lesen des letzten Byte 
//! wird der Puffer verworfen und schon der nächste Block geholt!
//! Blockiert nicht, solange der Zugriff nicht abgeschlossen ist.
//! \param [in,out] Adresse Speicheradresse im EEPROM
//! \param [out] Wert Ablageort für ausgelesenes Wert
//! \param [in] inc Adresse inkrementieren, wenn erfolgreich
//! \retval true Zugriff erfolgt, Wert ist befüllt
//! \retval false Zugriff noch nicht beendet, Funktionsaufruf ggf. wiederholen.
bool EeLesen(uint16_t* Adresse, uint8_t *Wert, bool inc) 
	{
	if (EeMode == Schreiben) 
		if (!EeAbschliessen())
			return false;
	
	EeTransferPruefen();
	
	if (EeMode == Frei)
		{ // neu beginnen
		EeMode = Auslesen;
   		EZP.StartAdr = EeBlockAnf(*Adresse);
	    EeStartTransfer();
		return false;
		}			

	if (EZP.Gesperrt)
		return false; // Lesevorgang läuft noch

	if (EeBlockAnf(*Adresse) == EZP.StartAdr)
	    { // im richtigen Block
		*Wert = EZP.TransfBuf[2 + *Adresse - EZP.StartAdr];
		if (inc)
			{
			*Adresse = EeAdrNorm(*Adresse + 1);
			if (EeBlockAnf(*Adresse) != EZP.StartAdr)
				{ // vorbereitend nächsten Puffer lesen
				EZP.StartAdr = EeBlockAnf(*Adresse);
				EeStartTransfer(); // hier wird EZP.Gesperrt dann gesetzt
				}
			}
		return true;
		} // if im richtigen Blockanfang
	else
		{ // im falschen Block...
  		EZP.StartAdr = EeBlockAnf(*Adresse);
	    EeStartTransfer();
		return false;
		}
	}
	

//! Macht einen Lesezugriff auf das EEPROM.
//--------------------------------------------	
//! optimiert auf sequentielles Lesen, das heißt: beim Lesen des letzten Byte 
//! wird der Puffer verworfen und schon der nächste Block geholt!
//! Blockiert bis der Zugriff abgeschlossen ist.
//! \param [in] Adresse Speicheradresse im EEPROM
//! \return gelesenes Byte aus dem Eeprom.
uint8_t EeLesen2(uint16_t Adresse) 
	// wartet bis erfolgreich
	{
	uint8_t res;
	
	while (!EeLesen(&Adresse, &res, false))
		DoSwTwi();
	return res;
	}
	
	
//! Hilfsfunktion für Schreibzugriff auf EEPROM.
//! \param ezp zu bearbeitender Puffer
//! \param [in] Adresse Speicheradresse im EEPROM
//! \param [in] Wert für die EEPROM-Speicherzelle.
//! \retval true wenn Puffer frei für die Annahme des Bytes.
static bool EePufferSchreib(TEeZwischenPuffer *ezp, uint16_t Adresse, uint8_t Wert) 
	{
	if (ezp->SchreibAnz == 0)
		{
		ezp->StartAdr = Adresse;
		ezp->TransfBuf[2] = Wert;
		ezp->SchreibAnz = 1;
		return true;
		}
	else if (!ezp->Gesperrt 
		     && Adresse >= ezp->StartAdr 
			 && Adresse <= ezp->StartAdr + ezp->SchreibAnz
			 && Adresse < EeBlockAnf(ezp->StartAdr) + XEEPROM_PUFFER_MAX)
		{ 
		ezp->TransfBuf[2 + Adresse - ezp->StartAdr] = Wert;
		if (Adresse >= ezp->StartAdr + ezp->SchreibAnz)
			ezp->SchreibAnz = Adresse - ezp->StartAdr + 1;
		return true;
		}
	else
		return false;
	}
		

//! Macht einen Lesezugriff auf das EEPROM.
//--------------------------------------------	
//! optimiert auf sequentielles Schreiben.
//! Blockiert nicht, solange der Zugriff nicht abgeschlossen ist.
//! \param [in,out] Adresse Speicheradresse im EEPROM
//! \param [in] Wert zu speicherndes Byte
//! \param [in] inc Adresse inkrementieren, wenn erfolgreich
//! \retval true Zugriff erfolgt Wert ist geschrieben.
//! \retval false Zugriff noch nicht beendet, Funktionsaufruf ggf. wiederholen.
bool EeSchreiben(uint16_t* Adresse, uint8_t Wert, bool inc) 
	{
	if (EeMode == Auslesen) 
		EeMode = Frei;
	
	EeTransferPruefen();
	
	if (EeMode == Frei)
		{
		EeMode = Schreiben;
		EZP.SchreibAnz = 0;
		EZP.Gesperrt = false;
		}
	
	if (EePufferSchreib(&EZP, *Adresse, Wert))
		{ // das ist der Regelfall
		if (inc)
			{
			if (*Adresse == EeBlockAnf(EZP.StartAdr) + XEEPROM_PUFFER_MAX - 1)
				EeStartTransfer();
			*Adresse = EeAdrNorm(*Adresse + 1);
			}
		return true;
		} // immer noch der gleiche Block
	
	// Schreibvorgang geht in einen neuen Block...

	if (EepromTwi.Phase == 0)
		EeStartTransfer(); // bisherige Daten schreiben (sofern nicht schon gestartet)
	
	if (EePufferSchreib(&HilfEZP, *Adresse, Wert)) // und zu schreibenden Wert in Hilfspuffer senden
		{ // hat geklappt
		if (inc)
			*Adresse = EeAdrNorm(*Adresse + 1);
		return true;
		}

	// Schreiben hat weder im Normalen noch im Hilfspuffer geklappt --> schade...
	return false;
	}


//! Aktuellen Lese- oder Schreibzugriff beenden.
//--------------------------------------------	
//! Blockiert nicht, solange der Zugriff nicht abgeschlossen ist.
//! \retval true Zugriff ist abgeschlossen.
//! \retval false Zugriff noch nicht beendet, Funktionsaufruf ggf. wiederholen.
bool EeAbschliessen() 
	{
	if (EeMode == Auslesen)
		EeMode = Frei;
	if (EeMode != Schreiben)
		return true;
	if (EZP.Gesperrt)
		EeTransferPruefen();
	else
		EeStartTransfer();
	return EeMode != Schreiben; 
		// wird bei Abschluß der Datenübertragung durch EeTransferPruefen() auf Frei gesetzt
	}


#ifdef EXTEEPROM_DEBUG


#include "LokalAusgabe.h"

int8_t LokalZahlEingabe(uint8_t* z, uint8_t maxzif);

char LokalZeichenLesen();

void SerSendFlush();


void EZPDebugAusg(TEeZwischenPuffer *ezp) 
	{
	LokalZeichenAusgabe('\r');
	LokalZeichenAusgabe('\n');
	LokalZeichenAusgabe(ezp->Gesperrt ? 'G' : 'F');
	LokalZeichenAusgabe(' ');
	LokalZahlAusgabe(ezp->StartAdr, 0);
	LokalZeichenAusgabe('/');
	LokalZahlAusgabe(ezp->SchreibAnz, 0);
	uint8_t i;
	for (i = 0 ; i < ezp->SchreibAnz ; i++)
		{
		LokalZeichenAusgabe(',');
		LokalZahlAusgabe(ezp->TransfBuf[2+i], 0);
		}
	LokalZeichenAusgabe(' ');
	}


void XEepromDebug()
	{
	uint8_t SerEmpfAnz();
	void SeriellIO();
	
	uint8_t Start, Ende, Wert, inc;
	uint16_t i;

	EeInit();
	while (true)
		{
		/*/ HACK debug...
		LokalZahlAusgabe(BeginnErsteMeldung, 0);
		LokalZeichenAusgabe('/');
		LokalZahlAusgabe(EndeLetzteMeldung, 0);
		//*/
		LokalTextAusgabeP(PSTR("\r\nEeprom (L/S/I/A/V/Q) >"));
		inc = 0;

		switch (LokalZeichenLesen())
			{
			case 'q':
			case '\t':
			case '\0':
				return;

			case 'l':
				LokalTextAusgabeP(PSTR(" Lesen von "));
				if (LokalZahlEingabe(&Start, 0) <= 0)
					break;
				LokalTextAusgabeP(PSTR(" bis "));
				if (LokalZahlEingabe(&Ende, 0) <= 0)
					break;
				i = Start; 
				while (i <= Ende)
					{
					DoSwTwi();
					if (EeLesen(&i, &Wert, true))
						{
						LokalZeichenAusgabe(' ');
						LokalZahlAusgabe(i-1, 0); // -1, da inzwischen inkrementiert wurde
						LokalZeichenAusgabe('=');
						LokalZahlAusgabe(Wert, 0);
						}
					else
						{
						//LokalZahlAusgabe(i, 0);
						//LokalZeichenAusgabe('.');
						//LokalZahlAusgabe(EepromTwi.Phase, 0);
						}
					SerSendFlush();
					}
				LokalTextAusgabeP(PSTR("\r\n"));
				break;

			case 'i':
				LokalTextAusgabeP(PSTR(" inkrementell"));
				inc = 1;
				// kein break
			case 's':
				LokalTextAusgabeP(PSTR(" Schreiben von "));
				if (LokalZahlEingabe(&Start, 0) <= 0)
					break;
				LokalTextAusgabeP(PSTR(" bis "));
				if (LokalZahlEingabe(&Ende, 0) <= 0)
					break;
				LokalTextAusgabeP(PSTR(" Wert "));
				if (LokalZahlEingabe(&Wert, 0) <= 0)
					break;

				for (i = Start ; i <= Ende ; )
					{
					DoSwTwi();
					if (EeSchreiben(&i, Wert, true))
						{
						LokalZeichenAusgabe(' ');
						LokalZahlAusgabe(i-1, 0); // -1, da inzwischen inkrementiert wurde
						LokalZeichenAusgabe('=');
						LokalZahlAusgabe(Wert, 0);
						Wert += inc;
						}
					else
						{
						//LokalZeichenAusgabe('.');
						//LokalZahlAusgabe(EepromTwi.Phase, 0);
						}
					SerSendFlush();
					}
				LokalTextAusgabeP(PSTR("\r\n"));
				EZPDebugAusg(&EZP);
				EZPDebugAusg(&HilfEZP);
				
				break;

			case 'a':
				while (!EeAbschliessen())
					{
					DoSwTwi();
					//LokalZeichenAusgabe(':');
					//LokalZahlAusgabe(EepromTwi.Phase, 0);
					}
				break;
			
			case 'v':
				// HACK test doppelt schreiben
				// das geht regelmäßig schief... warum auch immer...

				LokalTextAusgabeP(PSTR(" 1. Adr "));
				if (LokalZahlEingabe(&Start, 0) <= 0)
					break;
				LokalTextAusgabeP(PSTR(" 2. Adr "));
				if (LokalZahlEingabe(&Ende, 0) <= 0)
					break;
				LokalTextAusgabeP(PSTR(" Wert "));
				if (LokalZahlEingabe(&Wert, 0) <= 0)
					break;

				i = Start;
				while (!EeSchreiben(&i, Wert, false))
					DoSwTwi();
				while (!EeAbschliessen())
					DoSwTwi(); // erst Eeprom-Schreibvorgang abwarten

				i = Ende; 
				while (!EeSchreiben(&i, Wert+1, false)) // bisher stand dort MsgEndeLetzte
					DoSwTwi(); // Erst jetzt Ende-Markierung wegnehmen
				while (!EeAbschliessen())
					DoSwTwi(); // erst Eeprom-Schreibvorgang abwarten

				EZPDebugAusg(&EZP);
				EZPDebugAusg(&HilfEZP);

			} // switch c

		} // while true

	}

#endif //def EXTEEPROM_DEBUG


// ###############################################################################
// Speicherung von Telegrammen im EEPROM
// ###############################################################################

// Variablen für Meldungsspeicher im externen EEPROM
// -------------------------------------------------

// externes EEPROM spechert eingehende Nachrichten im Format:
// 1 Byte Header (MsgStart oder MsgStartNeu)
// x Byte der Meldung (in Klartext, inkl. Datum am Anfang
// 1 Byte Schlusszeichen (MsgEnde oder MsgEndeLetzte)

enum { MsgStartNeu = 0xAA, MsgStart = 0xCC, MsgEnde = 0xDD, MsgEndeLetzte = 0xEE } ;
// dezimal:          170               204             221                   238

// Möglicher Puffer-Inhalt (je Zeile eine Speicherstelle)
// MsgStart			Anfang einer bereits gelesenen (=gelöschten) Nachricht
//  <Text>			x Byte Meldungstext (Baudot-Code!)
// MsgEnde			Ende der nicht letzten Nachricht
// MsgStartNeu [B]	Anfang einer noch nicht gelesenen (=gelöschten) Nachricht
//  <Text>			x Byte Meldungstext (Baudot-Code!)
// MsgEnde			Ende der nicht letzten Nachricht
// MsgStart			Anfang einer bereits gelesenen (=gelöschten) Nachricht
//  <Text>			x Byte Meldungstext (Baudot-Code!)
// MsgEndeLetzte [E] Ende der LETZTEN Nachricht
//	[B] Position des Zeigers BeginnErsteMeldung: Erste Meldung mit Anfang MsgStartNeu
//                                              oder Ende der aller letzten Meldung
//  [E] Position des Zeiger EndeLetzteMeldung: Ende der letzten Meldung (das Byte wird bei der
//                                           nächsten empfangenen Meldung überschrieben

uint16_t EndeLetzteMeldung; //!< Letztes Byte der Letzten Meldung. Wird erst am Ende des Empfangs 
							//!< der neuen Nachricht aktualisiert

uint16_t SpeicherAdresse; //!< tatsächliche Adresse beim Schreiben in das externe EEPROM

uint16_t BeginnErsteMeldung; //!< tatsächliche Adresse der ersten ungelesenen Meldung im externen EEPROM
	//!< ODER auch Adresse des Ende einer Meldung, wenn keine ungelesene Meldung im externen EEPROM

uint16_t BeginnErsteMeldung2; //!< im internen EEPROM gespeicherte Adresse der ersten ungel. Meldung
	//!< wird nur bei großen Abweichungen ( > 1000 Zeichen) aktualisiert

uint16_t WiedergabeAdresse; //!< Leseposition in der aktuell Wiedergegebenen Meldung

uint16_t BeginnLetzteWiedergMeldung; //!< Beginn der aktuell Wiedergegebenen Meldung

#define ADR_UNGUELTIG ((uint16_t) ~1) //!< Wert für ungültige Adressen

bool SpeichernEin = false; //!< Meldungsempfang wird gespeichert.


//!< Alles im EEPROM löschen.
void MsgSpeicherLoeschen()
	{
	BeginnErsteMeldung2 = 0;
	BeginnErsteMeldung = 0;
	EndeLetzteMeldung = 0;
	SpeicherAdresse = 0;
	while (!EeSchreiben(&SpeicherAdresse, MsgEndeLetzte, false))
		DoSwTwi();
	while (!EeAbschliessen())
		DoSwTwi();
	SpeicherAdresse = ADR_UNGUELTIG;
	WiedergabeAdresse = ADR_UNGUELTIG;
	BeginnLetzteWiedergMeldung = ADR_UNGUELTIG;
	}

	
//!< Initialisiert alle Verweise auf die erste, letzte usw. Meldung
void MsgSpeicherInit()
	{
	bool StartGefunden = false;

	if (get_TASTE() || BeginnErsteMeldung2 == 0xEEEE)
		{ // Taste ist gedrückt --> Initialisierungs des externen Eeprom
		MsgSpeicherLoeschen();
		return;
		}

	uint8_t x = 0;
	uint16_t XeeAdr = BeginnErsteMeldung2;
	do 
		{
		DoSwTwi();
		x = EeLesen2(XeeAdr);
		if (x == MsgStartNeu && !StartGefunden)
			{
			BeginnErsteMeldung = XeeAdr;
			StartGefunden = true;
			}
		if (x == MsgEndeLetzte)
			{
			EndeLetzteMeldung = XeeAdr;
			if (!StartGefunden)
				BeginnErsteMeldung = XeeAdr;
			break; // nicht weiter suchen
			}
		XeeAdr = EeAdrNorm(XeeAdr + 1);
		} while (XeeAdr != BeginnErsteMeldung2);
		
	while (!EeAbschliessen())
		DoSwTwi();

	if (!StartGefunden)
		MsgSpeicherLoeschen();

	WiedergabeAdresse = ADR_UNGUELTIG;
	SpeicherAdresse = ADR_UNGUELTIG;
	BeginnLetzteWiedergMeldung = ADR_UNGUELTIG;
	}
	

//! Ein Zeichen in das EEPROM schreiben.
void AufzeichnungZeichen(char c)
	{
	if (SpeicherAdresse != ADR_UNGUELTIG)
		EeSchreiben(&SpeicherAdresse, c & 0x7F, true);
	}


//! Ein Zahl in das EEPROM schreiben.
void AufzeichnungZahl2(uint8_t x)
	{ // wartet auf jeden fall auf erfolgreiche Ausführung...
	uint8_t z = x / 10;

	if (SpeicherAdresse == ADR_UNGUELTIG)
		return;
		
	while (!EeSchreiben(&SpeicherAdresse, '0' + z, true))
		DoSwTwi(); // warten
	while (!EeSchreiben(&SpeicherAdresse, '0' + (x - 10*z), true))
		DoSwTwi(); // warten
	}


//! Beginn einer Aufzeichnung im EEPROM markieren (Datum und Uhrzeit)	
bool AufzeichnungBeginn(uint8_t Jahr, uint8_t Monat, uint8_t Tag, uint8_t Stunde, uint8_t Minute)
	{
	// Position feststellen
	SpeicherAdresse = EeAdrNorm(EndeLetzteMeldung + 1);

	if (EeSchreiben(&SpeicherAdresse, MsgStartNeu, true))
		{ 
		AufzeichnungZahl2(Tag);
		AufzeichnungZeichen('.');
		AufzeichnungZahl2(Monat);
		AufzeichnungZeichen('.');
		AufzeichnungZahl2(Jahr);
		AufzeichnungZeichen(' ');
		AufzeichnungZahl2(Stunde);
		AufzeichnungZeichen(':');
		AufzeichnungZahl2(Minute);
		AufzeichnungZeichen('\r');
		AufzeichnungZeichen('\n');
		return true;
		}
	else // Eeprom offensichtlich nicht schreibbereit...
		return false;
	}


//! Ende einer Aufzeichnung im EEPROM markieren 
void AufzeichnungEnde()
	{
	while (!EeSchreiben(&SpeicherAdresse, MsgEndeLetzte, false))
		DoSwTwi(); // Meldungsende muss merkiert werden

	// jetzt SchreibenEnde, weil nächster Schreibvorgang 'rückwärts' geht
	while (!EeAbschliessen())
		DoSwTwi(); // erst Eeprom-Schreibvorgang abwarten

	while (!EeSchreiben(&EndeLetzteMeldung, MsgEnde, false)) // bisher stand dort MsgEndeLetzte
		DoSwTwi(); // Erst jetzt Ende-Markierung wegnehmen

	EndeLetzteMeldung = SpeicherAdresse;
	SpeicherAdresse = ADR_UNGUELTIG;
	}
	

//! Laufende Aufzeichnung abbrechen.
void AufzeichnungAbbruch()
	{
	EeAbbruch();
	SpeicherAdresse = ADR_UNGUELTIG;
	}


// Wiedergabe 
// ==========


//! initialisiert die Wiedergabe. 
//------------------------------
//! Danach muss WiedergabeNaechsteMeldung aufgerufen werden
void WiedergabeStart()
	{
	while (!EeAbschliessen())
		DoSwTwi();
	WiedergabeAdresse = BeginnErsteMeldung;
	BeginnLetzteWiedergMeldung = ADR_UNGUELTIG;
	}


//! Springt zur nächste Meldung im EEPROM 
//---------------------------------------------
//! darf auch mehrfach aufgerufen werden, bleibt bei neuer Meldung stehen
//! \retval true wenn noch eine ungelesene Meldung gefunden wurde...
bool WiedergabeNaechsteMeldung()
	{
	if (WiedergabeAdresse == ADR_UNGUELTIG)
		return false;

	while (true)
		{
		uint8_t Code = EeLesen2(WiedergabeAdresse);

/*
LokalZahlAusgabe16(WiedergabeAdresse, 0);
LokalZeichenAusgabe(':');
LokalZahlAusgabe(Code, 0);
LokalZeichenAusgabe('\r');
LokalZeichenAusgabe('\n');
SerSendFlush();
*/
		if (Code == MsgStartNeu)
			{
			BeginnLetzteWiedergMeldung = WiedergabeAdresse;
			return true;
			}
		else if (Code == MsgEndeLetzte)
			{
			EndeLetzteMeldung = WiedergabeAdresse;
			WiedergabeAdresse = ADR_UNGUELTIG;
			return false;
			}
		else // alles andere interessiert nicht
			WiedergabeAdresse = EeAdrNorm(WiedergabeAdresse + 1);
		} 
	}
	

//! Gibt nächste Meldung aus dem EEPROM wieder.
//---------------------------------------------
//! darf auch mehrfach aufgerufen werden, bleibt bei neuer Meldung stehen
//! \retval Zeichen auf dem EEPROM, '\0' am Meldungsende (wiederholbar).
char WiedergabeZeichen()
	{
	if (WiedergabeAdresse == ADR_UNGUELTIG)
		return '\0';
		
	while (true)
		{
		uint8_t Code = EeLesen2(WiedergabeAdresse);

		if (Code == MsgEnde)
			return '\0';

		if (Code == MsgEndeLetzte)
		 	{
			EndeLetzteMeldung = WiedergabeAdresse;
			return '\0';
			}

		WiedergabeAdresse = EeAdrNorm(WiedergabeAdresse + 1);

		if (Code != MsgStartNeu && Code != MsgStart)
			return (char) Code;
		} // sonst nächstes Zeichen holen
	}


//! Löscht die aktuell wiedergegebene Meldung.
//----------------------------------------------	
//! springt auch automatisch zur nächsten Meldung
void WiedergabeLoescheAktuelleMeldung()
	{
	bool ErsteMeldungWurdeGeloescht;
	uint16_t NaechsteNeueMeldung;
	uint16_t LoeschPosition;

	if (BeginnLetzteWiedergMeldung == ADR_UNGUELTIG)
		return;

	LoeschPosition = BeginnLetzteWiedergMeldung;

	ErsteMeldungWurdeGeloescht = (LoeschPosition == BeginnErsteMeldung)
						          || (LoeschPosition == EeAdrNorm(BeginnErsteMeldung + 1));

	if (WiedergabeNaechsteMeldung())
		NaechsteNeueMeldung = BeginnLetzteWiedergMeldung;
	else
		NaechsteNeueMeldung = EndeLetzteMeldung;

	// da ein Rückschritt gemacht wird:
	while (!EeAbschliessen())
		DoSwTwi(); 

	if (EeLesen2(LoeschPosition) != MsgStartNeu)
		{ // kann eigentlich nicht sein
		FehlerStop(11);
		}

	while (!EeSchreiben(&LoeschPosition, MsgStart, false))
		DoSwTwi();

	if (ErsteMeldungWurdeGeloescht)
		{ 
		BeginnErsteMeldung = NaechsteNeueMeldung; 
		if (BeginnErsteMeldung2 + 500 < BeginnErsteMeldung || BeginnErsteMeldung2 > BeginnErsteMeldung)
			BeginnErsteMeldung2 = BeginnErsteMeldung;
		}
	}
	
	
//! Beendet die laufende Wiedergabe
//----------------------------------------------	
void WiedergabeEnde()
	{
	while (!EeAbschliessen())
		DoSwTwi(); // erst Eeprom-Schreibvorgang abwarten
	WiedergabeAdresse = ADR_UNGUELTIG;
	BeginnLetzteWiedergMeldung = ADR_UNGUELTIG;
	}

	
#endif //def AF_ANRUFSPEICHER
