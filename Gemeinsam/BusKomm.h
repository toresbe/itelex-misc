#ifndef __BUSKOMM_H__

#define __BUSKOMM_H__

#include <inttypes.h>
#include <avr/interrupt.h>
#include <stdbool.h>

#include "bits.h"


// Varianten
// ---------

#ifdef FUER_TW39

#define BUSKOMM_SPARVERSION
	// dadurch wird Rundsenden und Empfang von Rundsendungen nicht implementiert
	
#endif //def FUER_TW39

// Status-Variablen
// ----------------

extern volatile uint8_t Status;
//!< Betriebszustand des eigenen Moduls. Bitcodiert, siehe folgende Definitionen StatBit_***.

//!< Manches Bit doppelt belegt, abhängig von Bit 7.

enum { StatBit_Frei				= 7	}; //!< 0x80, keine Verbindung besteht (weder innen noch außen)
enum { StatBit_BusKdoEmpfangen	= 6	}; //!< 0x40, Empfangenes Kommando noch nicht abschließend bearbeitet
// folgende Bits gelten nur bei Frei = JA:
enum { StatBit_LeitungKennung	= 5 }; //!< 0xA0, Leitungsschnittstelle (nur gültig bei StatBit_Frei = 1)
enum { StatBit_SpezialGeraetKennung	= 4 }; //!< 0x90, Spezialgerät, nicht für Dialog mit Benutzer geeignet (nur gültig bei StatBit_Frei = 1)
// folgende Bits gelten nur bei Frei = NEIN:
enum { StatBit_Verbunden		= 5	}; //!< 0x20, Verbindung ist komplett hergestellt 
enum { StatBit_AngerufenBelegt	= 4	}; //!< 0x10, Fernschreiber wurde von anderem Partner (intern oder extern) aktiviert
enum { StatBit_FsBefBetrieb		= 3	}; //!< 0x08, Fernschreiber ist eingeschaltet (Polarität)
enum { StatBit_FsBefEin			= 2	}; //!< 0x04, Fernschreiber Stromschleife ist aktivert (Ausgabe)
enum { StatBit_FsMeldBetrieb	= 1	}; //!< 0x02, Fernschreiber angeschaltet (Stromschleife aktiv oder nur kurz unterbrochen
enum { StatBit_FsMeldEin		= 0 }; //!< 0x01, Fernschreiber Stromschleife mit Stromfluß (Einlesung)

typedef volatile enum { Ok, KeineAntwort, Abbruch, Besetzt } TBusErgebnis; //!< Ergebnis des letzten abgeschlossenen Buszugriffs.
typedef volatile enum { Nichts, Senden, BedSenden, Lesen, Rundsenden, Fertig } TBusAuftrag; //!< Art des gewünschten Buszugriffs.
	// Rundsendung ist eine Sendung von Daten an alle anderen.

extern volatile TBusAuftrag BusAuftrag; //!< Aktuell anstehende Bus-Aktivität des eigenen Moduls.
extern volatile TBusErgebnis BusErgebnis; //!< Ergebnis des letzten BusSenden Aufrufs. Besetzt nur nach BedSenden. 
extern volatile uint8_t BusVerbPartner; //!< Aktueller Verbindungspartner als I²C-Adresse, also * 2
extern uint8_t BusEigenAdresse; //!< Aktuelle eigene Adresse als I²C-Adresse, also * 2. Bei Mehrfach-Adressen nur Basisadresse.

extern volatile uint8_t BusAnrufSubAdresse; //!< Aktuelle eigene Adresse der aktuellen Verbindung.
extern volatile uint8_t BusSendeDaten; //!< zu sendendes Byte.
extern volatile bool BusFrei; //!< True, wenn Modul bereit für neue Bus-Aktivität.
extern volatile uint8_t BusKollisionZaehler; //!< Zählt die Anzahl der verlorenen TWI-Arbitrierungen. Nur zum Testen / Statistik.
extern volatile bool BusEmpfMark; //!< Aktueller "Empfangspegel". Wird durch Empfang von BusKdoMark gesetzt bzw. BusKdoSpace gelöscht.

extern volatile bool BusEmpfMarkwechsel; //!< \brief Flag für Empfangspegel-Wechsel. 
	//!< \details Wird durch Empfang von BusKdoMark oder BusKdoSpace gesetzt.
	//!< Muss durch das Anwendungsprogramm gelöscht werden.
	
extern uint8_t BusEigenAdrMehrfach; 
	//!< Anzahl der Bus-Adressen des eigenen Moduls. Muss Potenz von 2 sein (also 1, 2, 4, 8, 16, ...) , Standard = 1

#ifndef BUSKOMM_SPARVERSION

extern bool RundsendEmpfFreig; //!< Freigabe des Empfangs von Rundsendungen 

enum { RundsendMaxDaten = 16 } ; //!< Maximale Anzahl Byte für eine Rundsendung

extern volatile uint8_t RundsendDaten[RundsendMaxDaten]; //!< zu sendende ODER empfangene Daten

extern volatile uint8_t RundsendAnzDaten; //!< tatsächliche Anzahl an zu sendende ODER empfangene Daten.
	//!< wird bei Beginn eine Empfangs auf 0 gesetzt
	
#endif //ndef BUSKOMM_SPARVERSION


extern volatile uint16_t TwiIsrCount;
	//!< Zählt die Anzahl der Aufrufe der TWI-Interrupt-Routine. Nur für Debugging-Zwecke.

extern volatile uint16_t TwiWatchdogCount;
	//!< Wird bei jedem Aufruf der TWI-Interrupt-Routine auf 0 gesetzt. Zur Prüfung der regelmäßigen Kommunikation.
	
	
extern void CLR_BIT_Status(uint8_t BitNr);
	
extern void SET_BIT_Status(uint8_t BitNr);

extern void TwiInit(void);

extern void BusWarteFertig(void);

extern void BusSenden(uint8_t Kdo);

#ifndef BUSKOMM_SPARVERSION

extern void BusRundsenden(void);

#endif //ndef BUSKOMM_SPARVERSION

extern void WarteSchlussQuittung(uint16_t MaxTimer);

extern int16_t GetStatus(uint8_t Adr);

extern bool BusEigenAdressePruefenUndSetzen(uint8_t neu);

extern uint8_t WahlZuAdresse(uint8_t Wahl, uint8_t AnzZiffern);

extern uint8_t AdresseZuWahl(uint8_t Adresse, uint8_t *AnzZiffern);

extern void SendeLebenszeichen(void);

extern bool GetEmpfByte(uint8_t *Code);
	
extern bool EmpfPufferLeer(void);


#endif //ndef __BUSKOMM_H__
