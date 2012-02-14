#ifndef __SERIELLUMSETZ_H__

#define __SERIELLUMSETZ_H__

#include <inttypes.h>
#include <bool.h>

// Empfang: Umsetzung Seriell (Baudot) --> Parallel (Daten)
	
enum { SerUmEmpfWarte = 0, SerUmEmpfFertig = 8, SerUmSendWarte = 0, SerUmSendStart = 1 } ;
//!< Wichtige Zustände von SerUmEmpfBitNr und SerUmSendBitNr.

extern volatile uint8_t SerUmEmpfBitNr; 

extern volatile uint8_t SerUmEmpfDaten; 

extern volatile bool SerUmEmpfFehler; 

// Senden: Umsetzung Parallel (Daten) --> Seriell (Baudot)

extern volatile uint8_t SerUmSendBitNr; 

extern volatile uint8_t SerUmSendDaten;


extern void SeriellUmsetzInit();

extern void SeriellUmsetzung(bool SeriellEing, bool *SeriellAusg);

#endif //ndef __SERIELLUMSETZ_H__
