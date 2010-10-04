// Port-Definitionen für Serielle Schnittstelle im TxP2-System

#ifndef __PORTS_H__

#define __PORTS_H__


// Platinen-Version
// ================

#define PLATINE_VERSION 13 // 1.3x


// Schnittstellen
// --------------

#if (PLATINE_VERSION < 13)

#warning "Nur für alte Platinen-Version 1.0 bis 1.2"

#define TAST_PORT PORTC
#define TAST_IPORT PINC
#define TAST_BIT 3

#define LED_ROT_PORT PORTC
#define LED_ROT_DDR DDRC
#define LED_ROT_BIT 0

#define LED_GELB_PORT PORTC
#define LED_GELB_DDR DDRC
#define LED_GELB_BIT 1

#define LED_GRUEN_PORT PORTC
#define LED_GRUEN_DDR DDRC
#define LED_GRUEN_BIT 2

#define LED_BLAU_PORT PORTD
#define LED_BLAU_DDR DDRD
#define LED_BLAU_BIT 4


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


#else // PLATINE_VERSION >= 13


#define TAST_PORT PORTD
#define TAST_IPORT PIND
#define TAST_BIT 2

#define LED_ROT_PORT PORTC
#define LED_ROT_DDR DDRC
#define LED_ROT_BIT 2

#define LED_GELB_PORT PORTC
#define LED_GELB_DDR DDRC
#define LED_GELB_BIT 3

#define LED_GRUEN_PORT PORTD
#define LED_GRUEN_DDR DDRD
#define LED_GRUEN_BIT 3

#define LED_BLAU_PORT PORTD
#define LED_BLAU_DDR DDRD
#define LED_BLAU_BIT 4


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

#endif 


#define LED_EIN(LED) SET_BIT(LED_ ## LED ## _PORT, LED_ ## LED ## _BIT)

#define LED_AUS(LED) CLR_BIT(LED_ ## LED ## _PORT, LED_ ## LED ## _BIT)


#endif //ndef __PORTS_H__

