#ifndef __PORTS_H__

#define __PORTS_H__

//================================================================
// verwendete Pins
//================================================================
//				
//   01: C6  	Reset
//   02: D0 	Taster nach Masse
//   03: D1 	LED rot: High = ein
//   04: D2  	LED gelb: High = ein
//   05: D3  	LED gr¸n: High = ein
//   06: D4  	LED blau: High = ein
//   07: VCC		
//   08: GND		
//   09: B6 	Quarz
//   10: B7 	Quarz
//   11: D5  	frei
//   12: D6   	Eingang FS Eingang: Low = Strom ein
//   13: D7   	Ausgabe FS Motor: High = Maschine ein
//   14: B0  	Ausgabe FS Daten: High = Strom ein = Mark
//   15: B1  	frei
//   16: B2  	frei
//   17: B3 MOSI
//   18: B4 MISO
//   19: B5 SCK 
//   20: AVCC
//   21: AREF
//   22: GND
//   23: C0  					
//   24: C1  
//   25: C2  	
//   26: C3  	
//   27: C4 SDA	
//   28: C5 SCL	


#include "Defports.h"

#ifndef PLATINE_VERSION
#error Platinen-Version in Konfiguration festlegen!
#endif

// Schnittstellen
// --------------

#if PLATINE_VERSION >= 13 // 1.3x
// aktuelle Version

DEFPORTINPULL	(TASTE,		D, 0)	

DEFPORTOUT		(LEDROT, 	D, 1)
DEFPORTOUT		(LEDGELB, 	D, 2)
DEFPORTOUT		(LEDGRUEN, 	D, 3)
DEFPORTOUT		(LEDBLAU, 	D, 4)

DEFPORTOUT		(FS_AUSG,	B, 0) // Schleifenschluﬂ-Ausgabe
DEFPORTOUT		(FS_AKTIV,	D, 7) // Einschaltung (Ausgabe, Optionaler Motor-Schalter
DEFPORTINPULL	(FS_EING, 	D, 6) // Strom-Einlesung

#else

#error Ungueltige Angabe fuer PLATINE_VERSION...

#endif // PLATINE_VERSION 


#endif //ndef __PORTS_H__
