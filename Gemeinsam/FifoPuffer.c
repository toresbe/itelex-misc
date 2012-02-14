#include "FifoPuffer.h"

#include <avr/interrupt.h>


//! Initialisiert einen Puffer.
// ----------------------------
//! \param[out] p Zeiger auf den Puffer.
void PufferInit(TPuffer *p)
	{
	p->BuZiMode = '\0'; // undefiniert
	p->SpeichP = 0;
	p->AusgP = 0;
	}

	
//! Gibt den Index der folgenden Pufferposition zurück.
// ----------------------------------------------------
//! Ringpuffer beachten.
//! \param p Aktuelle Pufferposition.
//! \return nächste Pufferposition.
static uint8_t PufferNP(uint8_t p)
	{
	if (p >= MaxPuffer - 1)
		return 0;
	else
		return p+1;
	}


//! Prüft, ob Puffer komplett leer ist.
// ------------------------------------
//! \param p Zeiger auf den Puffer.
//! \returns Puffer ist leer.
extern bool PufferLeer(TPuffer *p) 
	{
	return p->AusgP == p->SpeichP;
	}
	
	
//! Prüft, ob Puffer komplett voll ist.
// ------------------------------------
//! \param p Zeiger auf den Puffer.
//! \returns Puffer ist vollständig gefüllt.
extern bool PufferVoll(TPuffer *p) 
	{
	return PufferNP(p->SpeichP) == p->AusgP;
	}
	
	
//! Schreibt ein Byte in den Puffer.
// ------------------------------------
//! \param p Zeiger auf den Puffer.
//! \param code Zu speicherndes Byte.
//! \returns Zeichen hatte noch im Puffer platz.
bool PufferSpeich(TPuffer *p, uint8_t code)
	{
	if (!PufferVoll(p))
		{
		uint8_t SregTemp = SREG; // sichert Interrupt-Enable
		cli();
		p->Puffer[p->SpeichP] = code;
		p->SpeichP = PufferNP(p->SpeichP);
		SREG = SregTemp;
		return true;
		}
	else
		return false;
	}
	

//! Holt ein Byte aus dem Puffer
// ------------------------------------
//! Vorher \b muss geprüft werden, dass mindestens ein Zeichen im Puffer ist.
//! Geeignet ist \code if (!PufferLeer(p)) _irgendwas_(PufferAusg(p)); \endcode
//! \param p Zeiger auf den Puffer.
//! \returns Zeichen aus dem Puffer.
uint8_t PufferAusg(TPuffer *p)
	{
	uint8_t SregTemp = SREG; // sichert Interrupt-Enable
	cli();
	uint8_t res = p->Puffer[p->AusgP];
	p->AusgP = PufferNP(p->AusgP);
	SREG = SregTemp;
	return res;
	}
	

#ifndef FUER_TW39
	
//! liefert Anzahl Zeichen im Puffer.
// ------------------------------------
//! \param p Zeiger auf den Puffer.
//! \returns Anzahl Zeichen im Puffer.
uint8_t PufferAnzahl(TPuffer *p)
	{
	if (p->SpeichP >= p->AusgP)
		// Normalfall
		return p->SpeichP - p->AusgP;
	else
		// genutzer Speicher "geht ein mal rum"
		return (p->SpeichP +  MaxPuffer) - p->AusgP;
	}

#endif //ndef FUER_TW39
