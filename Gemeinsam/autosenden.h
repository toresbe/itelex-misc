#ifndef __AUTOSENDEN_H__

#define __AUTOSENDEN_H__

#include <avr/pgmspace.h>

#include "bool.h"

void AutoSendenTextP(PGM_P s);

void AutoSendenText(char* s);

bool AutoSendenFertig();

#endif //ndef __AUTOSENDEN_H__
