#ifndef __PORTS_H__

#define __PORTS_H__

//================================================================
// verwendete Pins
//================================================================
//				
//   01: C6  	Reset
//   02: D0 	bis V1.2 Ausgabe FS Daten: High = Strom ein = Mark
//				ab V1.3 Taster nach Masse
//   03: D1 	bis V1.2 Ausgabe FS Steuerung: High = Maschine ein
//				ab V1.3 LED rot: High = ein
//   04: D2  	ab V1.3 LED gelb: High = ein
//   05: D3  	ab V1.3 LED grün: High = ein
//   06: D4  	ab V1.3 LED blau: High = ein
//   07: VCC		
//   08: GND		
//   09: B6 	Quarz
//   10: B7 	Quarz
//   11: D5  	ab V1.31 Maschine 2 Eingang FS Eingang: Low = Strom ein
//   12: D6   	bis V1.2 Taster nach Masse
//				ab V1.3 Eingang FS Eingang: Low = Strom ein
//   13: D7   	
//				
//   14: B0  	bis V1.2 LED gelb: High = ein
//				ab V1.3 Ausgabe FS Daten: High = Strom ein = Mark
//   15: B1  	bis V1.2 LED grün: High = ein
// 				ab V1.31 Maschine 2 Ausgabe FS Steuerung: High = Maschine ein
//   16: B2  	bis V1.2 LED blau: High = ein
//				ab V1.31 Maschine 2 Ausgabe FS Daten: High = Strom ein = Mark
//   17: B3 MOSI
//   18: B4 MISO
//   19: B5 SCK 
//   20: AVCC
//   21: AREF
//   22: GND
//   23: C0  					
//   24: C1  
//   25: C2  	
//   26: C3  	bis V1.2 Eingang FS Eingang: Low = Strom ein
//   27: C4 SDA	
//   28: C5 SCL	


#include "Defports.h"

#ifndef PLATINE_VERSION
#error Platinen-Version in Konfiguration festlegen!
#endif

// Schnittstellen
// --------------

#if PLATINE_VERSION >= 20

										// 73K221  Mega168
										//			 1  Reset
DEFPORTINPULL	(RXD, 			D, 0)	//	22		 2  RXD
DEFPORTOUT		(TXD, 			D, 1)	//	21		 3  TXD
DEFPORTOUT		(LEDBLAU,		D, 2)	//			 4
DEFPORTINPULL	(INT, 			D, 3)	//	17		 5
DEFPORTOUT		(EXCLK, 		D, 4)  	//	19		 6
										//			 7	VCC
										//			 8	GND
//								B, 6)	//			 9	XTAL1
DEFPORTOUT		(WR, 			B, 7)	//	13		10	XTAL2
DEFPORTOUT		(RD, 			D, 5)	//	14		11
DEFPORTOUT		(SV_EIN, 		D, 6)	//				12
DEFPORTINPULLAL(MODEJMP_POLARITY, D, 7)	//	13
DEFPORTINPULLAL	(TASTEEXT,		B, 0)	//				14
DEFPORTOUT		(AD1, 			B, 1)	//	 5		15
DEFPORTOUT		(AD2, 			B, 2)	//	 6		16
DEFPORTTRI		(AD7, 			B, 3)	//	11		17	MOSI
DEFPORTOUT		(AD0, 			B, 4)	//	 4		18	MISO
DEFPORTINPULLAL	(MODEJMP_ANSWER, B, 5)	//			19	SCK
										//			20	AVCC
										//			21	AREF
										//			22	AGND
DEFPORTOUT		(LEDGRUEN, 		C, 0)	//			23
DEFPORTOUT		(LEDGELB, 		C, 1)	//			24
DEFPORTOUT		(LEDROT, 		C, 2)	//			25
DEFPORTINPULLAL	(TASTE,			C, 3)	//			26
//								C, 4)	//			27	SDA
//								C, 5)	//			28	SCL


#else

#error Platinenversion PLATINE_VERSION nicht implementiert...

#endif // PLATINE_VERSION 


#endif //ndef __PORTS_H__
