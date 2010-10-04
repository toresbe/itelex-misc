#include "BaudotCode.h"

#include <avr/pgmspace.h>

// Übersetzer-Tabellen
// -------------------         0           1         2         3 
//                             012 345678 90123456789012345678901
char TtyCodeTabBu[] PROGMEM = "#t\ro hnm\nlrgipcvezdbsyfxawj#uqk#";
char TtyCodeTabZi[] PROGMEM = "#5\r9 #,.\n)4#80:=3+#?'6#/-2##71(#";

uint8_t ZeichenZuCode(char c, bool Ziffer)
	{
	volatile prog_char* tp = (Ziffer ? TtyCodeTabZi : TtyCodeTabBu);

	if (c >= 'A' && c <= 'Z')
		c += 'a' - 'A';
	for (uint8_t i = 0 ; i < 32 ; i++)
		if (c == pgm_read_byte(tp))
			return i;
		else
			tp++;
	return 255;
	}


char CodeZuZeichen(uint8_t code, bool *Ziffer)
	{
	if (code == TtyCodeZiUm)
		*Ziffer = true;
	else if (code == TtyCodeBuUm)
		*Ziffer = false;
	else
		if (*Ziffer)
			return pgm_read_byte(&TtyCodeTabZi[code]);
		else
			return pgm_read_byte(&TtyCodeTabBu[code]);
	return '\0';
	}
	

#ifndef FUER_TW39

bool ZeichenZuCode2(char c, bool* Ziffer, uint8_t* Code1, uint8_t* Code2)
	{
	if (c == CodeChrWerDa)
		{
		*Code1 = TtyCodeZiUm;
		*Code2 = TtyCodeZiWerDa;
		*Ziffer = true;
		return true;
		}

	*Code1 = ZeichenZuCode(c, *Ziffer);
	*Code2 = 255;

	if (*Code1 == 255)
		{
		*Code2 = ZeichenZuCode(c, !*Ziffer); // andere Tabelle probieren
		if (*Code2 == 255)
			return false;
		else
			{
			if (*Ziffer)
				{
				*Code1 = TtyCodeBuUm;
				*Ziffer = false;
				}
			else
				{
				*Code1 = TtyCodeZiUm;
				*Ziffer = true;
				}
			}
		}
	return true;
	}

		
#endif //def FUER_TW39
