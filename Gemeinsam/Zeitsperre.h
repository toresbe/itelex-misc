#ifndef __ZEITSPERRE_H__

#define __ZEITSPERRE_H__

#include <stdbool.h>
#include <inttypes.h>
#include <avr/eeprom.h>


typedef uint16_t TSperrzeitDaten[10]; // nur im EEPROM verwenden

extern bool SperrzeitAktiv();

extern void SperrzeitInit();

extern bool SperrzeitEingabeDialog();

extern void SperrzeitLadeEeprom(TSperrzeitDaten *eedat);

extern void SperrzeitSpeicherEeprom(TSperrzeitDaten *eedat);


#endif //ndef __ZEITSPERRE_H__
