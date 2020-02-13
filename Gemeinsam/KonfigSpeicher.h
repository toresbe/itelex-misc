#ifndef __KONFIGSPEICHER_H__

#define __KONFIGSPEICHER_H__

#include <inttypes.h>
#include <bool.h>


// Fehlerkonstanten
enum { 
	KonfigSpeicherOK = 0,
	KonfigSpeicherNichtInit = 1,
	KonfigSpeicherLesefehler = 2, // durch inkonsistenz offenbart
	KonfigSpeicherSchreibfehler = 3 };


void KonfigSpeicherInit();

uint8_t KonfigLeseByte(uint8_t Adresse, uint8_t Default);

uint8_t KonfigLeseByteBegrenzt(uint8_t Adresse, uint8_t Default, uint8_t Min, uint8_t Max);


void KonfigSchreibeByte(uint8_t Adresse, uint8_t Wert);


uint8_t KonfigSpeicherFehlercode(bool Loeschen);

uint8_t KonfigSpeicherFehlerAdresse();


#endif //ndef __KONFIGSPEICHER_H__
