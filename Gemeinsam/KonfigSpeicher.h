#ifndef __KONFIGSPEICHER_H__

#define __KONFIGSPEICHER_H__

#include <inttypes.h>
#include <stdbool.h>


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

bool KonfigLeseBool(uint16_t Adresse, bool Default);


void KonfigSchreibeByte(uint16_t Adresse, uint8_t Wert);

void KonfigSchreibeBool(uint16_t Adresse, bool Wert);


uint8_t KonfigSpeicherFehlercode(bool Loeschen);

uint16_t KonfigSpeicherFehlerAdresse();


#endif //ndef __KONFIGSPEICHER_H__
