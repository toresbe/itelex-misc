#ifndef __FIFOPUFFER_H__

#define __FIFOPUFFER_H__

#include <inttypes.h>
#include <stdbool.h>


#ifndef FIFOPUFFER_MAX
enum { MaxPuffer = 50 } ; 
#else 
enum { MaxPuffer = FIFOPUFFER_MAX } ; 
#endif


typedef volatile struct {
	uint8_t Puffer[MaxPuffer]; //!< enthält Baudot-Codes oder im Wahlzustand die Wahlziffern.
	uint8_t SpeichP; //!< Index für das Befüllen des Puffers.
	uint8_t AusgP; //!< Index für das Auslesen des Puffers.
	} TPuffer;


extern void PufferInit(TPuffer *p);
	
extern bool PufferSpeich(TPuffer *p, uint8_t code);
	
extern bool PufferLeer(TPuffer *p);

extern bool PufferVoll(TPuffer *p);
	
extern uint8_t PufferAusg(TPuffer *p);	

extern uint8_t PufferAnzahl(TPuffer *p);

extern uint8_t PufferZeig(TPuffer *p);


#endif //ndef __FIFOPUFFER_H__
