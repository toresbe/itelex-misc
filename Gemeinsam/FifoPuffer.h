#ifndef __FIFOPUFFER_H__

#define __FIFOPUFFER_H__

#include <inttypes.h>
#include <bool.h>

enum { MaxPuffer = 50 } ; 

typedef volatile struct {
	uint8_t Puffer[MaxPuffer]; // enthält Baudot-Codes oder im Wahlzustand die Wahlziffern
	bool ModeZiffern; // Umschaltung Bu/Zi beim Füllen bzw. Auslesen
	uint8_t SpeichP, AusgP;
	} TPuffer;

extern TPuffer SendePuffer, EmpfPuffer;


extern void PufferInit(TPuffer *p);
	
extern bool PufferSpeich(TPuffer *p, uint8_t code);
	
inline bool PufferLeer(TPuffer *p);

inline bool PufferVoll(TPuffer *p);
	
extern uint8_t PufferAusg(TPuffer *p);	
 
extern uint8_t PufferAnzahl(TPuffer *p);

#endif //ndef __FIFOPUFFER_H__
