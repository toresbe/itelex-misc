#include "LokalAusgabe.h"


extern void LokalZeichenAusgabe(char c); 
// muss vom Anwendung beigestellt werden!


//! Gibt einen Text auf dem angeschlossenen Fernschreiber aus.
//------------------------------------------------------------
//! \param s Zeiger auf Text im Programmspeicher.
void LokalTextAusgabeP(PGM_P s)
	{
	while (pgm_read_byte(s) != '\0')
		{
		LokalZeichenAusgabe(pgm_read_byte(s));
		s++;
		}
	}


#ifndef FUER_TW39
	
//! Gibt einen Text auf dem angeschlossenen Fernschreiber aus.
//------------------------------------------------------------
//! \param s Zeiger auf Text im RAM.
void LokalTextAusgabe(char* s)
	{
	while (*s != '\0')
		{
		LokalZeichenAusgabe(*s);
		s++;
		}
	}

#endif 
	
	
//! Gibt einen einzelne Ziffer auf dem angeschlossenen Fernschreiber aus.
//------------------------------------------------------------
//! Darf auch eine Hexadezimale Ziffer sein.
//! \param i Ziffer / Wert.
void LokalZifferAusgabe(uint8_t i)
	{
	if (i <= 9)
		LokalZeichenAusgabe('0' + i);
	else
		LokalZeichenAusgabe('a' + i - 10);
	}


#ifndef FUER_TW39

//! Gibt einen hexadezimale Zahl auf dem angeschlossenen Fernschreiber aus.
//-------------------------------------------------------------------------
//! \param i die auszugebende Zahl.
void LokalHexAusgabe(uint8_t i)
	{
	LokalZifferAusgabe(i >> 4);
	LokalZifferAusgabe(i & 0xf);
	}

#endif
	
	
//! Gibt einen dezimale Zahl auf dem angeschlossenen Fernschreiber aus.
//---------------------------------------------------------------------
//! \param i die auszugebende Zahl.
//! \param minzif Mindestzahl an Ziffern (kann führende Nullen bewirken).
void LokalZahlAusgabe(uint8_t i, int8_t minzif)
	{
	uint8_t z = 0; // Zehner

	while (i >= 10)
		z++, i -= 10;

	if 	(z > 0 || minzif > 1)
		LokalZahlAusgabe(z, minzif - 1);

	LokalZifferAusgabe(i);
	}
	

