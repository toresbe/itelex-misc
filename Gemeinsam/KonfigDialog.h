#ifndef __KONFIGDIALOG_H__

#define __KONFIGDIALOG_H__

#include <inttypes.h>

#include "bool.h"

uint8_t LokalZahlEingabe(uint8_t* z, uint8_t maxzif);

uint8_t LokalBoolEingabe(bool* b);

uint8_t LokalTextEingabe(char* s, uint8_t maxbuchst);

bool KonfigurationAllgemein();

#endif //ndef __KONFIGDIALOG_H__
