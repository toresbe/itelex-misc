// Port-Definitionen für Serielle Schnittstelle im i-Telex-System

#ifndef __PORTS_H__

#define __PORTS_H__

#include "Defports.h"


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

DEFPORTINPULLAL	(TASTE,		C, 3)	//			26

DEFPORTOUT		(LEDROT,    C, 0)
DEFPORTOUT		(LEDGELB, 	C, 1)
DEFPORTOUT		(LEDGRUEN,  C, 2)
DEFPORTOUT		(LEDBLAU,   D, 4)

DEFPORTOUT		(SV_EIN,	B, 5) // Ausgang für Zusatzschaltung zur Stromversorgung des Fernschreibers
DEFPORTINPULLAL	(TASTEEXT,	B, 4) // Eingang für externe Taste zur Aktivierung

DEFPORTTRI		(SWTWI_SDA,	D, 7)
DEFPORTTRI		(SWTWI_SCL, B, 0)

DEFPORTOUT		(SER_RTS,	D, 3)
DEFPORTIN		(SER_CTS,	D, 2)


#elif (PLATINE_VERSION < 21)

#warning "Nur fuer alte Platinen-Version 1.3 bis 1.x"

DEFPORTINPULLAL	(TASTE,		D, 2)

DEFPORTOUT		(LEDROT,    C, 2)
DEFPORTOUT		(LEDGELB, 	C, 3)
DEFPORTOUT		(LEDGRUEN,  D, 3)
DEFPORTOUT		(LEDBLAU,   D, 4)

DEFPORTOUT		(SV_EIN,	B, 5) // Ausgang für Zusatzschaltung zur Stromversorgung des Fernschreibers
DEFPORTINPULLAL	(TASTEEXT,	B, 4) // Eingang für externe Taste zur Aktivierung

DEFPORTTRI		(SWTWI_SDA,	B, 0)
DEFPORTTRI		(SWTWI_SCL,	D, 7)

DEFPORTOUT		(SER_RTS,	B, 1)
DEFPORTIN		(SER_CTS,	B, 2)

#else // PLATINE_VERSION >= 21

DEFPORTINPULLAL	(TASTE,		C, 0)

DEFPORTOUT		(LEDROT,    B, 2)
DEFPORTOUT		(LEDGELB, 	C, 1)
DEFPORTOUT		(LEDGRUEN,  C, 2)
DEFPORTOUT		(LEDBLAU,   C, 3)

DEFPORTOUT		(SV_EIN,	B, 5) // Ausgang für Zusatzschaltung zur Stromversorgung des Fernschreibers
DEFPORTINPULLAL	(TASTEEXT,	B, 4) // Eingang für externe Taste zur Aktivierung

DEFPORTTRI		(SWTWI_SDA,	B, 0)
DEFPORTTRI		(SWTWI_SCL,	D, 7)

DEFPORTOUT		(SER_RTS,	D, 2)
DEFPORTIN		(SER_CTS,	D, 3)

#endif 

#endif //ndef __PORTS_H__

