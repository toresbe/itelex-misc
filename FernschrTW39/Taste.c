#include "Taste.h"

#include <avr/io.h>
#include <avr/wdt.h>

#include "bits.h"

#include "MsTimer.h"

#include "Ports.h"


//! Speichert den letzten Zustand der Taste.
static enum { TasteAus, TasteEin, TasteSperr } TasteZustandIntern;

//! Registriert die Dauer des Tastendrucks.
static TMsTimer TasteTimer;

//! Speichert den letzten Tastendruck. Muss vom Anwender wieder auf NichtGedr gesetzt werden.
volatile TTastendruck Tastendruck;


//! Zyklisch aufrufen, um die Bedienung der Taste registrieren zu lassen.
//-------------------------------------------------------------------------
//! Das Ergebnis wird in der globalen Variable \sa Tastendruck gespeichert.
void TastePruefen()
	{
	switch (TasteZustandIntern)
		{
		case TasteAus:
#ifdef TASTE_NACH_PLUS
			if (get_TASTE()) // Gedrückt = HIGH!
#else
			if (!get_TASTE()) // Gedrückt = LOW!
#endif
				{
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
#ifdef TASTE_NACH_PLUS
			if (get_TASTE()) // Gedrückt = HIGH!
#else
			if (!get_TASTE()) // Gedrückt = LOW!
#endif
				{
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
#ifdef TASTE_NACH_PLUS
			if (get_TASTE()) // Gedrückt = HIGH!
#else
			if (!get_TASTE()) // Gedrückt = LOW!
#endif
				{
				// immer noch gedrückt...
				}
			else
				{ 
				TasteZustandIntern = TasteAus;
				}
			break;

		} // switch (TasteZustandIntern)
	}


//! Wartet, bis die Taste einmal gedrückt wurde.
//-------------------------------------------------------------------------
//! \retval false Taste wurde kurz gedrückt.
//! \retval false Taste wurde lang gedrückt.
bool WarteTaste()
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


