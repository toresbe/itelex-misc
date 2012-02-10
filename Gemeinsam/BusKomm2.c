#include <inttypes.h>
#include <avr/wdt.h>

#include <bool.h>
#include "TwiEvents.h"

#include "TxP2-Defs.h"
#include "BusKomm.h"
#include "MsTimer.h"


void WarteSchlussQuittung(uint16_t MaxTimer)
	{
	TMsTimer Timer;
	
	StartTimer(&Timer);
	while (true)
		{
		uint8_t Code;

		if (GetEmpfByte(&Code)) 
			{
			if (Code == BusQuittSchluss)
				break;
			}
		if (TimerVal(&Timer) > MaxTimer) 
			break;
		wdt_reset(); // nach Schluss-Kommando keine Lebenszeichen mehr...
		}
	}

	
static TMsTimer LebenszTimer;


void SendeLebenszeichen()
	{
	if (BusVerbPartner == 0)
		StartTimer(&LebenszTimer);
	else if (TimerVal(&LebenszTimer) > 674 && BusFrei && (BusAuftrag == Nichts || BusAuftrag == Fertig))
		{
		BusSenden(BusLebenszeichen);
		StartTimer(&LebenszTimer);
		}
	}
	
