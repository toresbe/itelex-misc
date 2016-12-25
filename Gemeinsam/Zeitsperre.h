#ifndef __ZEITSPERRE_H__

#define __ZEITSPERRE_H__

#include <stdbool.h>

extern bool SperrzeitAktiv();

extern void SperrzeitInit();

extern bool SperrzeitEingabeDialog();

extern void SperrzeitLadeEeprom();

extern void SperrzeitSpeicherEeprom();


#endif //ndef __ZEITSPERRE_H__
