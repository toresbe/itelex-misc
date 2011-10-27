#include "Taste.h"

#include <avr/io.h>
#include <avr/wdt.h>

#include "bits.h"

#include "MsTimer.h"

#include "Ports.h"

enum { TasteAus, TasteEin, TasteSperr } TasteZustandIntern;

TMsTimer TasteTimer;

volatile TTastendruck Tastendruck;


// Bedientaste
// -----------

void TastePruefen()
	{
	switch (TasteZustandIntern)
		{
		case TasteAus:
			if (!get_TASTE())
				{ // Gedrückt = LOW
				if (TimerVal(&TasteTimer) > 50)
					{ // ausreichend lang gedrückt
					TasteZustandIntern = TasteEin;
					StartTimer(&TasteTimer);
					}
				}
			else
				{
				StartTimer(&TasteTimer);
				}
			break;

		case TasteEin: 
			if (!get_TASTE())
				{ // Gedrückt = LOW
				if (TimerVal(&TasteTimer) > 800) // 0,8 Sek. = lang
					{ // lang gedrückt
					TasteZustandIntern = TasteSperr;
					Tastendruck = Lang;
					}
				}
			else
				{ // Taste wieder früh losgelassen
				TasteZustandIntern = TasteAus;
				Tastendruck = Kurz;
				}
			break;

		case TasteSperr:
			if (!get_TASTE())
				{ // Gedrückt = LOW
				// immer noch gedrückt...
				}
			else
				{ 
				TasteZustandIntern = TasteAus;
				}
			break;

		} // switch (TasteZustandIntern)
	}


bool WarteTaste()
	// True: Lang gedrückt...
	{ 
	Tastendruck = NichtGedr;
	while (Tastendruck == NichtGedr)
		{
		// Hier Projektabhängige Standard-Polling Prozeduren einfügen
		wdt_reset();
		TastePruefen();
		}
	bool Res = (Tastendruck == Lang);
	Tastendruck = NichtGedr;
	return Res;
	}


	
