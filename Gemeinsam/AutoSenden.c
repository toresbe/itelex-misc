#include "AutoSenden.h"

//! Sendet einen Text aus dem Flash.
//! Verwendet (externe) Funktion AutoSendenZeichen.
void AutoSendenTextP(PGM_P s)
	{
	while (pgm_read_byte(s) != '\0')
		{
		AutoSendenZeichen(pgm_read_byte(s));
		s++;
		}
	}
	
		
void AutoSendenText(char* s)
	{
	while (*s != '\0')
		{
		AutoSendenZeichen(*s);
		s++;
		}
	}
	
		
bool AutoSendenFertig()
	{
	return (TODO AutoSendenAusgP == AutoSendenSpeichP && SerUmSendBitNr == SerUmSendWarte);
	}
	

