#ifndef __KONFIGSPEICHER_H__

#define __KONFIGSPEICHER_H__

#include <inttypes.h>
#include <stdbool.h>
#include <avr/pgmspace.h>


// Fehlerkonstanten
enum { 
	KonfigSpeicherOK = 0,
	KonfigSpeicherNichtInit = 1,
	KonfigSpeicherLesefehler = 2, // durch inkonsistenz offenbart
	KonfigSpeicherLesefehlerSchwer = 3, // dreifache inkonsistenz
	KonfigSpeicherSchreibfehler = 4 };


void KonfigSpeicherInit();


uint8_t KonfigLeseByte(uint16_t Adresse, uint8_t Default);

uint8_t KonfigLeseByteBegrenzt(uint16_t Adresse, uint8_t Default, uint8_t Min, uint8_t Max);

uint16_t KonfigLeseWortBegrenzt(uint16_t Adresse, uint16_t Default, uint16_t Min, uint16_t Max);

bool KonfigLeseBool(uint16_t Adresse, bool Default);

void KonfigLeseString(uint16_t Adresse, char *s, uint8_t len, PGM_P defstr);


void KonfigSchreibeByte(uint16_t Adresse, uint8_t Wert);

void KonfigSchreibeWort(uint16_t Adresse, uint16_t Wert);

void KonfigSchreibeBool(uint16_t Adresse, bool Wert);

void KonfigSchreibeString(uint16_t Adresse, char *s, uint8_t len);


uint8_t KonfigSpeicherFehlercode(bool Loeschen);

uint16_t KonfigSpeicherFehlerAdresse();


void KonfigSpeicherFehlerAusgeben();


#endif //ndef __KONFIGSPEICHER_H__
