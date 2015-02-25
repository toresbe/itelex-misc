#ifndef __LOKALUHR_H__

#define __LOKALUHR_H__

#include <inttypes.h>
#include <stdbool.h>

extern void LokalUhrInit();

extern bool LokalUhrPruefeRundsendung(uint8_t *buf, uint8_t anz);

extern uint8_t LokalUhrBaudotAusgabe(uint8_t *buf);

#endif // __LOKALUHR_H__
