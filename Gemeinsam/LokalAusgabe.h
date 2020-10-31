#ifndef __LOKALAUSGABE_H__

#define __LOKALAUSGABE_H__

#include <inttypes.h>
#include <avr/pgmspace.h>
#include <stdbool.h>

extern void LokalZeichenAusgabe(char c); 
// muss vom Anwendung beigestellt werden!

extern void LokalTextAusgabeP(PGM_P s);

extern void LokalTextAusgabe(char* s);

extern void LokalZifferAusgabe(uint8_t i);

extern void LokalHexAusgabe(uint8_t i);

extern void LokalZahlAusgabe(uint8_t i, int8_t minzif);

#ifdef FUER_TW39

#ifdef SPRACHE_EN
#define LokalBoolAusgabe(b) LokalZeichenAusgabe((b) ? 'y' : 'n')
#else
#define LokalBoolAusgabe(b) LokalZeichenAusgabe((b) ? 'j' : 'n')
#endif

#else
	
extern void LokalBoolAusgabe(bool b);

#endif

#endif //ndef __LOKALAUSGABE_H__
