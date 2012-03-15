#ifndef __PORTS_H__

#define __PORTS_H__

#include "Defports.h"

#ifndef PLATINE_VERSION
#error Platinen-Version in Konfiguration festlegen!
#endif

// Schnittstellen
// --------------

#if PLATINE_VERSION >= 13 // 1.3x
// aktuelle Version

DEFPORTINPULL	(TASTE,		D, 0)	

DEFPORTOUT		(LEDROT, 	D, 1)
DEFPORTOUT		(LEDGELB, 	D, 2)
DEFPORTOUT		(LEDGRUEN, 	D, 3)
DEFPORTOUT		(LEDBLAU, 	D, 4)

DEFPORTOUT		(FS_AUSG,	B, 0) // Schleifenschluﬂ-Ausgabe
DEFPORTOUT		(FS_AKTIV,	D, 7) // Einschaltung (Ausgabe, Polarit‰t der Schleife)
DEFPORTINPULL	(FS_EING, 	D, 6) // Strom-Einlesung

#ifdef PARALLELAUSGABE

DEFPORTOUT		(FS2_AUSG,	B, 2) // Schleifenschluﬂ-Ausgabe
DEFPORTOUT		(FS2_AKTIV, B, 1) // Einschaltung (Ausgabe, Polarit‰t der Schleife)
DEFPORTINPULL	(FS2_EING,	D, 5) // Strom-Einlesung

#endif //def PARALLELAUSGABE

#elif PLATINE_VERSION >= 10

#warning Nur fuer alte Platinen-Version 1.0 bis 1.2

DEFPORTINPULL	(TASTE,		D, 6)	

DEFPORTOUT		(LEDROT, 	D, 7)
DEFPORTOUT		(LEDGELB, 	B, 0)
DEFPORTOUT		(LEDGRUEN, 	B, 1)
DEFPORTOUT		(LEDBLAU, 	B, 2)

DEFPORTOUT		(FS_AUSG,	D, 0) // Schleifenschluﬂ-Ausgabe
DEFPORTOUT		(FS_AKTIV,	D, 1) // Einschaltung (Ausgabe, Polarit‰t der Schleife)
DEFPORTINPULL	(FS_EING, 	C, 3) // Strom-Einlesung

#else

#error Platinenversion PLATINE_VERSION nicht bekannt...

#endif // PLATINE_VERSION 


#endif //ndef __PORTS_H__
