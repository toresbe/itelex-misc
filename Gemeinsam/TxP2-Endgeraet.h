#ifndef __ENDGERAET_H__

#define __ENDGERAET_H__

// Schnittstellen-Funktionen
// ------------------------------

// Ko... = Kommend = Befehl vom Amt oder von der Gegenstelle (in der Regel Abfragefunktionen)
// Ge... = Gehend = Befehl vom angeschlossenen Endgerät

// bei nicht zulässiger Aktion wird Reset durchgeführt!

#include "bool.h"

#include "FifoPuffer.h"

extern TPuffer SendePuffer, EmpfPuffer;

extern void FehlerStop(int Nummer);

extern void KommInit();

extern bool KoEinschalten(); 

extern uint8_t KoAnwahlnummer();
	// im Regelfall 0. Kann bei Mehrfach-Endgerät 0 bis BusEigenAdrMehrfach-1 sein

typedef enum { GeEinschFehler, GeEinschWahl, GeEinschAnrufquitt } TGeEinschResultat;
	   
extern TGeEinschResultat GeEinschalten(); 

extern void GeWaehlen(uint8_t Ziffer);
extern bool KoEmpfMark(); // true bei Mark
extern void GeSendeMark(bool Mark);
extern bool KoEmpfCode(uint8_t *Code); // wenn Zeichen empfangen wurde, wird dieses in Code gespeichert und true zurückgegeben
extern bool GeSendeCode(uint8_t Code); // true, wenn Sendepuffer nicht voll
extern bool KoEmpfZeichen(char *Zeichen); // ASCII-Code
extern bool GeSendeZeichen(char c);
extern bool GeSendePufferVoll();
extern bool GeSendePufferLeer();
extern bool KoAusschalten();
extern void GeAusschalten();

extern void Aktivieren(bool Aktiv);
	// Standard-Zustand ist Aktiv. Umschaltung nur im ausgeschalteten Zustand

extern uint8_t LetzteInterneWahl();

extern bool BetriebsartAusgeschaltet();


#endif //ndef __ENDGERAET_H__
