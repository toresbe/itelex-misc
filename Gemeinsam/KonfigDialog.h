#ifndef __KONFIGDIALOG_H__

#define __KONFIGDIALOG_H__

#include <inttypes.h>

#include <avr/pgmspace.h>

#include <stdbool.h>


// Für die Dialogausgabe und Eingabe werden nur die folgenden zwei Funktionen verwendet:
extern char LokalZeichenLesen();
// Diese Funktion muss in der Anwendung definiert werden und sollte nur Kleinbuchstaben zurückgeben.
// Dies ist bei CodeZuZeichen() auf jeden Fall der Fall.
// Bei Tastendruck soll '\t' geliefert werden, bei Abbruch '\0'.

extern void LokalZeichenAusgabe(char c);
// Diese Funktion muss in der Anwendung definiert werden.


extern PROGMEM const char OkStrP[];

extern PROGMEM const char NeuStrP[];

extern int8_t LokalZahlEingabe(uint8_t* z, uint8_t maxzif);

extern uint8_t LokalBoolEingabe(bool* b);

extern uint8_t LokalTextEingabe(char* s, uint8_t maxbuchst);

extern bool KonfigurationAllgemein();


#endif //ndef __KONFIGDIALOG_H__
