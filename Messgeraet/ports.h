#ifndef __PORTS_H__

#define __PORTS_H__

#include "Defports.h"

// Platinen-Version
// ================

#ifdef FUER_PLAT_ED1000

	DEFPORTINPULLAL(TASTE, 	C, 1)	//			24

	DEFPORTOUT(LED_ROT,		D, 0)	//			 2  RXD
	DEFPORTOUT(LED_GELB,	D, 1)	//			 3  TXD
	DEFPORTOUT(LED_GRUEN,	D, 2)	//			 4
	DEFPORTOUT(LED_BLAU,	D, 3)	//			 5

#else //ndef FUER_PLAT_ED1000

	#define PLATINE_VERSION 13 // 1.3x

	// Schnittstellen
	// --------------

	#if (PLATINE_VERSION < 13)

		#warning "Nur fuer alte Platinen-Version 1.0 bis 1.2"

		#error "... gab es gar nicht"

	#else // PLATINE_VERSION >= 13

		DEFPORTINPULLAL(TASTE,	D, 0)

		DEFPORTOUT(LED_ROT,     D, 1)
		DEFPORTOUT(LED_GELB, 	D, 2)
		DEFPORTOUT(LED_GRUEN,   D, 3)
		DEFPORTOUT(LED_BLAU,    D, 4)

	#endif //else PLATINE_VERSION >= 13

#endif //else ndef FUER_PLAT_ED1000


#endif //ndef __PORTS_H__
