#ifndef __FERNDIALOG_H__

#define __FERNDIALOG_H__

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <inttypes.h>

#include <stdbool.h>

//! Wird in der Schleife beim Fern-Dialog aufgerufen.
//--------------------------------------------------
//! Muss von der jeweiligen Anwendung definiert werden.
extern void FernDialogCallback();

extern bool FernDialogVerbinden(uint8_t SucheStartAdresse);

// alle folgenden Funktionen liefern false, wenn ein 
// "unerwarteter" Code auf dem Bus empfangen wurde. Also alles
// außer Mark und Space und Lebenszeichen

// Achtung: Bei Benutzung von CodeEmpfangenFern und CodeAusgabeFern
// kann die ASCII-Umwandlung für alle anderen Funktionen versagen.

extern bool CodeEmpfangenFern(uint8_t *c);

extern bool ZeichenEmpfangenFern(char *c);

extern bool BoolEmpfangenFern(bool *b);

extern uint8_t ZahlEmpfangenFernAnzahlZiffern;

extern bool ZahlEmpfangenFern(uint8_t *n);

extern bool CodeAusgabeFern(uint8_t code);

extern bool ZeichenAusgabeFern(char c);

extern bool TextAusgabeFern(PGM_P s);

extern bool BoolAusgabeFern(bool b);

extern bool ZahlAusgabeFern(uint8_t n, uint8_t Ziffern);

extern bool ZahlAbfrageFern(PGM_P Prompt, uint8_t *Wert, uint8_t Ziffern);

extern bool BitAbfrageFern(PGM_P Prompt, uint8_t *Wert, uint8_t Mask);


#endif //ndef __FERNDIALOG_H__
