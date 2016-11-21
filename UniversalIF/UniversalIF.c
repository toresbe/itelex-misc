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
// 0xA0 - 0xFF  |	  	yes			| control codes. 0xAC also clears destination address
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
// 0xA0 - 0xFF  | yes, except 0xA9	| control codes. 0xAC also clears destination address.
//				|					|	0xA9 on TWI bus is heartbeat and is not forwarded to client


// standard libs:
#include <avr/io.h>
//#include <avr/pgmspace.h>
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

// Project includes:
//#include "Ports.h"
#include "ClientCommunication.h"


// Constants
// =========



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


//! Buffer for 



static void InitBuffers()
	{
	PufferInit(&ClientInputBuffer);
	PufferInit(&ClientOutputBuffer);
	} // InitBuffers()



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
#ifdef TCCR0A
	TCCR0A = 0;
	TCCR0B = TIMER0_CS; 
	SET_BIT(TIMSK0, TOIE0);
#else
	TCCR0 = TIMER0_CS;
	SET_BIT(TIMSK, TOIE0);
#endif //def TCCR0A

	Status = (1 << StatBit_Frei); // StatBit_SpezialGeraetKennung muss vom Hauptprogramm gesetzt werden
	SeriellUmsetzInit();

	// TWI
	TwiInit();
	
	// Watchdog
	wdt_enable(WDTO_2S);
	
	}


static void ProcessClientToTWI()
	{
	

/*
baudot-codes an Bus:
	if (SerUmSendBitNr == SerUmSendWarte)
		{
		SerUmSendDaten = xxx;
		SerUmSendBitNr = SerUmSendStart;
		}
*/
	
	} // ProcessClientToTWI()


static void ProcessTWItoClient()
	{
	uint8_t Kdo;
	
/*	
	if (GetEmpfByte(&Kdo))
		{
		// Codes for mark and space and heartbeat are already processed
		
		TODO put other codes to the client buffer
		}

		
		
			if (SerUmEmpfBitNr == SerUmEmpfFertig)
				{
				PufferSpeich(&EmpfPuffer, SerUmEmpfDaten);
				SerUmEmpfBitNr = SerUmEmpfWarte;
				}
*/

				
	} // ProcessTWItoClient()


static void DoTWICommunication()
	{
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
					BusSenden(BusKdoSpaceWdh); // TODO small delay
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
					break;
				case BusKdoSpaceWdh:
					SentLoopStatus = Space2;
					break;
				case BusKdoMark:
					SentLoopStatus = Mark1;
					break;
				case BusKdoMarkWdh:
					SentLoopStatus = Mark2;
					break;
				} // switch BusSendeDaten
			} // letzte Bus-Sendung war fehlerfrei
		BusAuftrag = Nichts;
		}

	if (BusVerbPartner > 0 
		&& BusFrei 
		&& BusAuftrag == Nichts 
		&& TimerVal(&TWICommTimer) > 891) // mind. alle 0,891 Sek senden
		{
		BusSenden(BusLebenszeichen);
		StartTimer(&TWICommTimer);
		}
	
	if (BusVerbPartner == 0)
		wdt_reset(); // because no communication is expected
	
	} // DoTWICommunication()




//! Main Programm

int main()
	{
	// initializing everything
	InitPorts();
	
	InitBuffers();
	
	InitClientCom();
	
	// main loop
	while (true)
		{
		DoClientCommunication();
		ProcessClientToTWI();
		DoTWICommunication();
		ProcessTWItoClient();
		}
	} // main()


