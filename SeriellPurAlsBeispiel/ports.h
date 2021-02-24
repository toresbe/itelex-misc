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

#ifdef AF_ANRUFSPEICHER
DEFPORTTRI(SWTWI_SDA,	D, 7)
DEFPORTTRI(SWTWI_SCL,   B, 0)
#endif //def AF_ANRUFSPEICHER

DEFPORTOUT(SER_RTS,		D, 3)
DEFPORTIN(SER_CTS,		D, 2)


#elif (PLATINE_VERSION < 21)

#warning "Nur für alte Platinen-Version 1.3 bis 1.x"

DEFPORTINPULL(TASTE,	D, 2)

DEFPORTOUT(LED_ROT,     C, 2)
DEFPORTOUT(LED_GELB, 	C, 3)
DEFPORTOUT(LED_GRUEN,   D, 3)
DEFPORTOUT(LED_BLAU,    D, 4)

#ifdef AF_ANRUFSPEICHER
DEFPORTTRI(SWTWI_SDA,	B, 0)
DEFPORTTRI(SWTWI_SCL,	D, 7)
#endif //def AF_ANRUFSPEICHER

DEFPORTOUT(SER_RTS,		B, 1)
DEFPORTIN(SER_CTS,		B, 2)


#else // PLATINE_VERSION >= 21

DEFPORTINPULL(TASTE,	C, 0)

DEFPORTOUT(LED_ROT,     B, 2)
DEFPORTOUT(LED_GELB, 	C, 1)
DEFPORTOUT(LED_GRUEN,   C, 2)
DEFPORTOUT(LED_BLAU,    C, 3)

#ifdef AF_ANRUFSPEICHER
DEFPORTTRI(SWTWI_SDA,	B, 0)
DEFPORTTRI(SWTWI_SCL,	D, 7)
#endif //def AF_ANRUFSPEICHER

DEFPORTOUT(SER_RTS,		D, 2)
DEFPORTIN(SER_CTS,		D, 3)


#endif 


#define LED_EIN(Farbe) set_LED_ ## Farbe ()

#define LED_AUS(Farbe) clr_LED_ ## Farbe ()


#endif //ndef __PORTS_H__

