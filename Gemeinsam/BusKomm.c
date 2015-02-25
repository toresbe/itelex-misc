#include <inttypes.h>
#include <avr/wdt.h>

#include <stdbool.h>
#include "TwiEvents.h"

#include "TxP2-Defs.h"
#include "BusKomm.h"
#include "BaudotCode.h"
#include "SeriellUmsetz.h"


volatile uint8_t Status;


// HACK (gilt nur für TW39):
//#define ROT_EIN SET_BIT(PORTD,1);
//#define ROT_AUS CLR_BIT(PORTD,1);


#define EMPF_PUFFER_GROESSE 20

// Variablen für Datenaustausch über I²C
// -------------------------------------

// globale Variablen aus BusKomm.h

volatile TBusAuftrag BusAuftrag; 
volatile TBusErgebnis BusErgebnis; 
uint8_t BusEigenAdresse; 
uint8_t BusEigenAdrMehrfach; 
bool RundsendEmpfFreig; 
volatile uint8_t BusAnrufSubAdresse; 
volatile uint8_t BusVerbPartner; 
volatile uint8_t BusSendeDaten;
volatile bool BusFrei;
volatile uint8_t BusKollisionZaehler; 
volatile bool BusEmpfMark;
volatile bool BusEmpfMarkwechsel;
volatile uint16_t TwiIsrCount;
volatile uint16_t TwiWatchdogCount;
volatile uint8_t RundsendDaten[RundsendMaxDaten]; 
volatile uint8_t RundsendAnzDaten; 
	

// lokal:
static volatile uint8_t BusEmpfPuffer[EMPF_PUFFER_GROESSE]; 
	//!< Puffert empfangene Kommandos vom TWI-Bus \e außer BusKdoMark und BusKdoSpace.
static volatile uint8_t BusEmpfPufferSchreibPos;
	//!< Index für BusEmpfPuffer beim Eintragen von Daten.
static volatile uint8_t BusEmpfPufferLesePos;
	//!< Index für BusEmpfPuffer beim Auslesen von Daten.
static volatile uint8_t BusEmpfFremdStatus;
	//!< Ablage für Status-Byte eines abgefragten Bus-Partners.
static volatile uint8_t RundsendPufferPos;
	//!< Index für RundsendDaten
	
// Makros für Debugging
// --------------------
// ggf mit Aktionen füllen

#define DEBUG_BUSTRANSFER_FEHLER
#define DEBUG_BUSTRANSFER_INIT 				
#define DEBUG_BUSTRANSFER_INITEND
#define DEBUG_BUSTRANSFER_START
#define DEBUG_BUSTRANSFER_KOLLISION
#define DEBUG_BUSTRANSFER_FERTIG			
#define DEBUG_BUSTRANSFER_EMPFANGSTART
#define DEBUG_BUSTRANSFER_EMPFANGFERTIG


//! Loscht ein Bit in der globalen Variable Status.
//---------------------------------------------
//! \param BitNr Bit-Nummer (Konstanten StatBit_* benutzen).
void CLR_BIT_Status(uint8_t BitNr)
	{
	uint8_t SregAlt = SREG;
	cli();
	CLR_BIT(Status, BitNr);
	SREG = SregAlt;
	}
	
	
//! Setzt ein Bit in Status.
//---------------------------------------------
//! \param BitNr Bit-Nummer (Konstanten StatBit_* benutzen).
void SET_BIT_Status(uint8_t BitNr)
	{
	uint8_t SregAlt = SREG;
	cli();
	SET_BIT(Status, BitNr);
	SREG = SregAlt;
	}
	
	
void FehlerStop(int Nummer);


//! Interne Funktion zur Verarbeitung eines über TWI empfangenen Kommandos.
// ---------------------------------------------------------------
//! BusKdoMark und BusKdoSpace wirken auf BusEmpfMark. 
//! Alle anderen Kommandos werden in einen Puffer geschrieben.
//! \param RecData Empfangenes TWI-Kommando.
static void EmpfByteSpeichern(uint8_t RecData)
	{
	switch (RecData)
		{
		case BusLebenszeichen:
			break; // wird ignoriert

		case BusKdoSpaceWdh:
			// TODO fehlenden ersten Wechsel melden
		case BusKdoSpace:
			BusEmpfMarkwechsel = true;
			BusEmpfMark = false;
			break;

		case BusKdoMarkWdh:
			// TODO fehlenden ersten Wechsel melden
		case BusKdoMark:
			BusEmpfMarkwechsel = true;
			BusEmpfMark = true;
			break;

		default: // alles andere in den Puffer
			BusEmpfPuffer[BusEmpfPufferSchreibPos++] = RecData;
			if (BusEmpfPufferSchreibPos >= EMPF_PUFFER_GROESSE)
				{
				if (BusEmpfPufferLesePos == 0) // Es würde ein Überlauf entstehen, also zurück...
					BusEmpfPufferSchreibPos = EMPF_PUFFER_GROESSE - 1;
				else // kein Überlauf...
					BusEmpfPufferSchreibPos = 0;
				}
			else
				{ 
				if (BusEmpfPufferSchreibPos == BusEmpfPufferLesePos) // Es würde ein Überlauf entstehen, also zurück...
					BusEmpfPufferSchreibPos--;
				}
			SET_BIT(Status, StatBit_BusKdoEmpfangen); // hier nicht SET_BIT_Status, da sonst das Interrupt-Flag wieder gesetzt wird!
			CLR_BIT(Status, StatBit_Frei); // hier nicht SET_BIT_Status, da sonst das Interrupt-Flag wieder gesetzt wird!
			break;

		} // switch RecData

	} // EmpfByteSpeichern
	

//! Interrupt-Routine für TWI. 
//----------------------------
//! Verarbeitet Statusänderungen des Atmel-TWI-Interface.
ISR(TWI_vect)
	{
	uint8_t NewStat, RecData, SendData;

	NewStat = (0<<TWINT) | (1<<TWEA) | (0<<TWSTA) | (0<<TWSTO) | (1<<TWEN) | (1<<TWIE);
		// TWEA standardmäßig gesetzt, muss ggf. wieder gelöscht werden.

#ifdef TWI_DEBUG
	void DebSp(uint8_t x);
	DebSp(TWSR & TwiEv_Mask);
#endif //TWI_DEBUG
	
	TwiIsrCount++;
	
	switch (TWSR & TwiEv_Mask)
		{
		// Allgemein
		// ---------
        case TwiEv_BusError			:
        	DEBUG_BUSTRANSFER_FEHLER;
			SET_BIT(NewStat, TWSTO);
			BusFrei = true;
			break;

        case TwiEv_MasterStart		:
        	DEBUG_BUSTRANSFER_START;
			if (BusAuftrag == Rundsenden)
				{
				TWDR = 0;
				RundsendPufferPos = 0;
				}
			else
			if (BusAuftrag == Senden)
				TWDR = BusVerbPartner & ~1; // Bit 0 = 0 -> Write
			else
				TWDR = BusVerbPartner | 1; // Bit 0 = 1 -> Read
#ifdef TWI_DEBUG
			DebSp(BusVerbPartner);
#endif //TWI_DEBUG
			BusFrei = false;
			break;

        case TwiEv_MasterRepStart	: // kann nur bei Funktion BedSenden auftreten
			TWDR = BusVerbPartner & ~1; // Bit 0 = 0 -> Write
#ifdef TWI_DEBUG
			DebSp(BusVerbPartner);
#endif //TWI_DEBUG
			BusFrei = false;
			break;

		// Master-Transmit
		// ---------------
        case TwiEv_MT_AddrACK		:
			if (BusAuftrag == Rundsenden)
				SendData = RundsendDaten[RundsendPufferPos++];
			else
				SendData = BusSendeDaten;
#ifdef TWI_DEBUG
			DebSp(SendData);
#endif //TWI_DEBUG
			TWDR = SendData;
			break;

        case TwiEv_MT_AddrNACK		:
			BusAuftrag = Fertig;
			BusErgebnis = KeineAntwort; // wiederholung hat keinen Sinn
			SET_BIT(NewStat, TWSTO);
			BusFrei = true;
			DEBUG_BUSTRANSFER_FERTIG;
			break;

        case TwiEv_MT_DataACK		:
			if (BusAuftrag == Rundsenden && RundsendPufferPos < RundsendMaxDaten)
				{
				SendData = RundsendDaten[RundsendPufferPos++];
#ifdef TWI_DEBUG
				DebSp(SendData);
#endif //TWI_DEBUG
				TWDR = SendData;
				}
			else
				{
				BusAuftrag = Fertig;
				BusErgebnis = Ok; // Zeichen der erfolgreichen Erledigung
				SET_BIT(NewStat, TWSTO); // weil nur ein Byte zu übertragen ist
				BusFrei = true;
				DEBUG_BUSTRANSFER_FERTIG;
				}
			break;

        case TwiEv_MT_DataNACK		:
			//! \todo Senden Abbrechen
			if (BusAuftrag == Rundsenden && RundsendPufferPos < RundsendMaxDaten)
				{
				SendData = RundsendDaten[RundsendPufferPos++];
#ifdef TWI_DEBUG
				DebSp(SendData);
#endif //TWI_DEBUG
				TWDR = SendData;
				}
			else
				{
				BusAuftrag = Fertig;
				BusErgebnis = Ok; // Zeichen der erfolgreichen Erledigung (das vorherige Byte wurde angenommen!)
				SET_BIT(NewStat, TWSTO); 
				BusFrei = true;
				DEBUG_BUSTRANSFER_FERTIG;
				}
			break;

        case TwiEv_MT_ArbitrLost	:
			SET_BIT(NewStat, TWSTA); // gleich nochmal probieren
				// Bei BedSenden wird auf jeden Fall zuerst wieder das Status-Lesen begonnen
			BusKollisionZaehler++;
			DEBUG_BUSTRANSFER_KOLLISION;
			break;

		// Master-Receive (kann keine Rundsendung sein)
		// --------------
        case TwiEv_MR_AddrACK		:
			CLR_BIT(NewStat, TWEA); // damit nach erstem Datenbyte NACK gesendet wird
			break;

        case TwiEv_MR_AddrNACK		:
			BusAuftrag = Fertig;
			BusErgebnis = KeineAntwort; // wiederholung hat keinen Sinn
			SET_BIT(NewStat, TWSTO);
			BusFrei = true;
			DEBUG_BUSTRANSFER_FERTIG;
			break;

        case TwiEv_MR_DataACK		:
        case TwiEv_MR_DataNACK		:
			BusEmpfFremdStatus = TWDR;
#ifdef TWI_DEBUG
			DebSp(BusEmpfFremdStatus);
#endif //TWI_DEBUG
			if (BusAuftrag == BedSenden) // kommt nur bei Verbindungsaufnahme vor...
				if (BIT_IS_SET(BusEmpfFremdStatus, StatBit_Frei)) 
					{ // Empfänger ist frei
					SET_BIT(NewStat, TWSTA); // repeated Start
					}
				else
					{
					BusAuftrag = Fertig;
					BusErgebnis = Besetzt; // Zeichen der nicht erfolgreichen Erledigung
					SET_BIT(NewStat, TWSTO);
					BusFrei = true;
					DEBUG_BUSTRANSFER_FERTIG;
					}
			else // !BedSenden
				{
				BusAuftrag = Fertig;
				BusErgebnis = Ok; // Zeichen der erfolgreichen Erledigung
				SET_BIT(NewStat, TWSTO);
				BusFrei = true;
				DEBUG_BUSTRANSFER_FERTIG;
				}
			break;

		// Slave-Receive
		// -------------
        case TwiEv_SR_AddrACK_AL	: // after lost arbitration 
			BusKollisionZaehler++;
			// kein break
			
		case TwiEv_SR_AddrACK		:
			wdt_reset();
			TwiWatchdogCount = 0;
			BusFrei = false;
			RundsendPufferPos = 0; // muss hier gemacht werden, damit beim TwiEv_SR_Stop nicht RundsendAnzDaten gesetzt wird.
			DEBUG_BUSTRANSFER_EMPFANGSTART;
			BusAnrufSubAdresse = (TWDR >> 1) & (BusEigenAdrMehrfach - 1);
			CLR_BIT(NewStat, TWEA); // damit nach erstem Datenbyte NACK gesendet wird 
			break;

        case TwiEv_SR_DataACK		:
			RecData = TWDR;
#ifdef TWI_DEBUG
			DebSp(RecData);
#endif //TWI_DEBUG
			EmpfByteSpeichern(RecData);
			CLR_BIT(NewStat, TWEA); // damit weiter NACK gesendet wird 
			break;

        case TwiEv_SR_DataNACK		:
			RecData = TWDR;
#ifdef TWI_DEBUG
			DebSp(RecData);
#endif //TWI_DEBUG
			EmpfByteSpeichern(RecData);
			BusFrei = true;
			DEBUG_BUSTRANSFER_FERTIG;
			if (BusAuftrag == Lesen || BusAuftrag == Senden || BusAuftrag == BedSenden || BusAuftrag == Rundsenden)
				SET_BIT(NewStat, TWSTA);
			break;

        case TwiEv_SR_Stop			: // dies wird auch beim GeneralCall aufgerufen
			BusFrei = true;
			if (RundsendPufferPos > RundsendAnzDaten)
				RundsendAnzDaten = RundsendPufferPos;
				
			DEBUG_BUSTRANSFER_FERTIG;
			if (BusAuftrag == Lesen || BusAuftrag == Senden || BusAuftrag == BedSenden || BusAuftrag == Rundsenden)
				SET_BIT(NewStat, TWSTA);
			break;

		// Slave-Receive als General call
		// ------------------------------
        case TwiEv_SR_GenCallACK_AL	: // after lost arbitration 
			BusKollisionZaehler++;
			// kein break
			
		case TwiEv_SR_GenCallACK		:
			wdt_reset();
			TwiWatchdogCount = 0;
			BusFrei = false;
			DEBUG_BUSTRANSFER_EMPFANGSTART;
			RundsendPufferPos = 0;
			RundsendAnzDaten = 0;
			// bei Rundsendungen grundsätzlich kein NACK durch den Empfänger
			break;

        case TwiEv_SR_DataGcACK		:
			RecData = TWDR;
#ifdef TWI_DEBUG
			DebSp(RecData);
#endif //TWI_DEBUG
			if (RundsendPufferPos < RundsendMaxDaten)
				RundsendDaten[RundsendPufferPos++] = RecData;
			break;

        case TwiEv_SR_DataGcNACK		:
			RecData = TWDR;
#ifdef TWI_DEBUG
			DebSp(RecData);
#endif //TWI_DEBUG
			if (RundsendPufferPos < RundsendMaxDaten)
				RundsendDaten[RundsendPufferPos++] = RecData;
			RundsendAnzDaten = RundsendPufferPos; 

			BusFrei = true;
			DEBUG_BUSTRANSFER_FERTIG;
			if (BusAuftrag == Lesen || BusAuftrag == Senden || BusAuftrag == BedSenden || BusAuftrag == Rundsenden)
				SET_BIT(NewStat, TWSTA);
			break;

		// Slave-Transmit (kann kein Rundsenden sein)
		// --------------
        case TwiEv_ST_AddrACK_AL	:
			BusKollisionZaehler++;
			// kein break
			
        case TwiEv_ST_AddrACK		:
			DEBUG_BUSTRANSFER_EMPFANGSTART;
			// weiter mit TWDR = Status...
			       	
        case TwiEv_ST_DataACK		:
			TWDR = Status;
#ifdef TWI_DEBUG
			DebSp(Status);
#endif //TWI_DEBUG
			BusFrei = false;
			break;

        case TwiEv_ST_DataNACK		: // Beendigung durch Master
        case TwiEv_ST_DataLast 		: // Beendigung durch Slave
			BusFrei = true;
			DEBUG_BUSTRANSFER_EMPFANGFERTIG;
			if (BusAuftrag == Lesen || BusAuftrag == Senden || BusAuftrag == BedSenden || BusAuftrag == Rundsenden)
				SET_BIT(NewStat, TWSTA);
			break;

		default:
			DEBUG_BUSTRANSFER_FEHLER;
#ifdef FALSCHKDO_FEHLERSTOP
			FehlerStop(2);
#endif
			break;

		}

	TWCR = NewStat | (1<<TWINT);
	}
	

//! Wartet, bis TWI-Bus-Auftrag (lesen oder schreiben) beendet ist. 
//-----------------------------------------------------------------
//! Erfolgreich oder nicht ist egal.
void BusWarteFertig()
	{
	while (BusAuftrag != Nichts && BusAuftrag != Fertig)
		; // warten, bis eigene Kommandos bearbeitet sind. kein wdt_reset, da es schnell gehen sollte.
	}
	

//! Sendet ein TWI-Kommando an den aktuellen Verbindungspartner.
// ---------------------------------------------------------------
//! Der aktuelle Verbindungspartner ist BusVerbPartner.
//! Wartet, bis vorheriger TWI-Transfer abgeschlossen ist.
//! Wartet \e nicht auf Abschluss des aktuellen Transfers.
//! Ergebnis (Erfolg) ist in den globalen Variablen BusAuftrag und BusErgebnis zu entnehmen.
//! \param Kdo das zu sendende TWI-Kommando. Siehe Txp2-Defs.h.	
void BusSenden(uint8_t Kdo)
	{ // an BusVerbPartner 
	DEBUG_BUSTRANSFER_INIT;

	BusWarteFertig();

	uint8_t sreg_alt = SREG;
	cli();
	
	BusSendeDaten = Kdo;
	
	if (Kdo > BusKdoVerbAufnahme)
		BusAuftrag = Senden;
	else
		BusAuftrag = BedSenden;

	if (BusFrei)
		{
		while (BIT_IS_SET(TWCR, TWSTO))
			;
		SET_BIT(TWCR, TWSTA);
		}
	else
		DEBUG_BUSTRANSFER_KOLLISION;
		
	SREG = sreg_alt; // setzt altes Interrupt-Enable zurück

	DEBUG_BUSTRANSFER_INITEND;

	}
	
	
//! Fragt aktuellen Modul-Zustand (Status) eines anderen Moduls ab. 
// ----------------------------------------------------------------
//! "Verbiegt" dazu vorübergehend BusVerbPartner.
//! \param Adr Adresse des abzufragenden Moduls (schon *2)
//! \return Status des abgefragten Moduls. Bitkombination aus StatBit_*.
//! \retval -1 falls keine Verbindung zum anderen Modul hergestellt werden konnte.
int16_t GetStatus(uint8_t Adr)
	// liefert Status >= 0 oder -1 für keine Verbindung
	{
	DEBUG_BUSTRANSFER_INIT;
		
	BusWarteFertig();
	
	uint8_t AltVerbPartner = BusVerbPartner;
	BusVerbPartner = Adr; // vorübergehend
	BusAuftrag = Lesen;
	if (BusFrei)
		{
		while (BIT_IS_SET(TWCR, TWSTO))
			;
		SET_BIT(TWCR, TWSTA);
		}
	else
		DEBUG_BUSTRANSFER_KOLLISION;

	BusWarteFertig();

	BusVerbPartner = AltVerbPartner;

	DEBUG_BUSTRANSFER_INITEND;
		
	if (BusErgebnis != Ok)
		return -1;
	else
		return BusEmpfFremdStatus; // wurde von der ISR gefüllt
	}
	

//! Setzt neue eigene TWI-Adresse.
// --------------------------------
//! Prüft vorher, ob die Adresse bereits anderweitig genutzt ist.
//! \todo BusEigenAdrMehrfach prüfen!
//! \param neu Neue Adresse für TWI-Bus, bereits * 2
//! \retval true bei erfolgreicher Adress-Umstellung.
bool BusEigenAdressePruefenUndSetzen(uint8_t neu)
	{
	if (neu >= BusAdrMin
		&& neu <= BusAdrMax
		&& !BIT_IS_SET(neu, 0)
		&& GetStatus(neu) < 0)
		{ //! \todo BusEigenAdrMehrfach prüfen!
		uint8_t SregAlt = SREG;
		cli();
		TWAR = neu;
		if (RundsendEmpfFreig)
			SET_BIT(TWAR, TWGCE);
		SET_BIT(TWCR, TWEA);
		BusEigenAdresse = neu;
		SREG = SregAlt;
		return true;
		}
	else
		{
		uint8_t SregAlt = SREG;
		cli();
		TWAR = 0;
		CLR_BIT(TWCR, TWEA);
		BusEigenAdresse = BusAdrUngueltig;
		SREG = SregAlt;
		return false;
		}
	}


//! Initialisiert die TWI-Schnittstelle.
// --------------------------------------
//! Vorher muss #BusEigenAdresse und #BusEigenAdrMehrfach und
//! #RundsendEmpfFreig gesetzt sein.
//! Funktion prüft NICHT auf Mehrfachverwendung der eigenen Adresse.
void TwiInit()
	{
	// TWI initilaisieren
	BusFrei = true;
	BusAuftrag = Nichts;
	BusErgebnis = Ok;

#define TWI_PSBITS 1
#define TWI_PRESCALER (1<<(2*TWI_PSBITS))

	BusKollisionZaehler = 0;
	BusEmpfPufferLesePos = 0;
	BusEmpfPufferSchreibPos = 0;
	TwiIsrCount = 0;
	TwiWatchdogCount = 0;
 	BusSendeDaten = 0;
	RundsendAnzDaten = 0;
	RundsendPufferPos = 0;

	TWSR = TWI_PSBITS;
	TWBR = ((F_CPU / BusFrequenz) - 16) / (2 * TWI_PRESCALER);
	TWAR = BusEigenAdresse & 0xFE;
	if (RundsendEmpfFreig)
		SET_BIT(TWAR, TWGCE);
	TWCR = (1<<TWINT) | (0<<TWEA) | (0<<TWSTA) | (0<<TWSTO) | (1<<TWEN) | (0<<TWIE);
#ifdef TWAMR
	TWAMR = (BusEigenAdrMehrfach - 1) << 1; // muss Potenz von 2 sein, Standard = 1
#else
	if (BusEigenAdrMehrfach != 1)
		FehlerStop(12);
#endif //def TWAMR
	}


//! Übersetzt eine Wahlziffern-Folge in die TWI-Adresse.
//------------------------------------------------------
//! \param Wahl Wahlziffer bzw. Wahlziffern.
//! \param AnzZiffern Anzahl der Ziffern der Wahlnummer.
//! \return TWI-Adresse zur Wahlziffer(n)

//! \remarks Beispiele zu den Parametern: 
//! \n Ziffern: 3 4 --> Wahl = 34, AnzZiffern = 2 (TWI-Adresse 34 << 1)
//! \n Ziffern: 0 4 --> Wahl = 4, AnzZiffern = 2 (TWI-Adresse 4 << 1)
//! \n Ziffer:   4  --> Wahl = 4, AnzZiffern = 1 (TWI-Adresse 104 << 1)
//! \n Übersetzungsregel:
//! \n einstellig 1 - 9 --> 101 - 109 ; 0 --> 110
//! \n zweistellig 01 - 99 --> 1 - 99 ; 00 --> 100

uint8_t WahlZuAdresse(uint8_t Wahl, uint8_t AnzZiffern)
	{
	if (AnzZiffern == 1 && Wahl <= 9)
		return ((Wahl == 0) ? 110 : (100 + Wahl)) << 1;
	else if (AnzZiffern == 2)
		{
		if (Wahl == 0)
			return 100 << 1;
		else if (Wahl <= 99)
			return Wahl << 1;
		}
	return BusAdrUngueltig; // also bei AnzZiffern != 1 und != 2 oder Wahl > 99
	}


//! Übersetzt eine TWI-Adresse in eine Wahlziffern-Folge.
//------------------------------------------------------
//! \param[in] Adresse TWI-Adresse zur Wahlziffer(n)
//! \param[out] AnzZiffern Anzahl der Ziffern der Wahlnummer.
//! \return Wahlziffer bzw. Wahlziffern.

//! \remarks Beispiele zu den Parametern und Übersetzungsregel siehe WahlZuAdresse().

uint8_t AdresseZuWahl(uint8_t Adresse, uint8_t *AnzZiffern)
	{
	if (Adresse > BusAdrMax || Adresse < BusAdrMin)
		{
		*AnzZiffern = 0;
		return 0;
		}
	else
		{
		Adresse = Adresse >> 1;
		if (Adresse > 100)
			{
			*AnzZiffern = 1;
			return (Adresse >= 110) ? 0 : (Adresse - 100);
			}
		else
			{
			*AnzZiffern = 2;
			return (Adresse == 100) ? 0 : Adresse;
			}
		}
	}


//! Holt aus dem Empfangspuffer den nächsten Code.
// ------------------------------------------------
//! \param[out] Code TWI-Code nach Txp2-Defs.h.
//! \retval false, wenn Empfangspuffer leer ist.
bool GetEmpfByte(uint8_t *Code)
	{
	if (BusEmpfPufferLesePos == BusEmpfPufferSchreibPos)
		return false;
	*Code = BusEmpfPuffer[BusEmpfPufferLesePos++];
	if (BusEmpfPufferLesePos >= EMPF_PUFFER_GROESSE)
		BusEmpfPufferLesePos = 0;
	if (BusEmpfPufferLesePos == BusEmpfPufferSchreibPos)
		CLR_BIT_Status(StatBit_BusKdoEmpfangen);
	return true;
	}
	

#ifndef FUER_TW39
	
//! Prüft, ob Empfangspuffer für TWI-Kommandos leer ist.
//------------------------------------------------------	
bool EmpfPufferLeer()
	{
	return BusEmpfPufferLesePos == BusEmpfPufferSchreibPos;
	}
	
#endif //ndef FUER_TW39

