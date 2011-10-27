#include "LokalAusgabe.h"

void LokalTextAusgabeP(PGM_P s)
	{
	while (pgm_read_byte(s) != '\0')
		{
		LokalZeichenAusgabe(pgm_read_byte(s));
		s++;
		}
	}


#ifndef FUER_TW39
	
void LokalTextAusgabe(char* s)
	{
	while (*s != '\0')
		{
		LokalZeichenAusgabe(*s);
		s++;
		}
	}

#endif 
	
	
void LokalZifferAusgabe(uint8_t i) // auch Hex
	{
	if (i <= 9)
		LokalZeichenAusgabe('0' + i);
	else
		LokalZeichenAusgabe('a' + i - 10);
	}


#ifndef FUER_TW39

void LokalHexAusgabe(uint8_t i)
	{
	LokalZifferAusgabe(i >> 4);
	LokalZifferAusgabe(i & 0xf);
	}

#endif
	
	
void LokalZahlAusgabe(uint8_t i, int8_t minzif)
	{
	if (i >= 10 || minzif > 1)
		{
		uint8_t z = i / 10;
		LokalZahlAusgabe(z, minzif - 1);
		i -= 10 * z;
		}
	LokalZifferAusgabe(i);
	}
	

