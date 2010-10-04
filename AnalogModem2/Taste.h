#ifndef __TASTE_H__

#define __TASTE_H__

#include <inttypes.h>
#include <bool.h>

typedef enum { NichtGedr, Kurz, Lang } TTastendruck;

extern volatile TTastendruck Tastendruck;

extern void TastePruefen();

extern bool WarteTaste();
	// True: Lang gedrückt...

#endif //ndef __TASTE_H__
