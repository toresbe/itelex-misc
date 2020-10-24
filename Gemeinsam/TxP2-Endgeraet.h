#ifndef __ENDGERAET_H__

#define __ENDGERAET_H__

// Schnittstellen-Funktionen
// ------------------------------

// Ko... = Kommend = Befehl vom Amt oder von der Gegenstelle (in der Regel Abfragefunktionen)
// Ge... = Gehend = Befehl vom angeschlossenen Endgerät

// bei nicht zulässiger Aktion wird Reset durchgeführt!

#include <stdbool.h>

#include "FifoPuffer.h"

#include "BaudotCode.h"


#ifdef TESTFUNKTIONEN

extern uint8_t TestFunktion; 

// Definierte Testfunktionen:
enum { TestfnNormal 					=  0 } ; //!< Schnittstelle arbeitet normal
enum { TestfnLebenszeichenAbstand		=  1 } ; //!< Abstand der Lebenszeichen wird durch #TestVerzoegerung bestimmt.
enum { TestfnGeEinVerzoegerung			=  2 } ; //!< TODO Künstliche Pause zwischen Reservierung der Gegenstelle und EinschaltKommando an die Gegenstelle
enum { TestfnEinschaltAblehnung 		=  3 } ; //!< EinschaltKommando wird nach Verzögerung 'abgelehnt' mit BusKdoSchluss 
enum { TestfnEinschaltVerzoegerung 		=  4 } ; //!< EinschaltKommando wird erst nach Verzögerung quittiert
enum { TestfnAusschaltOhneQuitt 		=  5 } ; //!< AusschaltKommando wird nicht quittiert
enum { TestfnAusschaltQuittVerzoegerung	=  6 } ; //!< AusschaltKommando wird erst nach Verzögerung quittiert
enum { TestfnAusschaltQuittStattKdo		=  7 } ; //!< statt AusschaltKommando wird AusschaltQuittung gesendet
enum { TestfnMarkNichtWdh 				=  8 } ; //!< Wiederholungs-Meldung für Wechsel nach Mark wird nicht gesendet
enum { TestfnSpaceNichtWdh 				=  9 } ; //!< Wiederholungs-Meldung für Wechsel nach Space wird nicht gesendet
enum { TestfnMarkNurWdh 				= 10 } ; //!< Wechsel nach Mark wird nur als Wiederholung gemeldet (Simulation Verlust von BusKdoMark)
enum { TestfnSpaceNurWdh 				= 11 } ; //!< Wechsel nach Space wird nur als Wiederholung gemeldet (Simulation Verlust von BusKdoMark)

extern uint8_t TestVerzoegerung;

#endif //def TESTFUNKTIONEN


extern TPuffer SendePuffer, EmpfPuffer;

extern TBaudotMode BaudotMode;

//! Für Seriellumsetzung: Welche Seite der Verbindung wird ausgewertet bzw. beeinflusst.
typedef enum { UmsetzLokal, UmsetzFern, UmsetzLokalUndFern } TUmsetzModus;

extern TUmsetzModus SendeUmsetzModus, EmpfUmsetzModus;

extern bool UmleitungAbweisen;

// Funktionen
// ==========

extern void FehlerStop(int Nummer);

extern void KommInit();

extern bool KoEinschalten(); 
extern bool GeEinschaltQuittung();

extern uint8_t KoAnwahlnummer();
	// im Regelfall 0. Kann bei Mehrfach-Endgerät 0 bis BusEigenAdrMehrfach-1 sein

// extern TGeEinschResultat GeEinschalten(); -> ersetzt durch GeAnrufBeginn() und GeEinschaltQuittung()

extern bool GeAnrufBeginn();
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
extern void GeAusschalten(bool WarteQuitt);

extern void Aktivieren(bool Aktiv);
	// Standard-Zustand ist Aktiv. Umschaltung nur im ausgeschalteten Zustand

extern uint8_t LetzteInterneWahl();


#endif //ndef __ENDGERAET_H__
