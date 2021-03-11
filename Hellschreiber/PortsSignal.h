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
DEFPORTOUT		(DiagA,							B, 3) // = MOSI
DEFPORTOUT		(DiagB,							B, 4) // = MISO
DEFPORTOUT		(DiagC,							B, 5) // = SCK

DEFPORTTRI		(LEDblau,   					C, 0) // Da auch ein Ausgang vom Signalprozessor angeschlossen ist
// belegt durch TWI Schnittstelle				C, 4) // = SDA
// belegt durch TWI Schnittstelle				C, 5) // = SCL

// belegt durch Serielle Schnittstelle			D, 0)
// belegt durch Serielle Schnittstelle			D, 1)
DEFPORTOUT		(HellTonGegenkopplung,			D, 2) 
DEFPORTINAL		(HellSchleife,					D, 4)
DEFPORTTRI		(HellTonAusg,					D, 5) // = OC0B, Tristate, damit Empfang nicht beeinflusst wird.
// belegt durch analogen Komperator				D, 6)
// belegt durch analogen Komperator				D, 7)


#endif //ndef __PORTS_H__

