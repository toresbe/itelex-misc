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
//   13: D7   	bis V1.2 LED rot: High = ein
//				ab V1.3 Ausgabe FS Steuerung: High = Maschine ein
//   14: B0  	bis V1.2 LED gelb: High = ein
//				ab V1.3 Ausgabe FS Daten: High = Strom ein = Mark
//   15: B1  	bis V1.2 LED grün: High = ein
// 				ab V1.31 Maschine 2 Ausgabe FS Steuerung: High = Maschine ein
//   16: B2  	bis V1.2 LED blau: High = ein
//				ab V1.31 Maschine 2 Ausgabe FS Daten: High = Strom ein = Mark
//   17: B3 MOSI
//   18: B4 MISO	option Ausgang Energieversorgung Fs einschalten
//   19: B5 SCK 	option Taste Energieversorgung Fs einschalten
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

#ifdef V10

// für Platinenversion Seriell ab 2.00, TODO andere noch definieren


DEFPORTINPULLAL	(TASTE,		C, 0)	

DEFPORTOUT		(LEDROT, 	B, 2)
DEFPORTOUT		(LEDGELB, 	C, 1)
DEFPORTOUT		(LEDGRUEN, 	C, 2)
DEFPORTOUT		(LEDBLAU, 	C, 3)

DEFPORTOUT		(FS_AUSG,	D, 1) // Schleifenschluß-Ausgabe
DEFPORTOUT		(FS_AKTIV,	D, 2) // Einschaltung (Ausgabe, Polarität der Schleife)
DEFPORTINPULL	(FS_EING, 	D, 0) // Strom-Einlesung

DEFPORTOUT		(SV_EIN,	B, 5) // Ausgang für Zusatzschaltung zur Stromversorgung des Fernschreibers
DEFPORTINPULLAL	(TASTEEXT,	B, 4) // Eingang für externe Taste zur Aktivierung


#else // not defined V10

#if PLATINE_VERSION >= 13 // 1.3x
// aktuelle Version

DEFPORTINPULLAL	(TASTE,		D, 0)	

DEFPORTOUT		(LEDROT, 	D, 1)
DEFPORTOUT		(LEDGELB, 	D, 2)
DEFPORTOUT		(LEDGRUEN, 	D, 3)
DEFPORTOUT		(LEDBLAU, 	D, 4)

DEFPORTOUT		(FS_AUSG,	B, 0) // Schleifenschluß-Ausgabe
DEFPORTOUT		(FS_AKTIV,	D, 7) // Einschaltung (Ausgabe, Polarität der Schleife)
DEFPORTINPULL	(FS_EING, 	D, 6) // Strom-Einlesung

DEFPORTOUT		(SV_EIN,	B, 5) // Ausgang für Zusatzschaltung zur Stromversorgung des Fernschreibers
DEFPORTINPULLAL	(TASTEEXT,	B, 4) // Eingang für externe Taste zur Aktivierung

#ifdef PARALLELAUSGABE

DEFPORTOUT		(FS2_AUSG,	B, 2) // Schleifenschluß-Ausgabe
DEFPORTOUT		(FS2_AKTIV, B, 1) // Einschaltung (Ausgabe, Polarität der Schleife)
DEFPORTINPULL	(FS2_EING,	D, 5) // Strom-Einlesung

#endif //def PARALLELAUSGABE

#elif PLATINE_VERSION >= 10

#warning Nur fuer alte Platinen-Version 1.0 bis 1.2

DEFPORTINPULLAL	(TASTE,		D, 6)	

DEFPORTOUT		(LEDROT, 	D, 7)
DEFPORTOUT		(LEDGELB, 	B, 0)
DEFPORTOUT		(LEDGRUEN, 	B, 1)
DEFPORTOUT		(LEDBLAU, 	B, 2)

DEFPORTOUT		(FS_AUSG,	D, 0) // Schleifenschluß-Ausgabe
DEFPORTOUT		(FS_AKTIV,	D, 1) // Einschaltung (Ausgabe, Polarität der Schleife)
DEFPORTINPULL	(FS_EING, 	C, 3) // Strom-Einlesung

DEFPORTOUT		(SV_EIN,	B, 5) // Ausgang für Zusatzschaltung zur Stromversorgung des Fernschreibers
DEFPORTINPULLAL	(TASTEEXT,	B, 4) // Eingang für externe Taste zur Aktivierung

#else

#error Platinenversion PLATINE_VERSION nicht bekannt...

#endif // PLATINE_VERSION 

#endif // not defined V10

#endif //ndef __PORTS_H__
