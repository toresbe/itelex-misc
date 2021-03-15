// Port-Definitionen für Hellschreiber-Schnittstelle im i-Telex-System, Teil Kommunikationsprozessor.

#ifndef __PORTS_H__

#define __PORTS_H__

#include "Defports.h"


// Platinen-Version
// ================
#ifndef PLATINE_VERSION
// #error Platinen-Version in Konfiguration festlegen!
// ist momentan noch unnötig
#endif

//#define PLATINE_VERSION xx


// Schnittstellen
// --------------

// #if (PLATINE_VERSION < 13)

DEFPORTOUT		(HellAnrufPulse,				B, 0) 
DEFPORTOUT		(DiagD,							B, 1) //        = Pin 15 Prozessor (ungenutzt)   Funktion: TWI-Datenverkehr
DEFPORTOUT		(DiagE,							B, 2) //        = Pin 16 Prozessor (ungenutzt)   Funktion: Letzter TWI-Zugriff war lesend
DEFPORTOUT		(DiagA,							B, 3) // = MOSI = Pin 6 Programmierstecker       Funktion: Hell-Ton-Sendung: Pixel-Ende-Takt
DEFPORTOUT		(DiagB,							B, 4) // = MISO = Pin 5 Programmierstecker       Funktion: Hell-Ton-Empfang: gesamtes Zeichen
DEFPORTOUT		(DiagC,							B, 5) // = SCK  = Pin 4 Programmierstecker       Funktion: Wirkdauer von PollTwi

DEFPORTTRI		(LEDblau,   					C, 0) // Da auch ein Ausgang vom Signalprozessor angeschlossen ist
DEFPORTINAL		(SDA,							C, 4) 
DEFPORTINAL		(SCL,							C, 5)

// belegt durch Serielle Schnittstelle			D, 0)
// belegt durch Serielle Schnittstelle			D, 1)
DEFPORTOUT		(HellTonGegenkopplung,			D, 2) 
DEFPORTINAL		(HellSchleife,					D, 4)
DEFPORTTRI		(HellTonAusg,					D, 5) // = OC0B, Tristate, damit Empfang nicht beeinflusst wird.
// belegt durch analogen Komperator				D, 6)
// belegt durch analogen Komperator				D, 7)


#endif //ndef __PORTS_H__

