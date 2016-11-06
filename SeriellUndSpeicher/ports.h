// Port-Definitionen für Serielle Schnittstelle im TxP2-System

#ifndef __PORTS_H__

#define __PORTS_H__

#include "Defports.h"


//! \todo Umstellung auf Defports.h


// Platinen-Version
// ================

#ifndef PLATINE_VERSION
#error Platinen-Version in Konfiguration festlegen!
#endif

//#define PLATINE_VERSION 13 // 1.3x


// Schnittstellen
// --------------

#if (PLATINE_VERSION < 13)

#warning "Nur für alte Platinen-Version 1.0 bis 1.2"


DEFPORTINPULL(TASTE,	C, 3)	//			26

DEFPORTOUT(LED_ROT,     C, 0)
DEFPORTOUT(LED_GELB, 	C, 1)
DEFPORTOUT(LED_GRUEN,   C, 2)
DEFPORTOUT(LED_BLAU,    D, 4)

#ifndef OHNE_SPEICHER

#define SWTWI_SDA_OPORT	PORTD
#define SWTWI_SDA_IPORT	PIND
#define SWTWI_SDA_DDR	DDRD
#define SWTWI_SDA_BIT 7

#define SWTWI_SCL_OPORT PORTB
#define SWTWI_SCL_IPORT	PINB
#define SWTWI_SCL_DDR	DDRB
#define SWTWI_SCL_BIT 0

#endif //ndef OHNE_SPEICHER


#define SER_RTS_OPORT PORTD
#define SER_RTS_DDR DDRD
#define SER_RTS_BIT 3

#define SER_CTS_IPORT PIND
#define SER_CTS_DDR DDRD
#define SER_CTS_BIT 2

#define TEST1_OPORT PORTD
#define TEST1_IPORT PIND
#define TEST1_DDR DDRD
#define TEST1_BIT 5


#elif (PLATINE_VERSION < 21)

#warning "Nur für alte Platinen-Version 1.3 bis 1.x"


DEFPORTINPULL(TASTE,	D, 2)

DEFPORTOUT(LED_ROT,     C, 2)
DEFPORTOUT(LED_GELB, 	C, 3)
DEFPORTOUT(LED_GRUEN,   D, 3)
DEFPORTOUT(LED_BLAU,    D, 4)


#ifndef OHNE_SPEICHER

#define SWTWI_SDA_OPORT	PORTB
#define SWTWI_SDA_IPORT	PINB
#define SWTWI_SDA_DDR	DDRB
#define SWTWI_SDA_BIT 0

#define SWTWI_SCL_OPORT PORTD
#define SWTWI_SCL_IPORT	PIND
#define SWTWI_SCL_DDR	DDRD
#define SWTWI_SCL_BIT 7

#endif //ndef OHNE_SPEICHER


#define SER_RTS_OPORT PORTB
#define SER_RTS_DDR DDRB
#define SER_RTS_BIT 1

#define SER_CTS_IPORT PINB
#define SER_CTS_DDR DDRB
#define SER_CTS_BIT 2

#define TEST1_OPORT PORTD
#define TEST1_IPORT PIND
#define TEST1_DDR DDRD
#define TEST1_BIT 5

#else // PLATINE_VERSION >= 21


DEFPORTINPULL(TASTE,	C, 0)

DEFPORTOUT(LED_ROT,     B, 2)
DEFPORTOUT(LED_GELB, 	C, 1)
DEFPORTOUT(LED_GRUEN,   C, 2)
DEFPORTOUT(LED_BLAU,    C, 3)


#ifndef OHNE_SPEICHER

#define SWTWI_SDA_OPORT	PORTB
#define SWTWI_SDA_IPORT	PINB
#define SWTWI_SDA_DDR	DDRB
#define SWTWI_SDA_BIT 0

#define SWTWI_SCL_OPORT PORTD
#define SWTWI_SCL_IPORT	PIND
#define SWTWI_SCL_DDR	DDRD
#define SWTWI_SCL_BIT 7

#endif //ndef OHNE_SPEICHER


#define SER_RTS_OPORT PORTD
#define SER_RTS_DDR DDRD
#define SER_RTS_BIT 2

#define SER_CTS_IPORT PIND
#define SER_CTS_DDR DDRD
#define SER_CTS_BIT 3

#define TEST1_OPORT PORTD
#define TEST1_IPORT PIND
#define TEST1_DDR DDRD
#define TEST1_BIT 5


#endif 


#define LED_EIN(Farbe) set_LED_ ## Farbe ()

#define LED_AUS(Farbe) clr_LED_ ## Farbe ()



#endif //ndef __PORTS_H__

