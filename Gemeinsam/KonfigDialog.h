#ifndef __KONFIGDIALOG_H__

#define __KONFIGDIALOG_H__

#include <inttypes.h>

#include <avr/pgmspace.h>

#include <stdbool.h>

extern PROGMEM char OkStrP[];

extern uint8_t LokalZahlEingabe(uint8_t* z, uint8_t maxzif);

extern uint8_t LokalBoolEingabe(bool* b);

extern uint8_t LokalTextEingabe(char* s, uint8_t maxbuchst);

extern bool KonfigurationAllgemein();


#endif //ndef __KONFIGDIALOG_H__
