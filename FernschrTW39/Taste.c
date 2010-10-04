#include "Taste.h"

#include <avr/io.h>
#include "bits.h"

#include "MsTimer.h"

#include "ports.h"

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
			if (!BIT_IS_SET(TAST_IPORT, TAST_BIT))
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
			if (!BIT_IS_SET(TAST_IPORT, TAST_BIT))
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
			if (!BIT_IS_SET(TAST_IPORT, TAST_BIT))
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





