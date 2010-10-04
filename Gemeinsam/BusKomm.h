#ifndef __BUSKOMM_H__

#define __BUSKOMM_H__

#include <inttypes.h>
#include <avr/interrupt.h>
#include <bool.h>

#include "bits.h"

// Status-Variablen
// ----------------

extern volatile uint8_t Status;

enum { StatBit_Frei				= 7	}; // keine Verbindung besteht (weder innen noch auﬂen)
enum { StatBit_BusKdoEmpfangen	= 6	}; // Empfangenes Kommando noch nicht abschlieﬂend bearbeitet
enum { StatBit_Verbunden		= 5	}; // Verbindung ist komplett hergestellt
enum { StatBit_AngerufenBelegt	= 4	}; // Fernschreiber wurde von anderem Partner (intern oder extern) aktiviert (darf nur bei StatBit_Frei = 1 gesetzt werden)
enum { StatBit_LeitungFrei		= 4 }; // Leitungsschnittstelle (darf nur bei StatBit_Frei = 0 gesetzt werden)
enum { StatBit_FsBefBetrieb		= 3	}; // Fernschreiber ist eingeschaltet (Polarit‰t)
enum { StatBit_FsBefEin			= 2	}; // Fernschreiber Stromschleife ist aktivert (Ausgabe)
enum { StatBit_FsMeldBetrieb	= 1	}; // Fernschreiber angeschaltet (Stromschleife aktiv oder nur kurz unterbrochen
enum { StatBit_FsMeldEin		= 0 }; // Fernschreiber Stromschleife mit Stromfluﬂ (Einlesung)

typedef volatile enum { Ok, KeineAntwort, Abbruch, Besetzt } TBusErgebnis;
typedef volatile enum { Nichts, Senden, BedSenden, Lesen, Fertig } TBusAuftrag;

extern volatile TBusAuftrag BusAuftrag; 
extern volatile TBusErgebnis BusErgebnis; // Besetzt nur nach BedSenden 
extern volatile uint8_t BusVerbPartner; // als I≤C-Adresse, also * 2
extern uint8_t BusEigenAdresse; // als I≤C-Adresse, also * 2. Bei Mehrfach-Adressen nur Basisadresse
extern uint8_t BusEigenAdrMehrfach; // muss Potenz von 2 sein, Standard = 1
extern volatile uint8_t BusAnrufSubAdresse; // tats‰chlich als Adresse verwendete Nummer 
extern volatile uint8_t BusSendeDaten;
extern volatile uint8_t BusEmpfDaten;
extern volatile bool BusFrei;
extern volatile uint8_t BusKollisionZaehler; // nur zum Testen / Statistik


extern void StatusKdoEmpfReset();

extern void CLR_BIT_Status(uint8_t BitNr);
	
extern void SET_BIT_Status(uint8_t BitNr);

extern void TwiInit();
// vorher muss BusEigenAdresse und BusEigenAdrMehrfach gesetzt sein!

extern void BusWarteFertig();

extern void BusSenden(uint8_t Kdo, uint8_t BesetztWdhWartezeit);
	// BesetztWdhWartezeit = 0: sofort senden 

extern void WarteSchlussQuittung(uint16_t MaxTimer);

extern int16_t GetStatus(uint8_t Adr);

extern bool BusEigenAdressePruefenUndSetzen(uint8_t neu);

extern uint8_t WahlZuAdresse(uint8_t Wahl, uint8_t AnzZiffern);

extern uint8_t AdresseZuWahl(uint8_t Adresse, uint8_t *AnzZiffern);

extern void SendeLebenszeichen();


#endif //ndef __BUSKOMM_H__
