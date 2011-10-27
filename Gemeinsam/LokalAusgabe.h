#ifndef __LOKALAUSGABE_H__

#define __LOKALAUSGABE_H__

#include <inttypes.h>
#include <avr/pgmspace.h>

extern void LokalZeichenAusgabe(char c); 
	// muss von anderem Programm beigestellt werden!
	
extern void LokalTextAusgabeP(PGM_P s);

extern void LokalTextAusgabe(char* s);

extern void LokalZifferAusgabe(uint8_t i);

extern void LokalHexAusgabe(uint8_t i);

extern void LokalZahlAusgabe(uint8_t i, int8_t minzif);

#endif //ndef __LOKALAUSGABE_H__
