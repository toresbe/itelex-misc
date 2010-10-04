#ifndef __KONFIGDIALOG_H__

#define __KONFIGDIALOG_H__

#include <inttypes.h>

#include "bool.h"

uint8_t LokalZahlEingabe(uint8_t* z, uint8_t maxzif);
	// return: 0 = abbruch, 1 = unverändert, 2 = eingabe erfolgt
	// kann nur positive Zahlen

uint8_t LokalBoolEingabe(bool* b);
	// return: 0 = abbruch, 1 = unverändert, 2 = eingabe erfolgt

uint8_t LokalTextEingabe(char* s, uint8_t maxbuchst);
	// return: 0 = abbruch, 1 = unverändert, 2 = eingabe erfolgt
	// Abschluss nur mit CR oder LF, nur CR oder LF = alter wert, nur Leer = löschen
	// Leer am Anfang wird ignoriert

bool KonfigurationAllgemein();

#endif //ndef __KONFIGDIALOG_H__
