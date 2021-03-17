// das Ganze nur, wenn es nicht abgeschaltet ist

//! \todo DOXYGEN noch nicht ganz fertig (Debugging fehlt)

#include <inttypes.h>
#include <avr/io.h>
#include <stdbool.h>

#include "bits.h"

#include "BaudotCode.h"
#include "MsTimer.h"

#include "PortsKomm.h"

#include "SwTwi.h"


// Schalter für Code-Varianten
// ===========================

//#define EXTEEPROM_DEBUG
	//!< Wenn definiert, kann interaktiv das Externe EEPROM ausgelesen werden.

//#define SWTWI_SHOWMSG
	// !< wenn definiert, wird der Eeprom-Zugriff protokolliert.


// Ausgabe-Funktionen für's Debuggen
// =================================

/*
extern void LokalZahlAusgabe16(uint16_t i, int8_t minzif);
extern void LokalZahlAusgabe(uint8_t i, int8_t minzif);
extern void LokalZeichenAusgabe(char c);
extern void LokalHexAusgabe(uint8_t i);
extern void SerSendFlush();
*/


// ####################################################################################
// Software-TWI-Code
// =================
// Allgemein für alle Arten von TWI-Bausteinen
// ####################################################################################

#ifdef SWTWI_DEBUG 

// nur zur Verwendung im Sowftware-Debugger!
// =========================================

#define SDA_0 CLR_BIT(PORTB, 7)	
#define SDA_1 SET_BIT(PORTB, 7)
#define SDA_in BIT_IS_SET(PINB, 7)

#define SCL_0 CLR_BIT(PORTB, 6)
#define SCL_1 SET_BIT(PORTB, 6)
#define SCL_in BIT_IS_SET(PINB, 6)

#else //SWTWI_DEBUG

/* ALT OHNE externe Pullup-Widerstände

#define TWI_0(p,b) do { (p ## _OPORT) &= ~(1<<(b)); (p ## _DDR) |= (1<<(b)); } while (0)
#define TWI_1(p,b) do { (p ## _DDR) &= ~(1<<(b)); (p ## _OPORT) |= (1<<(b)); } while (0)

*/

#define SDA_0 clro_SWTWI_SDA()
#define SDA_1 inp_SWTWI_SDA()
#define SDA_in get_SWTWI_SDA()

#define SCL_0 clro_SWTWI_SCL()
#define SCL_1 inp_SWTWI_SCL()
#define SCL_in get_SWTWI_SCL()

#endif //SWTWI_DEBUG


#define BitDebugOut(x)



//! regelmäßig aufrufen für Software-TWI.
//------------------------------------------
//! maximale TWI-Frequenz = halbe Aufruf-Frequenz
void SwTwiAktion(T_SwTwiTransferdaten *p)
	{
	switch (p->Phase)
		{
		// Start-Condition
		// ---------------
		case 0: // Bei Start: SDA auf 0
			if (p->AnzDaten == 0)
				break; // nix zu tun
			p->ByteNr = 0;
			p->BitNr = 7;
			p->AktByte = p->Adresse;
			p->Ergebnis = 0;
			SDA_0;
			p->Phase = 1;
			break;

		case 1: // Start 1: SDA 0 überprüfen
			if (!SDA_in)
				p->Phase = 2;
			break;

		case 2: // Start 2: SCL auf 0
			SCL_0;
			p->Phase = 3;
			break;

		case 3: // Start 3: SCL 0 überprüfen
			if (!SCL_in)
				p->Phase = 10;
			break;

		// Adresse oder Daten ausgeben
		// ---------------------------
		case 10: // Datenbit Ausgabe 1: SDA setzen
			if (p->AktByte & (1<<p->BitNr))
				{
				SDA_1;
				BitDebugOut('1');
				}
			else
				{
				SDA_0;
				BitDebugOut('0');
				}
			p->Phase = 11;
			break;
			
		case 11: // Datenbit Ausgabe 2: SDA prüfen, SCL auf 1
			if (p->AktByte & (1<<p->BitNr))
				{
				if (!SDA_in) break;
				}
			else
				{
				if (SDA_in) break;
				}
			SCL_1;
			p->Phase = 12;
			break;
			
		case 12: // Datenbit Ausgabe 3: SCL 1 prüfen
			if (SCL_in)
				p->Phase = 13;
			break;
			
		case 13: // Datenbit Ausgabe 4: SCL auf 0
			SCL_0;
			p->Phase = 14;
			break;
			
		case 14: // Datenbit Ausgabe 5: SCL 0 prüfen
			if (SCL_in)
				break;
			if (p->BitNr == 0)
				{ // gerade wurde Bit0 gesendet, jetzt ACK prüfen
				p->Phase = 20;
				}
			else
				{
				p->BitNr--;
				p->Phase = 10;
				}
			break;
			
		// ACK prüfen
		// ---------------
		case 20: // ACK Empfang 1: SDA und SCL auf 1
			SDA_1;
			SCL_1;
			p->Phase = 21;
			break;
			
		case 21: // ACK Empfang 2: SCL prüfen
			if (SCL_in)
				p->Phase = 22;
			break;
			
		case 22: // ACK Empfang 3: SDA auswerten, SCL auf 0
			if (SDA_in) // DEBUGBREAK beim Debuggen PINB(1) auf 0
				{ // kein ACK empfangen
				p->Phase = 50;
				BitDebugOut('N');
				}
			else
				{ // ACK empfangen
				p->AktByte = p->Puffer[p->ByteNr]; // Vorbereitend, falls weiter gesendet wird
				p->ByteNr++;
				p->Ergebnis = p->ByteNr;
				p->BitNr = 7;
				p->Phase = 23;
				BitDebugOut('A');
				}
			SCL_0;
			break;

		case 23: // ACK Empfang 5: SCL 0 prüfen
			if (SCL_in)
				break;
			if (p->ByteNr > p->AnzDaten)
				p->Phase = 50;
			else 
				if ((p->Adresse & 1) == 0) // Bit 0 in Adresse = 0 -> Write, = 1 -> Read
					p->Phase = 10;
				else
					p->Phase = 30;
			break;

		// Daten lesen
		// -----------
		case 30: // Datenbit Empfang 1: SCL auf 1
			if (p->BitNr == 7)
				p->AktByte = 0;
			SDA_1;
			SCL_1;
			p->Phase = 31;
			break;
			
		case 31: // Datenbit Empfang 2: SCL 1 prüfen
			if (SCL_in)
				p->Phase = 32;
			break;
			
		case 32: // Datenbit Empfang 3: SDA auswerten, SCL auf 0
			if (SDA_in) // DEBUGBREAK beim Debuggen PINB(1) auf 1 oder 0
				{ 
				p->AktByte |= (1<<p->BitNr);
				BitDebugOut('+');
				}
			else
				{
				BitDebugOut('-');
				}
			SCL_0;
			p->Phase = 33;
			break;
		
		case 33: // Datenbit Empfang 4: SCL 0 prüfen
			if (SCL_in)
				break;
			if (p->BitNr == 0)
				{ // gerade wurde Bit0 empfangen, jetzt ACK senden
				p->Phase = 40;
				}
			else
				{
				p->BitNr--;
				p->Phase = 30;
				}
			break;
			
		// ACK-Ausgabe beim Lesen
		// ----------------------
		case 40: // ACK Ausgabe 1: SDA auf 0, außer bei letztem Byte
			// Empfangene Daten speichern: (Byte-Nr steht auf 1 beim ersten Byte)
			p->Puffer[p->ByteNr - 1] = p->AktByte; 
			if (p->ByteNr < p->AnzDaten) 
				{
				SDA_0;
				BitDebugOut('a');
				}
			else
				{
				SDA_1;
				BitDebugOut('n');
				}
			p->Phase = 41;
			break;
			 
		case 41: // ACK Ausgabe 2: SDA prüfen, SCL auf 1
			if (p->ByteNr < p->AnzDaten) 
				{ // SDA soll 0 sein
				if (SDA_in) break;
				}
			else
				{
				if (!SDA_in) break;
				}
			SCL_1;
			p->Phase = 42;
			break;
			
		case 42: // ACK Ausgabe 3: SCL 1 prüfen
			if (SCL_in)
				p->Phase = 43;
			break;
			
		case 43: // ACK Ausgabe 4: SCL auf 0
			SCL_0;
			p->Phase = 44;
			break;
			
		case 44: // ACK Ausgabe 5: SCL 0 prüfen
			if (SCL_in)
				break;
			SDA_1; // auf 1 zu prüfen ist sinnlos, da 0 vom slave gesendet werden könnte
			p->ByteNr++;
			p->BitNr = 7;
			p->Ergebnis = p->ByteNr;
			if (p->ByteNr > p->AnzDaten)
				{
				p->Phase = 50;
				}
			else
				{
				p->Phase = 30;
				}
			break;
		
		// Stop-Condition
		// --------------
		case 50: // Stop 1: SDA auf 0
			SDA_0;
			p->Phase = 51;
			break;
			
		case 51: // Stop 2: SDA 0 prüfen
			if (!SDA_in)
				p->Phase = 52;
			break;
			
		case 52: // Stop 3: SCL auf 1
			SCL_1;
			p->Phase = 53;
			break;
			
		case 53: // Stop 4: SCL 1 prüfen
			if (SCL_in)
				p->Phase = 54;
			break;
			
		case 54: // Stop 5: SDA auf 1
			SDA_1;
			p->Phase = 55;
			break;
			
		case 55: // Stop 6: SDA 1 prüfen
			if (SDA_in)
				{
				p->Phase = 255;
				}
			break;

		// -----------------------------------------
		// Rücksetz-Prozedur: 20 Bits mit SDA = 1 senden. Damit sollte jeder Slave den Bus wieder freigeben.
		case 60: // Rücksetz-Prozedur Start 1: SDA auf 0
			p->BitNr = 20;
			SDA_0;
			p->Phase = 61;
			break;
		
		case 61: // Rücksetz-Prozedur Start 2: SDA 0 überprüfen
			if (!SDA_in)
				p->Phase = 62;
			break;
		
		case 62: // Rücksetz-Prozedur Start 3: SCL auf 0
			SCL_0;
			p->Phase = 63;
			break;

		case 63: // Rücksetz-Prozedur Start 4: SCL 0 überprüfen
			if (!SCL_in)
				p->Phase = 64;
			break;

		case 64: // Rücksetz-Prozedur Start 5: SDA auf 1
			SDA_1;
			p->Phase = 65;
			break;

		case 65: // Rücksetz-Prozedur Ausgabe 1: SCL auf 1
			SCL_1;
			p->Phase = 66;
			break;

		case 66: // Rücksetz-Prozedur Ausgabe 2: SCL 1 überprüfen
			if (SCL_in)
				p->Phase = 67;
			break;

		case 67: // Rücksetz-Prozedur Ausgabe 3: SCL auf 0
			SCL_0;
			p->Phase = 68;
			break;

		case 68: // Rücksetz-Prozedur Ausgabe 4: SCL 0 überprüfen
			if (!SCL_in)
				{
				if (p->BitNr > 0)
					{
					p->BitNr--;
					p->Phase = 65;
					}
				else
					{
					p->ByteNr = 0;
					p->AnzDaten = 0;
					p->Ergebnis = 1;
					p->Phase = 50; // Stop-Condition
					}
				}
			break;
					
		}
	}


void InitSwTwi()
	{
	init_SWTWI_SDA();
	init_SWTWI_SCL();
	}


