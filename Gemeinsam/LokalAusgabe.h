#ifndef __LOKALAUSGABE_H__

#define __LOKALAUSGABE_H__

#include <inttypes.h>
#include <avr/pgmspace.h>

extern void LokalZeichenAusgabe(char c); 
// muss vom Anwendung beigestellt werden!

extern void LokalTextAusgabeP(PGM_P s);

extern void LokalTextAusgabe(char* s);

extern void LokalZifferAusgabe(uint8_t i);

extern void LokalHexAusgabe(uint8_t i);

extern void LokalZahlAusgabe(uint8_t i, int8_t minzif);

#ifdef SPRACHE_EN
#define LokalBoolAusgabe(b) LokalZeichenAusgabe((b) ? 'y' : 'n')
#else
#define LokalBoolAusgabe(b) LokalZeichenAusgabe((b) ? 'j' : 'n')
#endif

#endif //ndef __LOKALAUSGABE_H__
