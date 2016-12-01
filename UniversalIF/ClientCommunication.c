// Variant for RS232

// standard libs
#include <avr/io.h>

// sonni's libs
#include "bits.h"

// project libs
#include "ClientComDefs.h"
#include "ClientCommunication.h"


//! Buffer for data coming from the client interface to be sent to the TWI bus.
TPuffer ClientInputBuffer;

//! Buffer for data to be sent to the client interface
TPuffer ClientOutputBuffer;


#ifdef HEX_FORMAT

//! Input processing of hex values. If < 0 waiting for first nibble.
int8_t HexNibbleIn;

//! Output processing for hex values. If >= 0 the second nibble is the next to be sent.
int8_t HexNibbleOut;

#endif //def HEX_FORMAT


#ifdef HEX_FORMAT

static int8_t CharToHexNibble(char c)
	{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
	}


static char HexNibbleToChar(uint8_t x)
	{
	if (x >= 10)
		return 'A' + x - 10;
	else
		return '0' + x;
	}
	
#endif //def HEX_FORMAT


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
	if (BIT_IS_SET(UCSR0A, UDRE0)) // 		&& GetCTS())
		{
#ifdef HEX_FORMAT
		if (HexNibbleOut >= 0) // pending second nibble to be sent
			{
			UDR0 = HexNibbleToChar(HexNibbleOut);
			HexNibbleOut = -1;
			}
		else if (!PufferLeer(&ClientOutputBuffer))
			{
			uint8_t x = PufferAusg(&ClientOutputBuffer);
			UDR0 = HexNibbleToChar(x >> 4); // upper nibble first
			HexNibbleOut = x & 0xF; // store lower nibble 
			}
#else // binary in / out
		if (!PufferLeer(&ClientOutputBuffer))
			{
			UDR0 = PufferAusg(&ClientOutputBuffer);
			}
#endif //else !HEX_FORMAT
		} // if (BIT_IS_SET(UCSR0A, UDRE0))
	
	// receive data from RS232 port
	if (BIT_IS_SET(UCSR0A, RXC0))
		{
		if (PufferAnzahl(&ClientInputBuffer) < MaxPuffer - 1)
			{
#ifdef HEX_FORMAT
			char c = UDR0;
			int8_t x = CharToHexNibble(c);
			if (x < 0) // not a valid hex nibble
				{
				if (HexNibbleIn >= 0) // a first nibble stored
					{
					PufferSpeich(&ClientInputBuffer, HexNibbleIn);
					HexNibbleIn = -1;
					}
					
				// if possible echo 'invalid' character 
				if (BIT_IS_SET(UCSR0A, UDRE0))
					UDR0 = c;
				}
			else // valid hex nibble
				{ 
				if (HexNibbleIn >= 0) // a first nibble stored
					{
					PufferSpeich(&ClientInputBuffer, (HexNibbleIn << 4) | x);
					HexNibbleIn = -1;
					}
				else
					HexNibbleIn = x; // store first nibble
				}
#else // binary in / out
			char c = UDR0;
			PufferSpeich(&ClientInputBuffer, c); 
#endif //else !HEX_FORMAT
			}
			
		else // character received and not enough space in input buffer
			RaiseError(Txi_ErrFlag_InputBufferOverflow);
		}
	}	
	
