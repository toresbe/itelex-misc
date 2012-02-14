#include "BaudotCode.h"

#include <avr/pgmspace.h>

// Übersetzer-Tabellen
// -------------------         0           1         2         3 
//                             012 345678 90123456789012345678901
PROGMEM char TtyCodeTabBu[] = "#t\ro hnm\nlrgipcvezdbsyfxawj#uqk#";
PROGMEM char TtyCodeTabZi[] = "#5\r9 #,.\n)4#80:=3+#?'6#/-2##71(#";


//! Setzt ASCII-Zeichen in Baudot-Code um. Es erfolgt keine Umschaltung von Buchstaben
//! auf Ziffern oder umgekehrt.
//! \param c Zeichen in ASCII.
//! \param Mode Buchstaben oder Ziffern.
//! \return Baudot-Code (zwischen 0 und 31).
//! \retval 255 bei nicht passendem c (nicht in Code-Tabelle oder falscher Modus Bu/Zi).
uint8_t ZeichenZuCode(char c, char Mode)
	{
	volatile prog_char* tp;
	if (Mode == BuMode)
		tp = TtyCodeTabBu;
	else if (Mode == ZiMode)
		tp = TtyCodeTabZi;
	else
		return 255;

	if (c >= 'A' && c <= 'Z')
		c += 'a' - 'A';
	for (uint8_t i = 0 ; i < 32 ; i++)
		if (c == pgm_read_byte(tp))
			return i;
		else
			tp++;
	return 255;
	}


//! Setzt Baudot-Code in ASCII-Zeichen um.
//! \param code Baudot-Code (zwischen 0 und 31).
//! \param[in,out] Mode Buchstaben oder Ziffern. Wird bei entsprechenden Baudot-Codes geändert!
//! \return Zeichen in ASCII.
//! \retval '\0' bei falschem Modus oder bei Buchstaben oder Ziffen-Umschaltung.
//! \retval '#' bei ungültigen Zeichen oder sonstigen Steuerzeichen.
char CodeZuZeichen(uint8_t code, char *Mode)

	{
	if (code == TtyCodeZiUm)
		*Mode = ZiMode;
	else if (code == TtyCodeBuUm)
		*Mode = BuMode;
	else
		{
		if (*Mode == ZiMode)
			return pgm_read_byte(&TtyCodeTabZi[code]);
		else if (*Mode == BuMode)
			return pgm_read_byte(&TtyCodeTabBu[code]);
		}
	return '\0';
	}
	

#ifndef FUER_TW39


//! Setzt ASCII-Zeichen in Baudot-Code um. Erforderlichenfalls wird ein zusätzlicher
//! Umschalt-Code von Buchstaben oder Ziffern generiert.
//! \param[in] c Zeichen in ASCII.
//! \param[in,out] Mode Buchstaben oder Ziffern. Wird ggf. geändert.
//! \param[out] Code1 erster Baudot-Code (ggf. Bu oder Zi-Umschaltung).
//! \param[out] Code2 zweiter Baudot-Code, wenn Code1 Bu oder Zi enthält. 
//!             Wenn 255 war keine Umschaltung erforderlich.
//! \return Umsetzung erfolgreich. Bei false sind Code1 und Code2 ungültig.
bool ZeichenZuCode2(char c, char *Mode, uint8_t* Code1, uint8_t* Code2)
	{
	if (c == CodeChrWerDa)
		{
		*Code1 = TtyCodeZiUm;
		*Code2 = TtyCodeZiWerDa;
		*Mode = '\0'; // undefiniert!
		return true;
		}

	*Code1 = ZeichenZuCode(c, *Mode);
	*Code2 = 255;

	if (*Code1 != 255)
		return true; // fertig

	*Code2 = ZeichenZuCode(c, BuMode); // andere Tabelle probieren
	if (*Code2 != 255)
		{
		*Code1 = TtyCodeBuUm;
		*Mode = BuMode;
		return true;
		}

	*Code2 = ZeichenZuCode(c, ZiMode); // andere Tabelle probieren
	if (*Code2 != 255)
		{
		*Code1 = TtyCodeZiUm;
		*Mode = ZiMode;
		return true;
		}

	return false; // Zeichen gar nicht umsetzbar
	}

		
#endif //def FUER_TW39
