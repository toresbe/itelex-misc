#ifndef __TASTE_H__

#define __TASTE_H__

#include <inttypes.h>

typedef enum { NichtGedr, Kurz, Lang } TTastendruck;

extern volatile TTastendruck Tastendruck;

extern void TastePruefen();

#endif //ndef __TASTE_H__
