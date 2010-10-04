#include "FifoPuffer.h"

#include <avr/interrupt.h>

void PufferInit(TPuffer *p)
	{
	p->ModeZiffern = false;
	p->SpeichP = 0;
	p->AusgP = 0;
	}

	
static uint8_t PufferNP(uint8_t p)
	{
	if (p >= MaxPuffer - 1)
		return 0;
	else
		return p+1;
	}


inline bool PufferLeer(TPuffer *p) 
	{
	return p->AusgP == p->SpeichP;
	}
	
	
inline bool PufferVoll(TPuffer *p) 
	{
	return PufferNP(p->SpeichP) == p->AusgP;
	}
	
	
bool PufferSpeich(TPuffer *p, uint8_t code)
	{
	if (!PufferVoll(p))
		{
		cli();
		p->Puffer[p->SpeichP] = code;
		p->SpeichP = PufferNP(p->SpeichP);
		sei();
		return true;
		}
	else
		return false;
	}
	

uint8_t PufferAusg(TPuffer *p)
	{
	cli();
	uint8_t res = p->Puffer[p->AusgP];
	p->AusgP = PufferNP(p->AusgP);
	sei();
	return res;
	}
	
 
 uint8_t PufferAnzahl(TPuffer *p)
	{
	if (p->AusgP < p->SpeichP)
		return MaxPuffer + p->AusgP - p->SpeichP;
	else
		return p->AusgP - p->SpeichP;
	}

