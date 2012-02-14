#include <inttypes.h>
#include <avr/wdt.h>

#include <bool.h>
#include "TwiEvents.h"

#include "TxP2-Defs.h"
#include "BusKomm.h"
#include "MsTimer.h"


//! Wartet nach Verbindungsabbau auf Quittung der Gegenstelle.
//------------------------------------------------------------
//! Vorher sollte BusKdoSchluss gesendet worden sein.
//! \param MaxTimer maximale Wartezeit in Timer-Einheiten.
 
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


//! Interne Variable für die Überwachung des Zeitintervalls der Lebenszeichen
//! auf dem TWI-Bus.
	
static TMsTimer LebenszTimer;


//! Sendet Lebenszeichen bei Bedarf.
//----------------------------------
//! Prüft, ob seit dem letzten Senden eines Lebenszeichens es wieder Zeit ist,
//! ein neues Lebenszeichen zu senden. Ist kein Verbindungspartner aktiv, wird
//! nichts gesendet. Ebenso wird nichts gesendet, wenn noch TWI-Kommandos auf die 
//! Übermittlung warten.

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
	
