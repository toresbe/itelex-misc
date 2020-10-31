// Signalzusatz für Martin


// standard libs:
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/wdt.h>
#include <inttypes.h>

// sonni's libs:
#include "bits.h"
#include "timercs.h"
#include "TwiEvents.h"
#include "MsTimer.h"
//#include "EepromTools.h"

// Txp2 libs:
#include "TxP2-Defs.h"
#include "BusKomm.h"
#include "../SvnVersion.h"

// Project includes:
#include "Ports.h"


//! Identificator in flash memory
const char PROGMEM Identifier[] = "___itlx_SignalZusatz-" PROGIDZUSATZ "___" __DATE__ "___" __TIME__ "___" SVNVERSION "___";


// Typen
// =====

typedef struct
{
	uint8_t Anschluss; //!< welches Modul überwachen
	uint8_t StatusMaske; //!< Welche Bits des Status-Worts auswerten
	uint8_t StatusWertEin; //!< Bei welchem Status-Wert Ausgang einschalten
	uint8_t StatusWertAus; //!< Bei welchem Status-Wert Ausgang ausschalten, wenn dieser Wert 
		//!< identisch mit #StatusWertEin dann wird bei jedem anderen Wert als #StatusWertEin wieder ausgeschaltet.
	uint8_t PruefTaktBeiAus; //!< in 1/10 sek
	uint8_t PruefTaktBeiEin; //!< in 1/10 sek
	uint8_t PruefTaktBeiWechsel; //!< in 1/10 sek
	uint8_t WechselPruefAnzahl;
	uint8_t AusgabeArt; //! 0=direkt, 1=Einschalt-Wischer, 2=Ausschalt-Wischer
	uint8_t WischerZeit; //!< in 1/10 sek
} TSignalParameter;


typedef struct 
{
	TMsTimer PruefTimer;
	uint16_t PruefTimerEnde;
	uint8_t WechselZaehler;
	bool AktuellStatus; 
	TMsTimer AusgabeTimer; // 
} TSignalArbeitsdaten;
	
	

// Constants
// =========



// internal Eeprom
// ===============

EEMEM uint8_t Spacer[4]; //!< Start of EEPROM sometimes disturbed
EEMEM uint8_t OwnAddress_EE = 99 << 1; //!< copy of #BusEigenAdresse in EEPROM
EEMEM uint8_t DefaultStatus_EE = 0xB0; //!< copy of #DefaultStatus in EEPROM


// Variables
// =========

TSignalParameter SigPar[] = { { 110, 0xF0, 0x20, 0x20, 50, 50, 5, 3, 0, 0 }, 
							  { 109, 0xF0, 0x20, 0x20, 50, 50, 5, 3, 0, 0 } } ;
							  
enum { SigAnzahl = sizeof(SigPar) / sizeof(TSignalParameter) } ;

TSignalArbeitsdaten SigArbd[SigAnzahl];


static void InitVariables()
	{
	// Init from EEPROM
	// ----------------
	//TODO
	
	// Init other variables (static)
	// -----------------------------
	
	} // InitVariables()



#define TIMER0_CS TCCR_DIV(0, 8)
#define TIMER0_PRESCALER 8
#define TIMER0_FREQ (F_CPU / TIMER0_PRESCALER)

// f_Timer0OVF = (f_CPU / Prescaler) / (256 - Startwert)
// --> Startwert = 256 - (f_CPU / Prescaler) / f_Timer0OVF

#define TIMER0_OVFFREQ 10000
#define TIMER0_START (256 - TIMER0_FREQ / TIMER0_OVFFREQ)


static void InitPorts()
	{
	// Timer
/* i don't need it, do i???
#ifdef TCCR0A
	TCCR0A = 0;
	TCCR0B = TIMER0_CS; 
	SET_BIT(TIMSK0, TOIE0);
#else
	TCCR0 = TIMER0_CS;
	SET_BIT(TIMSK, TOIE0);
#endif //def TCCR0A
*/

	// TWI
	TwiInit(); // but it's not activated yet
	
	// Timer
	MsTimerInit();

	// Watchdog
	//wdt_enable(WDTO_2S); //! \todo check if software watchdog would be a better solution
	
	}


static void TwiActivate()
	{
	TMsTimer Timer;
	
	StartTimer(&Timer);
	while (TimerVal(&Timer) < 100)
		;
	
	TWCR = (1<<TWINT) | (0<<TWEA) | (0<<TWSTA) | (1<<TWSTO) | (0<<TWEN) | (0<<TWIE);

	while (TimerVal(&Timer) < 200)
		;
		
	TWCR = (1<<TWINT) | (1<<TWEA) | (0<<TWSTA) | (0<<TWSTO) | (1<<TWEN) | (1<<TWIE);
	}
	

//! Check the TWI code for need to update the Status bits.


//! Main Programm

int main()
	{
	// initializing everything
	InitVariables();

	InitPorts();
	
	sei();
	
	TwiActivate();
	
	while (true)
		{
		}
	} // main()


