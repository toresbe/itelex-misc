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


#include <avr/io.h>
//#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/wdt.h>
#include <inttypes.h>

#include "bits.h"
#include "Ports.h"
//#include "EepromTools.h"

#include "TwiEvents.h"

#include "TxP2-Defs.h"
#include "MsTimer.h"
//#include "BaudotCode.h"
#include "BusKomm.h"
#include "SeriellUmsetz.h"

#include "ClientCommunication.h"


//! Hauptprogramm

int main()
{
	// initializing everything
	
	// main loop
	while ()
	{
		ClientCommunication();
		
		
	}
	
}


