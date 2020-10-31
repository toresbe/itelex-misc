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
//! Das Ergebnis wird in der globalen Variable #Tastendruck gespeichert.
void TastePruefen()
	{
	switch (TasteZustandIntern)
		{
		case TasteAus:
			if (get_TASTE())
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
			if (get_TASTE())
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
			if (get_TASTE()) 
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
//! \retval true Taste wurde lang gedrückt.
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


