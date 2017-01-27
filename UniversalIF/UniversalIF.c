// This program implements a interface to the TxP2-System with
// the main purpose to handle the time critical communication 
// on the TWI bus.
//
// The client side interface may be implemented in different 
// ways (e.g. serial rs232 or SPI).
//
// 
// Transmission client -> TxP2 (TWI)
//
// from client	| Send to TWI Bus 	| Comment
// -------------+-------------------+--------------------------------------------------------------
// 0x00			|		no			| ignored to allow idle communication on client side
// 0x01 - 0x6F  | 	own address		| Connect: received address is stored as destination address
//				|					|	status of destination is checked, confirmation is sent by
//				|					|	0x77 or 0x78
// 0x7D			|   status query	| query status of any TxP2 module. address for query is next byte
//				|					|	on client side
// 0x7E			|		no			| set internal parameter
// 0x7F			|		no			| query internal parameter
// 0x80 - 0x9F  | as 0x93 / 0x9E	| low 5 bits are used as baudot code to be send. MSB = bit 4 
//				|					|	is sent first.
// 0xA0 - 0xFE  |	  	yes			| control codes. 0xAC also clears destination address
// 0xFF			|					| reset
//
// Transmission TxP2 (TWI) -> client
// to client	| Orig. on TWI bus 	| Comment
// -------------+-------------------+--------------------------------------------------------------
// 0x00			|		no			| not allowed. On clinent side 0x00 may be used to signal idle 
// 0x01 - 0x6F  | 		yes			| Connect: received number is also stored as destination address
// 0x77			|		no			| positive confirmation
// 0x78			|		no			| negative confirmation
// 0x7D 		|		no			| 'header' for result of module status query. 
// 0x7F 		|		no			| 'header' for result of internal parameter query.
// 0x80 - 0x9F  | 0x93 / 0x9E		| received baudot code 
//				|					|	First received bit is bit 4 on client side.
// 0x80 - 0x9F  |	  	no			| other codes in this range are ignored
// 0xA0 - 0xFE  | yes, except 0xA9	| control codes. 0xAC also clears destination address.
//				|					|	0xA9 on TWI bus is heartbeat and is not forwarded to client
// 0xFF			|		no			| error indicator


// standard libs:
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/wdt.h>
#include <inttypes.h>

// sonni's libs:
#include "bits.h"
#include "timercs.h"
#include "TwiEvents.h"
#include "MsTimer.h"
//#include "EepromTools.h"

// Txp2 libs:
#include "TxP2-Defs.h"
//#include "BaudotCode.h"
#include "BusKomm.h"
#include "SeriellUmsetz.h"
#include "../SvnVersion.h"

// Project includes:
//#include "Ports.h"
#include "ClientComDefs.h"
#include "ClientCommunication.h"


//! Identificator in flash memory
const char PROGMEM Identifier[] = "___itlx_UniIF-" PROGIDZUSATZ "___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";


// Constants
// =========



// internal Eeprom
// ===============

EEMEM uint8_t Spacer[20]; //!< Start of EEPROM sometimes disturbed
EEMEM uint8_t OwnAddress_EE = 99 << 1; //!< copy of #BusEigenAdresse in EEPROM
EEMEM uint8_t DefaultStatus_EE = 0xB0; //!< copy of #DefaultStatus in EEPROM


// Variables
// =========

//! Speichert, welcher Pegelzustand der Gegenstelle gemeldet wurde.
static volatile enum { Mark1, 	//!< Es wurde BusKdoMark gesendet, aber noch nicht BusKdoMarkWdh.
					   Mark2,	//!< Es wurde BusKdoMark und BusKdoMarkWdh gesendet.
					   Space1,  //!< Es wurde BusKdoSpace gesendet, aber noch nicht BusKdoSpaceWdh.
					   Space2   //!< Es wurde BusKdoSpace und BusKdoSpaceWdh gesendet.
					   } SentLoopStatus = Mark2; 

					   
//! Measures time for Heartbeat and other generated commands on the TWI bus.					   
static TMsTimer TWICommTimer;

//! Active Error flags (to be reset by command #Txi_SetParam)
uint8_t ErrorFlags;

//! Error indication status
enum {
	EiNormal, //!< no error ocurred or error already sent to client
	EiSent, //!< error ocurred and stored in buffer, but not sent to client
	EiPending //!< error occurred but not stored in buffer due to overflow
	} ErrorIndicationStatus;


//! Default status when idle. Only bits #StatBit_SpezialGeraetKennung and #StatBit_Leitung are allowed.
uint8_t DefaultStatus;


// Error Flags (Bits)


// Actually used parameters


static void InitVariables()
	{
	// Init from EEPROM
	// ----------------
	BusEigenAdresse = eeprom_read_byte(&Spacer[0]) & 0xFE;
		// Dummy Read to "use" Spacer

	BusEigenAdresse = eeprom_read_byte(&OwnAddress_EE) & 0xFE;
	if (BusEigenAdresse < BusAdrMin || BusEigenAdresse > BusAdrMax)
		BusEigenAdresse = 99 << 1; // default
	BusEigenAdrMehrfach = 1; // no multi Adress mode supported yet.
	
	DefaultStatus = (1 << StatBit_Frei) | (eeprom_read_byte(&DefaultStatus_EE) & 0x30);
	Status = DefaultStatus;
	
	// Init other variables (static)
	// -----------------------------
	PufferInit(&ClientInputBuffer);
	PufferInit(&ClientOutputBuffer);
	
	ErrorIndicationStatus = EiNormal;

	ErrorFlags = 0;

	BusEmpfMark = true;
	SentLoopStatus = Mark2;
	
	} // InitVariables()



#define TIMER0_CS TCCR_DIV(0, 8)
#define TIMER0_PRESCALER 8
#define TIMER0_FREQ (F_CPU / TIMER0_PRESCALER)

// f_Timer0OVF = (f_CPU / Prescaler) / (256 - Startwert)
// --> Startwert = 256 - (f_CPU / Prescaler) / f_Timer0OVF

#define TIMER0_OVFFREQ 10000
#define TIMER0_START (256 - TIMER0_FREQ / TIMER0_OVFFREQ)


static void InitPorts()
	{
	// Timer
/* i don't need it, do i???
#ifdef TCCR0A
	TCCR0A = 0;
	TCCR0B = TIMER0_CS; 
	SET_BIT(TIMSK0, TOIE0);
#else
	TCCR0 = TIMER0_CS;
	SET_BIT(TIMSK, TOIE0);
#endif //def TCCR0A
*/

	SeriellUmsetzInit();

	// TWI
	TwiInit(); // but it's not activated yet
	
	// Timer
	MsTimerInit();

	// Watchdog
	//wdt_enable(WDTO_2S); //! \todo check if software watchdog would be a better solution
	
	}


static void TwiActivate()
	{
	TMsTimer Timer;
	
	StartTimer(&Timer);
	while (TimerVal(&Timer) < 100)
		;
	
	TWCR = (1<<TWINT) | (0<<TWEA) | (0<<TWSTA) | (1<<TWSTO) | (0<<TWEN) | (0<<TWIE);

	while (TimerVal(&Timer) < 200)
		;
		
	TWCR = (1<<TWINT) | (1<<TWEA) | (0<<TWSTA) | (0<<TWSTO) | (1<<TWEN) | (1<<TWIE);
	}

	
void RaiseError(uint8_t errflags)
	{
	ErrorFlags |= errflags;
	if (ErrorIndicationStatus == EiNormal)
		{
		if (PufferVoll(&ClientOutputBuffer))
			ErrorIndicationStatus = EiPending;
		else
			{
			PufferSpeich(&ClientOutputBuffer, Txi_Error); 
			ErrorIndicationStatus = EiSent;
			}
		}
	}

	
static void SetParameter(uint8_t addr, uint8_t val)
	{
	switch (addr)
		{
		case Txi_Param_OwnAddress:			
			BusEigenAdresse = val & 0xFE; // gets active after an reset
			break;
			
		case Txi_Param_DefaultStatus:		
			DefaultStatus = (val & 0x30) | (1 << StatBit_Frei);
				// only bits 4 and 5 allowed
			break;
			
		case Txi_Param_ErrorCode:			
			ErrorFlags = val; // to reset them
			break;
			
		case Txi_Param_CurrentPartner:		
			BusVerbPartner = val & 0xFE; // use this with extreme care!
			break;
			
		case Txi_Param_CurrentStatus:		
			Status = val; // use this with extreme care!
			break;
			
		default:							
			break; // nothing
		}
	}


static uint8_t GetParameter(uint8_t addr)
	{
	switch (addr)
		{
		case Txi_Param_OwnAddress:			return BusEigenAdresse;
		case Txi_Param_DefaultStatus:		return DefaultStatus;
		case Txi_Param_ErrorCode:			return ErrorFlags;
		case Txi_Param_CurrentPartner:		return BusVerbPartner;
		case Txi_Param_CurrentStatus:		return Status;
		default:							return 0;
		}
	}


//! Check the TWI code for need to update the Status bits.

static void UpdateStatusOnBusCode(uint8_t Code)
	{
	switch (Code)
		{
		case Txi_ActivateAck:
			SET_BIT_Status(StatBit_Verbunden);
			SET_BIT_Status(StatBit_FsBefBetrieb);
			SET_BIT_Status(StatBit_FsMeldBetrieb);
			break;
			
		case Txi_DisconnAck:
			BusVerbPartner = 0;
			Status = DefaultStatus | (1 << StatBit_Frei); // restore to standard value
			break;
			
		}
	}


static void ProcessClientToTWI()
	{
	uint8_t Code;
	uint8_t Addr;
	int16_t Val;
	
	if (PufferLeer(&ClientInputBuffer))
		return; // nothing to do
	
	Code = PufferZeig(&ClientInputBuffer); 
		// this function does not remove the code from the buffer.
		// at the end of this function ProcessClientToTWI it will 
		// be deleted, so insert a return statement if the command
		// is not processed completely or if the processed data 
		// is deleted specific
		
	switch (Code)
		{
		case Txi_Idle: // should never be put into the buffer.
			break;
			
		case Txi_ConnectMin ... Txi_ConnectMax:
			if (BusVerbPartner != 0)
				RaiseError(Txi_ErrFlag_AlreadyConnected);
			else
				{
				Status = 0; // not connected
				
				BusVerbPartner = Code << 1; 
				BusSenden(BusEigenAdresse >> 1); 
					// internally the free status of the unit to be connected is checked.
					
				BusWarteFertig();

				StartTimer(&TWICommTimer);

				if (BusErgebnis == Ok)
					{ // successful connected
					PufferSpeich(&ClientOutputBuffer, Txi_ConnectOK);
					}
				else 
					{ // unit has answered with 'busy' OR has not answered at all.
					if (BusErgebnis == Besetzt)				
						PufferSpeich(&ClientOutputBuffer, Txi_ConnectBusy);
					else
						PufferSpeich(&ClientOutputBuffer, Txi_NoAnswer);
					BusErgebnis = Ok; // to avoid later problems
					BusVerbPartner = 0;
					Status = DefaultStatus | (1 << StatBit_Frei); // restore to standard value
					}
				}
			
			break;
		
		case Txi_QueryStatus:
			if (PufferAnzahl(&ClientInputBuffer) < 2)
				return; // needs at least code + address

			if (PufferAnzahl(&ClientOutputBuffer) < MaxPuffer - 4)
				return; // not ready to send the result
			
			PufferAusg(&ClientInputBuffer); // deletes command code from buffer
			
			Addr = PufferAusg(&ClientInputBuffer);
			
			Val = GetStatus(Addr); // check status of any unit on the bus
			
			if (Val >= 0)
				{
				PufferSpeich(&ClientOutputBuffer, Txi_QueryStatus);
				PufferSpeich(&ClientOutputBuffer, Addr);
				PufferSpeich(&ClientOutputBuffer, Val & 0x0FF);
				}	
			else
				PufferSpeich(&ClientOutputBuffer, Txi_NoAnswer);
				
			return; // not break, because all data already removed from the input buffer.
		
		case Txi_SetParam:
			if (PufferAnzahl(&ClientInputBuffer) < 3)
				return; // needs at least code + param-index + value

			PufferAusg(&ClientInputBuffer); // deletes command code from buffer
			
			Addr = PufferAusg(&ClientInputBuffer);
			
			SetParameter(Addr, PufferAusg(&ClientInputBuffer));
			
			return; // not break, because all data already removed from the input buffer.
		
		case Txi_QueryParam:
			if (PufferAnzahl(&ClientInputBuffer) < 2)
				return; // needs at least code + address

			if (PufferAnzahl(&ClientOutputBuffer) < MaxPuffer - 4)
				return; // not ready to send the result
			
			PufferAusg(&ClientInputBuffer); // deletes command code from buffer
			
			Addr = PufferAusg(&ClientInputBuffer);
			
			PufferSpeich(&ClientOutputBuffer, Txi_QueryParam);
			PufferSpeich(&ClientOutputBuffer, Addr);
			PufferSpeich(&ClientOutputBuffer, GetParameter(Addr));
			
			return; // not break, because all data already removed from the input buffer.
	
		case Txi_PrintcodeMin ... Txi_PrintcodeMax:
			if (BusVerbPartner == 0)
				RaiseError(Txi_ErrFlag_NotConnected);
			else if (SerUmSendBitNr == SerUmSendWarte)
				{
				SerUmSendDaten = Code & 0x1F; 
				SerUmSendBitNr = SerUmSendStart;
				}
			else 
				return; // not yet ready to process the input.

			break;
			
		case Txi_DirectMin ... Txi_DirectMax:
			if (BusVerbPartner == 0)
				RaiseError(Txi_ErrFlag_NotConnected);
			else if (SerUmSendBitNr == SerUmSendWarte && BusFrei && BusAuftrag == Nichts)
				{
				BusSenden(Code);
				UpdateStatusOnBusCode(Code);
				}
			else
				return; // not yet ready to send the command;
			
			break;

		case Txi_Error:
			// TODO: Reset
			break;
			
		default: // unknown code
			RaiseError(Txi_ErrFlag_InvalidCmd);
			break;

		} // switch (Code)
	
	PufferAusg(&ClientInputBuffer); // deletes the processed code
	
	} // ProcessClientToTWI()


static void ProcessTWItoClient()
	{
	uint8_t Code;
	
	if (PufferVoll(&ClientOutputBuffer))
		return; // not enough space to store the result
	
	if (ErrorIndicationStatus == EiPending)
		{
		PufferSpeich(&ClientOutputBuffer, Txi_Error); 
		ErrorIndicationStatus = EiSent;
		return; // no further processing because buffer may be full now.
		}
	
	if (SerUmEmpfBitNr == SerUmEmpfFertig)
		{
		PufferSpeich(&ClientOutputBuffer, SerUmEmpfDaten | Txi_PrintcodeMin);
		SerUmEmpfBitNr = SerUmEmpfWarte;
		return; // no further processing because buffer may be full now.
		}
		
	if (GetEmpfByte(&Code))
		{
		switch (Code)
			{
			case Txi_ConnectMin ... Txi_ConnectMax:
				if (BusVerbPartner == 0)
					{ // new incoming connection
					BusVerbPartner = Code << 1;
					Status = (1 << StatBit_AngerufenBelegt);
					PufferSpeich(&ClientOutputBuffer, Code);
					}	
				else // already another existing connection
					{
					RaiseError(Txi_ErrFlag_TwiCodeError);
					}
				break;
				
			case Txi_DirectMin ... Txi_DirectMax:
				if (BusVerbPartner != 0)
					{ // standard procedure for incoming command code
					PufferSpeich(&ClientOutputBuffer, Code);
					UpdateStatusOnBusCode(Code);
					}	
				else // BusVerbPartner == 0
					{
					RaiseError(Txi_ErrFlag_TwiCodeError);
					}
				break;

			default:
				RaiseError(Txi_ErrFlag_TwiCodeError);
				break;
			
			} // switch (Code)

		return; // no further processing because buffer may be full now.
		} // GetEmpfByte(&Code)

	// reset status for error display if error code was successfully sent to client
	if (ErrorIndicationStatus == EiSent && PufferLeer(&ClientOutputBuffer))
		ErrorIndicationStatus = EiNormal;
	
	} // ProcessTWItoClient()


static void DoTWICommunication()
	{
	if (BusVerbPartner == 0)
		{
		wdt_reset(); // because no communication is expected
		return; // incoming data is handled in ProcessTWItoClient
		}

	if (BusEmpfMark)
		SET_BIT_Status(StatBit_FsBefEin);
	else
		CLR_BIT_Status(StatBit_FsBefEin);
	
	// prepare sending of mark / space

	bool BusSendMark = true; // mark signal is default as long as no data is to be sent
			
	SeriellUmsetzung(BusEmpfMark, &BusSendMark);
	
	if (BusFrei && BusAuftrag == Nichts)
		{ // ready to send anything on the TWI
		if (BusSendMark)
			{ // Mark
			if (SentLoopStatus == Space1 || SentLoopStatus == Space2)
				BusSenden(BusKdoMark);
			else if (SentLoopStatus == Mark1 && TimerVal(&TWICommTimer) > 0)
				BusSenden(BusKdoMarkWdh); 
			} // Mark
		else
			{ // Space
			if (SentLoopStatus == Mark1 || SentLoopStatus == Mark2)
				BusSenden(BusKdoSpace);
			else if (SentLoopStatus == Space1 && TimerVal(&TWICommTimer) > 0)
				BusSenden(BusKdoSpaceWdh); 
			} // Space

		} // Bus ist sendefähig	

	if (BusAuftrag == Fertig)
		{ // letzte Sendung wurde abgeschlossen
		StartTimer(&TWICommTimer);
		if (BusErgebnis == Ok)
			{
			switch (BusSendeDaten)
				{
				case BusKdoSpace:
					SentLoopStatus = Space1;
					CLR_BIT_Status(StatBit_FsMeldEin);
					break;
				case BusKdoSpaceWdh:
					SentLoopStatus = Space2;
					break;
				case BusKdoMark:
					SentLoopStatus = Mark1;
					SET_BIT_Status(StatBit_FsMeldEin);
					break;
				case BusKdoMarkWdh:
					SentLoopStatus = Mark2;
					break;
				} // switch BusSendeDaten
			} // command successfully sent on twi bus
		BusAuftrag = Nichts;
		}

	if (BusFrei 
		&& BusAuftrag == Nichts 
		&& TimerVal(&TWICommTimer) > 891) // mind. alle 0,891 Sek senden
		{
		BusSenden(BusLebenszeichen);
		StartTimer(&TWICommTimer);
		}
	
	} // DoTWICommunication()

	

//! Main Programm

int main()
	{
	// initializing everything
	InitVariables();

	InitPorts();
	
	InitClientCom();

	sei();
	
	TwiActivate();
	
	while (true)
		{
		DoClientCommunication();
		ProcessClientToTWI();
		DoTWICommunication();
		ProcessTWItoClient();
		}
	} // main()


