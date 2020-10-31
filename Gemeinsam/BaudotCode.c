#include "BaudotCode.h"

#include <stddef.h> // for NULL
#include <avr/pgmspace.h>

// Übersetzer-Tabellen
// -------------------     

// Behelfe für übersichtliche Tabelle:
#define KL CodeChrKlingel 
#define WD CodeChrWerDa 

// 15.01.2019: komplette umstrukturierung für verschiedene 'codepages':
// defines für codepages: 
//   AF_TTYCODE_SWITCHABLE für umschaltbare Tabellen (ungetestet)
//   TTYCODE_US für US_TTY
//   TTYCODE_RUSSIAN_KOI7N2 für Kyrillisch mit 7 Bit (ohne lateinische Kleinbuchstaben!)
//   TTYCODE_RUSSIAN_KOI8R für Kyrillisch mit 8 Bit
//   TTYCODE_
//   TTYCODE_GREEK_CP737 für Griechich
//   TTYCODE_NORDIC für Norwegen / Schweden (noch nicht implementiert)


// falls ich mal UTF-8 verwenden will: Umsetzung v.u.n. Unicode siehe 
// https://www.fileformat.info/info/unicode/utf8.htm


// Das Russische Alphabet in Großbuchstaben:
//  

// ========================================
// Code-Tabellen für besondere Zeichensätze
// ========================================

// für RUSSISCH (KOI8R) 
// ----========
#if defined(AF_TTYCODE_SWITCHABLE) || defined(TTYCODE_RUSSIAN_KOI8R)

#include "BaudotCode_KOI8.h"

#ifndef AF_TTYCODE_SWITCHABLE
#define TtyCodeTabZi TtyCodeTab_RU8_Zi
#define TtyCodeTab3E TtyCodeTab_RU8_3E
#define ErsetzTab ErsetzTab_RU8
#define DritteEbeneVorhanden 1
#endif					

#endif


// für USTTY:
// ----======
#if defined(AF_TTYCODE_SWITCHABLE) || defined(TTYCODE_US) 

#include "BaudotCode_US.h"

#ifndef AF_TTYCODE_SWITCHABLE
#define TtyCodeTabZi TtyCodeTab_US_Zi
#define ErsetzTab NULL
#endif					

#endif // USTTY Zi / Figs


// für GRIECHISCH (CP737) 
// ----==========
#if defined(AF_TTYCODE_SWITCHABLE) || defined(TTYCODE_GREEK_CP737)

#include "BaudotCode_GR737.h"

#ifndef AF_TTYCODE_SWITCHABLE
#define TtyCodeTabZi TtyCodeTab_GR737_Zi
#define TtyCodeTab3E TtyCodeTab_GR737_3E 
#define ErsetzTab NULL
#define DritteEbeneVorhanden 1
#endif					

#endif // Griechisch



// TODO Noch auf externe Dateien umstellen:

// für RUSSISCH (KOI7N) 
// ----========
#if defined(AF_TTYCODE_SWITCHABLE) || defined(TTYCODE_RUSSIAN_KOI7N2)
//                          			     0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15   
PROGMEM char TtyCodeTab_RU7_Zi[]		= { '#', '5','\r', '9', ' ', 125, ',', '.','\n', ')', '4', 123, '8', '0', ':', '=', 	
			
//                          			    16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31
											'3', '+', WD, '?', '\'', '6', 124, '/', '-', '2', 96, '#', '7', '1', '(', '#'};

//                          			     0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15   
PROGMEM char TtyCodeTab_RU7_3E[]		= { '#', 116,'\r', 111, ' ', 104, 110, 109,'\n', 108, 114, 103, 105, 112, 99, 118, 
			
//                          			    16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31
										   101, 122, 100,  98, 115, 121, 102, 120,  97, 119, 106,  '#',117, 113, 107,  '#'};

#ifndef AF_TTYCODE_SWITCHABLE
#define TtyCodeTabZi TtyCodeTab_RU7_Zi
#define TtyCodeTab3E TtyCodeTab_RU7_3E
#define ErsetzTab ErsetzTab_RU7
#define DritteEbeneVorhanden 1
#endif					

#endif // Russisch KOI7N2

/* TODO Noch später mal...

#elif defined(BULGARIAN_KOI8R)
											
#define KY CodeChrKyUm			
//                          			     0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15   
PROGMEM char TtyCodeTabBu[] 			= { '#', 't','\r', 'o', ' ', 'h', 'n', 'm','\n', 'l', 'r', 'g', 'i', 'p', 'c', 'v', 
			
//                          			    16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31
											'e', 'z', 'd', 'b', 's', 'y', 'f', 'x', 'a', 'w', 'j', '#', 'u', 'q', 'k', '#'};
			
//                          			     0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15   
PROGMEM char TtyCodeTabZi[] 			= { '#', '5','\r', '9', ' ', 234, ',', '.','\n', ')', '4', 253 '8', '0', ':', '=', 
			
//                          			    16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31
											'3', '+', WD, '?', '\'', '6', 224, '/', '-', '2', KL , '#', '7', '1', '(', '#'};
			
//                          			     0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15   
PROGMEM char TtyCodeTabKy[] 			= { '#', 244,'\r', 239, ' ', 232, 238, 237,'\n', 236, 242, 231, 233, 240, 227, 247, 
			
//                          			    16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31
											229, 250, 228, 226, 243, 249, 230, 251, 225, 255, 234, '#', 245, 241, 235, '#'};


*/

// ARBEITSVORRAT*/



// ========================================
// Code-Tabellen für Standard-Zeichensätze
// ========================================

// für Großbuchstaben (ITA2):
// ----==============--------

#if defined(AF_TTYCODE_SWITCHABLE) || defined(TTYCODE_RUSSIAN_KOI7N2)
//                          			     0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15   
PROGMEM const char TtyCodeTab_Gross_Bu[]= { '#', 'T','\r', 'O', ' ', 'H', 'N', 'M','\n', 'L', 'R', 'G', 'I', 'P', 'C', 'V', 
			
//                          			    16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31
											'E', 'Z', 'D', 'B', 'S', 'Y', 'F', 'X', 'A', 'W', 'J', '#', 'U', 'Q', 'K', '#'};

#ifndef AF_TTYCODE_SWITCHABLE
#define TtyCodeTabBu TtyCodeTab_Gross_Bu
#endif					

#endif


// für Kleinbuchstaben = ITA2:
// ----===============--------
								   
#if defined(AF_TTYCODE_SWITCHABLE) || !defined(TtyCodeTabBu)
//                                           0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15   
PROGMEM const char TtyCodeTab_STD_Bu[]	= { '#', 't','\r', 'o', ' ', 'h', 'n', 'm','\n', 'l', 'r', 'g', 'i', 'p', 'c', 'v', 
                                   
//                                          16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31
						            		'e', 'z', 'd', 'b', 's', 'y', 'f', 'x', 'a', 'w', 'j', '#', 'u', 'q', 'k', '#'};
											
#ifndef AF_TTYCODE_SWITCHABLE
#define TtyCodeTabBu TtyCodeTab_STD_Bu
#endif					

#endif	
	
// für Ziffern/Zeichen = ITA2:
// ----===============--------
						            		
#if defined(AF_TTYCODE_SWITCHABLE) || !defined(TtyCodeTabZi)
//                                           0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15   
PROGMEM const char TtyCodeTab_STD_Zi[]	= { '#', '5','\r', '9', ' ', '#', ',', '.','\n', ')', '4', '#', '8', '0', ':', '=', 
                                   
//                                          16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31
						            		'3', '+', WD , '?','\'', '6', '#', '/', '-', '2', KL,  '#', '7', '1', '(', '#'};
						            		
#ifndef AF_TTYCODE_SWITCHABLE
#define TtyCodeTabZi TtyCodeTab_STD_Zi
#endif					

#endif                             



// ==========================================
// Vorkehrungen für umschaltbare Zeichensätze
// ==========================================

#ifdef AF_TTYCODE_SWITCHABLE

// Variablen, die auf die aktuelle Code-Tabelle zeigen:
prog_char * TtyCodeTabBu;
prog_char * TtyCodeTabZi;
prog_char * TtyCodeTab3E;
prog_char * ErsetzTab;
bool GrossZuKleinbuchstaben;
bool DritteEbeneVorhanden;

#else
// die Tabellen TtyCodeTabBu und TtyCodeTabZi sind bereits per #define auf die einzig gültige Tabelle gesetzt
// nun müssen noch die (sonst) Variablen GrossZuKleinbuchstaben und DritteEbeneVorhanden 'simuliert' werden
// Falls DritteEbeneVorhanden nicht gesetzt ist wird dies auf false definiert und die Tabelle TtyCodeTab3E
// auf einen unschädlichen Bereich gesetzt, aber nur damit der Compiler nicht jammert.
	
#if defined(TTYCODE_RUSSIAN_KOI7N2)
#define GrossZuKleinbuchstaben 0
#else
#define GrossZuKleinbuchstaben 1
#endif //else not defined(TTYCODE_RUSSIAN_KOI7N2)

#ifndef DritteEbeneVorhanden
#define DritteEbeneVorhanden 0
#define TtyCodeTab3E TtyCodeTabBu
#endif

#ifndef ErsetzTab
#define ErsetzTab NULL
#endif 

#endif //def AF_TTYCODE_SWITCHABLE



//! Setzt Baudot-Code in ASCII-Zeichen um. Folgende Sonderzeichen werden beherrscht: Klingel -> CodeChrKlingel und WerDa -> CodeChrWerDa
//! Bei Buchstaben- oder Ziffern-Umschaltung wird nur der Status geändert, keine Ausgabe von SI oder SO
//! \param code Baudot-Code (zwischen 0 und 31).
//! \param[in,out] Mode wird bei entsprechenden Baudot-Codes geändert!
//! \return Zeichen in ASCII.
//! \retval '\0' bei Buchstaben- oder Ziffern-Umschaltung.
//! \retval '#' bei ungültigen Zeichen oder sonstigen Steuerzeichen.
char CodeZuZeichen(uint8_t code, TBaudotMode *Mode)
	{
	if (code == TtyCodeZiUm)
		*Mode = BaudotMode_ZiffernEmpfangen; 
	else if (code == TtyCodeBuUm)
		*Mode = BaudotMode_BuchstabenEmpfangen; 
	else if (DritteEbeneVorhanden && code == TtyCode3EUm)
		*Mode = BaudotMode_DritteEbEmpfangen; 

	else
		{
		BaudotMode_SetEmpfangen(*Mode); // dies bewirkt, dass beim nächsten Aufruf von ZeichenZuCode2 auf jeden Fall ein Bu oder Zi vorweg gesendet wird.
		if (BaudotMode_IstZiffern(*Mode))
			return pgm_read_byte(&TtyCodeTabZi[code]);
		else if (DritteEbeneVorhanden && BaudotMode_IstDritteEb(*Mode))
			return pgm_read_byte(&TtyCodeTab3E[code]);
		else 
			return pgm_read_byte(&TtyCodeTabBu[code]);
		}
	return '\0';
	}


//! Setzt ASCII-Zeichen in Baudot-Code um. Es erfolgt _keine_ Umschaltung von Buchstaben
//! auf Ziffern oder umgekehrt. Sonderzeichen Klingel und Werda werden umgesetzt, auch Großbuchstaben zulässig.
//! #ErsetzTab wird auch nicht berücksichtigt.
//! \param c Zeichen in ASCII.
//! \param Mode Buchstaben oder Ziffern. Es wird nur das Bit BaudotMode_Ziffern ausgewertet
//! \return Baudot-Code (zwischen 0 und 31).
//! \retval 255 bei nicht passendem c (nicht in Code-Tabelle).
uint8_t ZeichenZuCode(char c, TBaudotMode Mode)
	{
	prog_char* tp;
	if (BaudotMode_IstZiffern(Mode))
		tp = TtyCodeTabZi;
	else if (DritteEbeneVorhanden && BaudotMode_IstDritteEb(Mode))
		tp = TtyCodeTab3E;
	else 
		tp = TtyCodeTabBu;

	if (c >= 'A' && c <= 'Z' && GrossZuKleinbuchstaben)
		c += 'a' - 'A';
	
	for (uint8_t i = 0 ; i < 32 ; i++)
		if (c == pgm_read_byte(tp))
			return i;
		else
			tp++;
	return 255;
	}


#ifdef AF_TTYCODE_SWITCHABLE

void CodeTabWechsel(uint8_t Mode)
	{
	switch (Mode)
		{
		case 1: // US
			TtyCodeTabBu = TtyCodeTab_STD_Bu;
			TtyCodeTabZi = TtyCodeTab_US_Zi;
			ErsetzTab = NULL;
			DritteEbeneVorhanden = false;
			GrossZuKleinbuchstaben = true;
			break;
			
		case 2: // KOI7N2
			TtyCodeTabBu = TtyCodeTab_Gross_Bu;
			TtyCodeTabZi = TtyCodeTab_RU7_Zi;
			TtyCodeTab3E = TtyCodeTab_RU7_3E;
			//ErsetzTab = ErsetzTab_RU7; TODO
			DritteEbeneVorhanden = true;
			GrossZuKleinbuchstaben = false;
			break;
		
		case 3: // KOI8R
			TtyCodeTabBu = TtyCodeTab_STD_Bu;
			TtyCodeTabZi = TtyCodeTab_RU8_Zi;
			TtyCodeTab3E = TtyCodeTab_RU8_3E;
			ErsetzTab = ErsetzTab_RU8;
			DritteEbeneVorhanden = true;
			GrossZuKleinbuchstaben = true;
			break;
		
		case 4: // GRIECHISCH Codepage 737
			TtyCodeTabBu = TtyCodeTab_STD_Bu;
			TtyCodeTabZi = TtyCodeTab_GR737_Zi;
			TtyCodeTab3E = TtyCodeTab_GR737_3E;
			ErsetzTab = NULL;
			DritteEbeneVorhanden = true;
			GrossZuKleinbuchstaben = true;
			break;
			
		default: // ITA2
			TtyCodeTabBu = TtyCodeTab_STD_Bu;
			TtyCodeTabZi = TtyCodeTab_STD_Zi;
			ErsetzTab = NULL;
			DritteEbeneVorhanden = false;
			GrossZuKleinbuchstaben = true;
			break;
			
		} // switch (Mode)
		
	} // CodeTabWechsel(uint8_t Mode)


#endif //def AF_TTYCODE_SWITCHABLE


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

	if (c == CodeChrZiUm)
		{
		*Code1 = TtyCodeZiUm;
		*Mode = BaudotMode_ZiffernGesendet;
		return true;
		}		
	
	if (c == CodeChrBuUm)
		{
		*Code1 = TtyCodeBuUm;
		*Mode = BaudotMode_BuchstabenGesendet;
		return true;
		}		
		
	if (DritteEbeneVorhanden && c == CodeChr3EUm)
		{
		*Code1 = TtyCode3EUm;
		*Mode = BaudotMode_DritteEbGesendet;
		return true;
		}		
		
	// Umwandlung von GROSS in klein macht ZeichenZuCode
	
	if (ErsetzTab != NULL) // muss etwas vorab getauscht werden?
		{
		prog_char* p = ErsetzTab;
		while (pgm_read_byte(p) != '\0')
			if (pgm_read_byte(p) == c)
				{
				c = pgm_read_byte(p+1);
				break;
				}
			else
				p += 2; // ErsetzTab enthält Zeichenpaare!
		} // if (ErsetzTab != NULL)
	
	if (BaudotMode_IstSenden(*Mode) && c != CodeChrWerDa)
		{ // zuletzt wurde gesendet, dann kann die gesendete Zeichenebene ggf. weiterverwendet werden.
		*Code1 = ZeichenZuCode(c, *Mode);
		if (*Code1 != 255)
			return true; // fertig
		}

	// dieser Punkt wird nur erreicht, wenn auf jeden Fall eine Bu- oder Zi-Umschaltung stattfinden soll.
	// entweder weil ein Wechsel der Zeichenebene stattfand oder weil zuletzt empfangen wurde.

	*Code2 = ZeichenZuCode(c, 0); // zuerst Buchstaben probieren
	if (*Code2 != 255)
		{
		*Code1 = TtyCodeBuUm;
		*Mode = BaudotMode_BuchstabenGesendet;
		return true;
		}

	*Code2 = ZeichenZuCode(c, BaudotMode_ZiffernBitMaske); // Ziffern Tabelle probieren
	if (*Code2 != 255)
		{
		*Code1 = TtyCodeZiUm;
		*Mode = BaudotMode_ZiffernGesendet;
		return true;
		}

	if (DritteEbeneVorhanden)
		{
		*Code2 = ZeichenZuCode(c, BaudotMode_DritteEbBitMaske); // 3. Ebene Tabelle probieren
		if (*Code2 != 255)
			{
			*Code1 = TtyCode3EUm;
			*Mode = BaudotMode_DritteEbGesendet;
			return true;
			}
		}

	return false; // Zeichen gar nicht umsetzbar
	}

		
#endif //def FUER_TW39
