#ifndef __ZEITSPERRE_H__

#define __ZEITSPERRE_H__

#include <stdbool.h>
#include <inttypes.h>


extern bool SperrzeitAktiv();

extern void SperrzeitInit();

extern void SperrzeitAussetzen();

extern bool SperrzeitEingabeDialog();

extern void SperrzeitLadeEeprom(uint16_t Adresse);

extern void SperrzeitSpeicherEeprom(uint16_t Adresse);


#endif //ndef __ZEITSPERRE_H__
