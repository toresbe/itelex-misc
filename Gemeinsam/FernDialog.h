#ifndef __FERNDIALOG_H__

#define __FERNDIALOG_H__

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <inttypes.h>

#include "Bool.h"


extern void FernDialogCallback();
// Funktion, die in anderen Modulen definiert werden muss
// wird in der Empfangs- und Sendeschleife aufgerufen
// z.B. um die LED zu aktualisieren

extern bool FernDialogVerbinden(uint8_t SucheStartAdresse);

// alle folgenden Funktionen liefern false, wenn ein 
// "unerwarteter" Code auf dem Bus empfangen wurde. Also alles
// außer Mark und Space und Lebenszeichen

extern bool CodeEmpfangenFern(uint8_t *c);
// false bei Verbindungsende

extern bool ZeichenEmpfangenFern(char *c, char *BuZiMode);
// false bei Abbruch

extern bool BoolEmpfangenFern(bool *b);

extern uint8_t ZahlEmpfangenFernAnzahlZiffern;

extern bool ZahlEmpfangenFern(uint8_t *n);

extern bool CodeAusgabeFern(uint8_t code);
// gibt an über Bus verbundenes Endgerät einen Baudot-Code aus...
// Rückgabe true, wenn kein Abbruch

extern bool ZeichenAusgabeFern(char c, char *BuZiMode);
// gibt an über Bus verbundenes Endgerät einen Buchstaben aus...
// Rückgabe true, wenn kein Abbruch

extern bool TextAusgabeFern(PGM_P s);

extern bool BoolAusgabeFern(bool b);

extern bool ZahlAusgabeFern(uint8_t n, uint8_t Ziffern);

extern bool ZahlAbfrageFern(PGM_P Prompt, uint8_t *Wert, uint8_t Ziffern);

extern bool BitAbfrageFern(PGM_P Prompt, uint8_t *Wert, uint8_t Mask);


#endif //ndef __FERNDIALOG_H__
