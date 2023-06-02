// This program implements a interface to the TxP2-System with
// the main purpose to handle the time critical communication 
// on the TWI bus.
//
// The client side interface may be implemented in different 
// ways (e.g. serial rs232 or SPI).
//
// 
// ================================================================================================================
// Transmission client -> iTelex (TWI) -- see #defines "Txi_*" in ClientComDefs.h
// -----------------------------
//
// from client	| Parameter	  | Note: on TWI Bus	| Comment
//				|             | sent as...			|
// -------------+-------------+---------------------+--------------------------------------------------------------
// 0x00			| none        |		no				| ignored to allow idle communication on client side
// 0x01 to 0x6F | none        | 	own address		| Connect: received address is stored as destination address
//				|             |						|	status of destination is checked, confirmation is sent by
//				|             |						|	0x77 or 0x78
// 0x7D			| <ext>   *1  |   status query		| query status of any iTelex module. address for query is next byte
//				|             |						|	on client side
// 0x7E			| <addr>  *2  |		no				| set internal parameter
//              | <value>     |						|
// 0x7F			| <addr>  *2  |		no				| query internal parameter
//              | <value>     |						|
// 0x80 to 0x9F | none        | as 0x93 / 0x9E		| low 5 bits are used as baudot code to be send. MSB = bit 4 
//				|             |						|	is sent first.
// 0xA3			| none		  |     0xA3			| command: start connection (selected by 0x01 to 0x6F)
// 0xA5			| none		  |     0xA5			| confirmation: printer is running
// 0xA6			| none		  |		0xA6		    | confirmation: ready to get dialling information
// 0xAA			| none		  |		0xAA		    | command: switch off printer and close connection
// 0xAC			| none		  |     0xAC		    | confirmation: printer is switched off, interface is idle
// 0xB0 to 0xB9	| none		  | 0xB0 to 0xB9		| dialed digit
// 0xFD			| none        |		no				| reset
// -------------+-------------+---------------------+--------------------------------------------------------------
// note: generally all codes in range 0xA0 to 0xFC are directly forwarded to the TWI bus

// Explanation of parameters: 
// *1	<ext>	Address of TWI device (decimal values):  (see XXX)
//					00 = invalid extension, 01 to 99 = extensions 01 to 99,
//					100 = extension 00, 101 to 109 = extensions 1 to 9, 110 = extension 0
// *2	<addr>	Index of internal parameter: (see #defines "Txi_Param_*" in ClientComDefs.h)
//					0x00 = own extension (coding see *1)
//					0x01 = default status (only bits 4 and 5 allowed, 0x00 = local unit, 0x10 = restricted local unit, 0x20 = line interface
//					0x40 = error code (bit-mask, see *3)  
//					0x42 = current connected communication partner
//					0x43 = current connection status
// *3			Error code bitmask (see #defines "Txi_ErrFlag_*" in ClientComDefs.h)
//					0x01 = InputBufferOverflow
//					0x02 = OutputBufferOverflow
//					0x04 = AlreadyConnected
//					0x08 = NotConnected
//					0x10 = TwiTimeout
//					0x20 = TwiCodeError (received on TWI bus)
//					0x80 = InvalidCmd (from client)

// ================================================================================================================
// Transmission iTelex (TWI) -> client
//
// to client	| Parameter		| note: received	| Comment
//				|				| on TWI bus 		|
// -------------+---------------+-------------------+--------------------------------------------------------------
// 0x00			| none			|	no				| not allowed. On clinent side 0x00 may be used to signal idle 
// 0x01 to 0x6F | none			| 	yes				| Connect request: received byte is also stored as destination address
// 0x77			| none			|	no				| positive confirmation
// 0x78			| none			|	no				| negative confirmation
// 0x7D   		| <addr>   *2	|	no				| result of module status query.
//		 		| <status> *4	|	no				|  
// 0x7F 		| <addr>   *2	|	no				| result of internal parameter query.
//              | <value>		|                   |
// 0x80 - 0x9F  | 0x93 / 0x9E	| series of 0x93 	| received baudot code 
//				|				|	and / 0x9E		|	First received bit is bit 4 on client side.
// 0xA3			| none		  	|   0xA3			| command: handle further data as baudot codes / incoming call
// 0xA5			| none		  	|   0xA5			| confirmation: interface is ready to receive baudot data
// 0xA6			| none		  	|	0xA6	    	| command: be ready to receive get dialling information
// 0xAA			| none		  	|	0xAA			| command: close connection
// 0xAC			| none		  	|   0xAC		    | confirmation: connection is closed, interface is idle
// 0xB0 to 0xB9	| none		  	| 0xB0 to 0xB9		| dialed digit
// 0xFE			| <code> *3		|		 			| error indicator
// -------------+---------------+-------------------+--------------------------------------------------------------
// note: generally all codes in range 0xA0 to 0xFC received on TWI bus are directly forwarded to the client
                           
// notes:
//	*2 and *3: same as above
//  *4: Bitmask as combination of:
//		- 0x01:
//		- 0x02:
//		- 0x04:
//		- 0x08:
//		- 0x10:
//		- 0x20:
//		- 0x40:
//
//		- 0x80: local device in basic state
//		- 0x90: local device in basic state, does not accept redirected calls
//		- 0xA0: device connecting to the network, dialling information is needed after activation



// Typical examples: The numbering denotes steps, additional letters denote variants. 
// ===============================================================================================================
// Select interface 32 for an internal connection
// ---------------------------------------------------------------------------------------------------------------
// 1. select interface: send 0x20
//		1a)	receive 0x77: selected interface was free and is now reserved -> continue with 2.
//		1b) receive 0x78: selected interface was not free -> state unchanged
//		1c) receive 0x79: selected interface didn't asnwer -> state unchanged
//		1d) receive 0xFE: another error occurred (i.e.: the "own" interface is not free) -> continue with 9.
// 2. activate interface: send 0xA3
//		2a)	receive 0xA6: line interface was selected, waiting for dialling data -> continue with 3.
//		2b) receive 0xA5: printer interface was selected, printer is running -> continue with 4.
//		2c) receive XXXX: printer does not answer
//		2d) receive 0xFE: another error occurred (i.e.: the "own" interface is not free) -> continue with 9.
// 3. dialing: send 0xB3 0xB5: select 53 (local device)
//		3a) no reply: incomplete number
//		3b) receive 0xA5: connection established -> continue with 4.
//		3c) receive XXXX: connection refused (busy / no connection)	-> basic state
// 4. during connection:
//		4a) send 0x80 to 0x9F: Send baudot code for "who are you"
//		4b) receive 0xXXXX: receive Ltrs A B C 
// 5. active closing connection: Send 0xAA
//		5a) receive 0xAC: switch to basic status (waiting) -> basic state
//		5b) timeout: switch to basic status (waiting) -> basic state
// 6. passive closing connection: receive 0xAA
//		6a) switch off local printer, then send 0xAC -> basic state
// 7. incoming call: receive 0xA3
//		7a) activate local device, then send 0xA5 -> continue with 4.
//		7b) if local device not available: send 0xXXX -> basic state
// 8. (free)
// 9. read and clear error information (should work in every state)
//		9a) send 0x7A 0x40, then wait for answer
//		9b) receive 0x7A 0x40 0x08: error code "not connected", then
//		9c) send 0x7E 0x40 0x00 to clear the error code -> basic state
// ----------------------------------------------------------------------------------------------------------------		

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

// Txp2 libs:
#include "TxP2-Defs.h"
//#include "BaudotCode.h"
#include "BusKomm.h"
#include "SeriellUmsetz.h"
#include "KonfigSpeicher.h"
#include "../SvnVersion.h"

// Project includes:
//#include "Ports.h"
#include "ClientComDefs.h"
#include "ClientCommunication.h"


//! Identificator in flash memory
const char PROGMEM Identifier[] = "___itlx_UniIF-" PROGIDZUSATZ "___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";


// Constants
// =========

#define DEBUG_PORT_OUT	PORTB
#define DEBUG_PORT_DDR	DDRB
	// use only Bits 0 to 5 / Mask 0x3F


// internal Eeprom
// ===============

enum { ConfigAddr_OwnAddress = 4 }; //!< copy of #BusEigenAdresse in EEPROM
enum { ConfigAddr_DefaultStatus = 5 }; //!< copy of #DefaultStatus in EEPROM



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

	KonfigSpeicherInit();

	BusEigenAdresse = KonfigLeseByte(ConfigAddr_OwnAddress, 99) & 0xFE;
	if (BusEigenAdresse < BusAdrMin || BusEigenAdresse > BusAdrMax)
		BusEigenAdresse = 99 << 1; // default
	BusEigenAdrMehrfach = 1; // no multi address mode supported yet.
	
	DefaultStatus = (1 << StatBit_Frei) | (KonfigLeseByte(ConfigAddr_DefaultStatus, 0xA0) & 0x30);

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

#ifdef DEBUG_PORT_DDR
	DEBUG_PORT_DDR = 0x3F;
#endif

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
			BusEigenAdresse = val << 1; // gets active after an reset
			KonfigSchreibeByte(ConfigAddr_OwnAddress, BusEigenAdresse);
			break;
			
		case Txi_Param_DefaultStatus:		
			DefaultStatus = (val & 0x30) | (1 << StatBit_Frei);
				// only bits 4 and 5 allowed
			KonfigSchreibeByte(ConfigAddr_DefaultStatus, DefaultStatus);
			break;
			
		case Txi_Param_ErrorCode:			
			ErrorFlags = val; // to reset error flags
			break;
			
		case Txi_Param_CurrentPartner:		
			BusVerbPartner = val << 1; // use this with extreme care!
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
		case Txi_Param_OwnAddress:			return BusEigenAdresse >> 1;
		case Txi_Param_DefaultStatus:		return DefaultStatus;
		case Txi_Param_ErrorCode:			return ErrorFlags;
		case Txi_Param_CurrentPartner:		return BusVerbPartner >> 1;
		case Txi_Param_CurrentStatus:		return Status;
		default:							return 0;
		}
	}


//! Check the TWI code for need to update the Status bits.
// -------------------------------------------------------
//! Is called after successful send of any comamnd code on the TWI bus
//! and after any reception of command codes from the TWI Bus
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

			if (PufferAnzahl(&ClientOutputBuffer) > MaxPuffer - 4)
				return; // not ready to send the result
			
			PufferAusg(&ClientInputBuffer); // deletes command code from buffer
			
			Addr = PufferAusg(&ClientInputBuffer);
			
			Val = GetStatus(Addr << 1); // check status of any unit on the bus
			
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

			if (PufferAnzahl(&ClientOutputBuffer) > MaxPuffer - 4)
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
				BusSenden(Code);

			else
				return; // not yet ready to send the command;
			
			break;

		case Txi_Reset: // performs device reset by using the watchdog
			wdt_enable(WDTO_30MS);
			cli();
			while (true)
				;
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
				PufferSpeich(&ClientOutputBuffer, Code);
				UpdateStatusOnBusCode(Code);
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

	bool BusSendMark = true; 
		// mark signal is default as long as no data is to be sent.
		// will be overwritten by SeriellUmsetzung() if data is beeing sent
			
	SeriellUmsetzung(BusEmpfMark, &BusSendMark);
	
	if (BusFrei && BusAuftrag == Nichts)
		{ // ready to send anything on the TWI
		if (BusSendMark)
			{ // Mark
			if (SentLoopStatus == Space1 || SentLoopStatus == Space2)
				{
				BusSenden(BusKdoMark);
				#ifdef DEBUG_PORT_OUT
					DEBUG_PORT_OUT = 0x01;
				#endif
				}
			else if (SentLoopStatus == Mark1 && TimerVal(&TWICommTimer) > 0)
				{
				BusSenden(BusKdoMarkWdh); 
				#ifdef DEBUG_PORT_OUT
					DEBUG_PORT_OUT = 0x02;
				#endif
				}
			} // Mark
		else
			{ // Space
			if (SentLoopStatus == Mark1 || SentLoopStatus == Mark2)
				{
				BusSenden(BusKdoSpace);
				#ifdef DEBUG_PORT_OUT
					DEBUG_PORT_OUT = 0x04;
				#endif
				}
			else if (SentLoopStatus == Space1 && TimerVal(&TWICommTimer) > 0)
				{
				BusSenden(BusKdoSpaceWdh);
				#ifdef DEBUG_PORT_OUT
					DEBUG_PORT_OUT = 0x08;
				#endif
				}
 
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
				default:
					UpdateStatusOnBusCode(BusSendeDaten);
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
	wdt_disable(); // may be activated later

	pgm_read_byte(Identifier); // Dummy read to force the identifier to be placed in the FLASH.
		
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


