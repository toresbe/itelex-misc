#ifndef __TASTE_H__

#define __TASTE_H__

#include <inttypes.h>
#include <bool.h>


typedef enum { 
	NichtGedr, //!< nicht gedrückt.
	Kurz, //!< kurz gedrückt ( < 0,8 Sekunden)
	Lang  //!< lang gedrückt ( > 0,8 Sekunden)
	} TTastendruck; //!< Art des Tastendrucks

	
extern volatile TTastendruck Tastendruck;

extern void TastePruefen();

extern bool WarteTaste();
	// True: Lang gedrückt...

#endif //ndef __TASTE_H__
