// Variant for RS232

// standard libs
#include <avr/io.h>

// sonni's libs
#include "bits.h"

// project libs
#include "ClientCommunication.h"


//! Buffer for data coming from the client interface to be sent to the TWI bus.
TPuffer ClientInputBuffer;

//! Buffer for data to be sent to the client interface
TPuffer ClientOutputBuffer;

//! Any initialisation 
void InitClientCom()
	{
	#define BAUD 9600
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
	}


//! Doing the in- and output on the client interface. Data will be stored in #ClientInputBuffer 
//! and read from #ClientOutputBuffer.
void DoClientCommunication()
	{
	// send data to RS232 port
	if (!PufferLeer(&ClientOutputBuffer) 
		&& BIT_IS_SET(UCSR0A, UDRE0)) // 		&& GetCTS())
		{
		UDR0 = PufferAusg(&ClientOutputBuffer);
		}
	
	// receive data from RS232 port
	if (BIT_IS_SET(UCSR0A, RXC0))
		{
		if (PufferAnzahl(&ClientInputBuffer) < MaxPuffer - 1)
			{
			char c = UDR0;
			PufferSpeich(&ClientInputBuffer, c); 
			}
		else
			; //! \todo set Flag input overflow
		}
	}	
	
