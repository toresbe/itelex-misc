#include "BaudotCode.h"

#include <avr/pgmspace.h>

// Übersetzer-Tabellen
// -------------------     

// Behelfe für übersichtliche Tabelle:
#define BU CodeChrBuUm
#define ZI CodeChrZiUm 
#define KL CodeChrKlingel 
#define WD CodeChrWerDa 

#ifdef USTTY
//                               0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15   
PROGMEM char TtyCodeTabBu[] = { '#', 't','\r', 'o', ' ', 'h', 'n', 'm','\n', 'l', 'r', 'g', 'i', 'p', 'c', 'v', 

//                              16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31
                                'e', 'z', 'd', 'b', 's', 'y', 'f', 'x', 'a', 'w', 'j', ZI , 'u', 'q', 'k', BU };

//                               0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15   
PROGMEM char TtyCodeTabZi[] = { '#', '5','\r', '9', ' ', '#', ',', '.','\n', ')', '4', '&', '8', '0', ':', ';', 

//                              16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31
								'3', '"', '$', '?', KL , '6', '!', '/', '-', '2','''', ZI , '7', '1', '(', BU };
#else // ITA2
//                               0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15   
PROGMEM char TtyCodeTabBu[] = { '#', 't','\r', 'o', ' ', 'h', 'n', 'm','\n', 'l', 'r', 'g', 'i', 'p', 'c', 'v', 

//                              16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31
								'e', 'z', 'd', 'b', 's', 'y', 'f', 'x', 'a', 'w', 'j', ZI , 'u', 'q', 'k', BU };
								
//                               0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15   
PROGMEM char TtyCodeTabZi[] = { '#', '5','\r', '9', ' ', '#', ',', '.','\n', ')', '4', '#', '8', '0', ':', '=', 

//                              16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31
								'3', '+', WD , '?','''', '6', '#', '/', '-', '2', KL,  ZI , '7', '1', '(', BU };
								
#endif


//! Setzt Baudot-Code in ASCII-Zeichen um. Folgende Sonderzeichen werden beherrscht: Klingel -> CodeChrKlingel und WerDa -> CodeChrWerDa
//! Bei Buchstaben- oder Ziffern-Umschaltung wird nur der Status geändert, keine Ausgabe von SI oder SO
//! \param code Baudot-Code (zwischen 0 und 31).
//! \param[in,out] Mode wird bei entsprechenden Baudot-Codes geändert!
//! \return Zeichen in ASCII.
//! \retval '\0' bei Buchstaben- oder Ziffern-Umschaltung.
//! \retval '#' bei ungültigen Zeichen oder sonstigen Steuerzeichen.
char CodeZuZeichen(uint8_t code, TBaudotMode *Mode)
	{
	BaudotMode_SetEmpfangen(*Mode); // dies bewirkt, dass beim nächsten Aufruf von ZeichenZuCode2 auf jeden Fall ein Bu oder Zi vorweg gesendet wird.
	if (code == TtyCodeZiUm)
		BaudotMode_SetZiffern(*Mode);
	else if (code == TtyCodeBuUm)
		BaudotMode_SetBuchstaben(*Mode);
	else
		{
		if (BaudotMode_IstZiffern(*Mode))
			return pgm_read_byte(&TtyCodeTabZi[code]);
		else 
			return pgm_read_byte(&TtyCodeTabBu[code]);
		}
	return '\0';
	}


//! Setzt ASCII-Zeichen in Baudot-Code um. Es erfolgt keine Umschaltung von Buchstaben
//! auf Ziffern oder umgekehrt. Sonderzeichen Klingel und Werda werden umgesetzt
//! \param c Zeichen in ASCII.
//! \param Mode Buchstaben oder Ziffern. Es wird nur das Bit BaudotMode_Ziffern ausgewertet
//! \return Baudot-Code (zwischen 0 und 31).
//! \retval 255 bei nicht passendem c (nicht in Code-Tabelle).
uint8_t ZeichenZuCode(char c, TBaudotMode Mode)
	{
	volatile prog_char* tp;
	if (BaudotMode_IstZiffern(Mode))
		tp = TtyCodeTabZi;
	else 
		tp = TtyCodeTabBu;

	if (c >= 'A' && c <= 'Z')
		c += 'a' - 'A';
	for (uint8_t i = 0 ; i < 32 ; i++)
		if (c == pgm_read_byte(tp))
			return i;
		else
			tp++;
	return 255;
	}


	/*
static void UmschaltungBaudotMode(uint8_t code, TBaudotMode *Mode)
	{
	if (code == TtyCodeBuUm)
		BaudotMode_SetBuchstaben(*Mode); 
	else if (code == TtyCodeZiUm)
		BaudotMode_SetZiffern(*Mode);
	}
	*/
	

#ifndef FUER_TW39


//! Setzt ASCII-Zeichen in Baudot-Code um. Erforderlichenfalls wird ein zusätzlicher
//! Umschalt-Code von Buchstaben oder Ziffern generiert.
//! \param[in] c Zeichen in ASCII.
//! \param[in,out] Mode wird ggf. geändert.
//! \param[out] Code1 erster Baudot-Code (ggf. Bu oder Zi-Umschaltung).
//! \param[out] Code2 zweiter Baudot-Code, wenn Code1 Bu oder Zi enthält. 
//!             Wenn 255 war keine Umschaltung erforderlich.
//! \return Umsetzung erfolgreich. Bei false sind Code1 und Code2 ungültig.
bool ZeichenZuCode2(char c, TBaudotMode *Mode, uint8_t* Code1, uint8_t* Code2)
	{
	*Code2 = 255; // kommt öfter vor

	if (c >= 'A' && c <= 'Z')
		c += 'a' - 'A';
	
	if (BaudotMode_IstSenden(*Mode) && c != CodeChrWerDa))
		{ // zuletzt wurde gesendet, dann kann die gesendete Zeichenebene ggf. weiterverwendet werden.
		*Code1 = ZeichenZuCode(c, *Mode);
		if (*Code1 != 255)
			return true; // fertig
		}

	if (c == CodeChrZiUm)
		{
		*Code1 = TtyCodeZiUm;
		BaudotMode_SetZiffern(*Mode);
		BaudotMode_SetSenden(*Mode);
		return true;
		}		
	
	if (c == CodeChrBuUm)
		{
		*Code1 = TtyCodeBuUm;
		BaudotMode_SetBuchstaben(*Mode);
		BaudotMode_SetSenden(*Mode);
		return true;
		}		
		
	// dieser Punkt wird nur erreicht, wenn auf jeden Fall eine Bu- oder Zi-Umschaltung stattfinden soll.
	// entweder weil ein Wechsel der Zeichenebene stattfand oder weil zuletzt empfangen wurde.

	*Code2 = ZeichenZuCode(c, 0); // zuerst Buchstaben probieren
	if (*Code2 != 255)
		{
		*Code1 = TtyCodeBuUm;
		BaudotMode_SetBuchstaben(*Mode);
		BaudotMode_SetSenden(*Mode);
		return true;
		}

	*Code2 = ZeichenZuCode(c, BaudotMode_Ziffern); // Ziffern Tabelle probieren
	if (*Code2 != 255)
		{
		*Code1 = TtyCodeZiUm;
		BaudotMode_SetZiffern(*Mode);
		BaudotMode_SetSenden(*Mode);
		return true;
		}

	return false; // Zeichen gar nicht umsetzbar
	}

		
#endif //def FUER_TW39
