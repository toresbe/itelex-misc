#ifndef __PORTS_H__

#define __PORTS_H__

#include "Defports.h"

#ifndef PLATINE_VERSION
#error Platinen-Version in Konfiguration festlegen!
#endif

// Schnittstellen
// --------------

#if PLATINE_VERSION >= 13 // 1.3x
// aktuelle Version

								// 		  Mega168
								//			 1  Reset
DEFPORTOUT(LEDROT,		D, 0)	//			 2  RXD
DEFPORTOUT(LEDGELB,		D, 1)	//			 3  TXD
DEFPORTOUT(LEDGRUEN,	D, 2)	//			 4
DEFPORTOUT(LEDBLAU,		D, 3)	//			 5
                               	//			 6
								//			 7	VCC
								//			 8	GND
//						B, 6)	//			 9	XTAL1
//			 			B, 7)	//			10	XTAL2
//DEFPORTOUT(			D, 5)	//			11
DEFPORTIN(FS_EING,		D, 6)	//			12
DEFPORTOUT(FS_AUSG,		D, 7)	//			13
//DEFPORTOUT(SIGAUS0,	B, 0)	//			14
//DEFPORTOUT(SIGAUS1, 	B, 1)	//			15
//DEFPORTOUT(SIGAUS2, 	B, 2)	//			16
//DEFPORTOUT(SIGAUS3, 	B, 3)	//			17	MOSI
//DEFPORTOUT(SIGAUS4,	B, 4)	//			18	MISO
//DEFPORTOUT(SIGAUS5,	B, 5)	//			19	SCK
								//			20	AVCC
								//			21	AREF
								//			22	AGND
//DEFPORTIN(SIGEIN,	 	C, 0)	//			23	ADC0
DEFPORTINPULL(TASTE, 	C, 1)	//			24
//DEFPORTOUT(LEDROT, 	C, 2)	//			25
//DEFPORTINPULL(TASTE2,	C, 3)	//			26
//						C, 4)	//			27	SDA
//						C, 5)	//			28	SCL

#else

#error Platinenversion PLATINE_VERSION nicht bekannt...

#endif // PLATINE_VERSION 


#endif //ndef __PORTS_H__
