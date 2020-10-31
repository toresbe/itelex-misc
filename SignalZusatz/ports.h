#ifndef __PORTS_H__

#define __PORTS_H__

#include "Defports.h"

// Platinen-Version
// ================

#define PLATINE_VERSION 13 // 1.3x


// Schnittstellen
// --------------

#if (PLATINE_VERSION < 13)

#warning "Nur fuer alte Platinen-Version 1.0 bis 1.2"

#error "... gab es gar nicht"

#else // PLATINE_VERSION >= 13

DEFPORTINPULLAL(TASTE,	D, 0)

#define LED_ROT_PORT PORTD
#define LED_ROT_DDR DDRD
#define LED_ROT_BIT 1

#define LED_GELB_PORT PORTD
#define LED_GELB_DDR DDRD
#define LED_GELB_BIT 2

#define LED_GRUEN_PORT PORTD
#define LED_GRUEN_DDR DDRD
#define LED_GRUEN_BIT 3

#define LED_BLAU_PORT PORTD
#define LED_BLAU_DDR DDRD
#define LED_BLAU_BIT 4


#endif

#define LED_EIN(LED) SET_BIT(LED_ ## LED ## _PORT, LED_ ## LED ## _BIT)

#define LED_AUS(LED) CLR_BIT(LED_ ## LED ## _PORT, LED_ ## LED ## _BIT)

#endif //ndef __PORTS_H__
