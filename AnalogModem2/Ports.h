#ifndef __PORTS_H__

#define __PORTS_H__

#include "Defports.h"

#ifndef PLATINE_VERSION
#error Platinen-Version in Konfiguration festlegen!
#endif

#if PLATINE_VERSION >= 20

								// 73K221  Mega168
								//			 1  Reset
DEFPORTINPULL(RXD, 		D, 0)	//	22		 2  RXD
DEFPORTOUT(TXD, 		D, 1)	//	21		 3  TXD
DEFPORTOUT(LEDBLAU,		D, 2)	//			 4
DEFPORTINPULL(INT, 		D, 3)	//	17		 5
DEFPORTOUT(EXCLK, 		D, 4)  	//	19		 6
								//			 7	VCC
								//			 8	GND
//						B, 6)	//			 9	XTAL1
DEFPORTOUT(WR, 			B, 7)	//	13		10	XTAL2
DEFPORTOUT(RD, 			D, 5)	//	14		11
DEFPORTOUT(GABEL, 		D, 6)	//			12
DEFPORTINPULL(ANRUF,	D, 7)	//			13
DEFPORTOUT(TELAUS,		B, 0)	//			14
DEFPORTOUT(AD1, 		B, 1)	//	 5		15
DEFPORTOUT(AD2, 		B, 2)	//	 6		16
DEFPORTTRI(AD7, 		B, 3)	//	11		17	MOSI
DEFPORTOUT(AD0, 		B, 4)	//	 4		18	MISO
DEFPORTINPULL(TELAKTIV,	B, 5)	//			19	SCK
								//			20	AVCC
								//			21	AREF
								//			22	AGND
DEFPORTOUT(LEDGRUEN, 	C, 0)	//			23
DEFPORTOUT(LEDGELB, 	C, 1)	//			24
DEFPORTOUT(LEDROT, 		C, 2)	//			25
DEFPORTINPULL(TASTE,	C, 3)	//			26
//						C, 4)	//			27	SDA
//						C, 5)	//			28	SCL

#else

#error Platinenversion PLATINE_VERSION nicht bekannt...

#endif // PLATINE_VERSION < 20


#endif //ndef__PORTS_H__
