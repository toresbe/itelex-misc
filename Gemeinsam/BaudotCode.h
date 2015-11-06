#ifndef __BAUDOTCODE_H__

#define __BAUDOTCODE_H__

#include <stdbool.h>
#include <inttypes.h>

// wenn statt Zeichensatz ITA2 der USTTY gelten soll, ist USTTY vorzudefinieren


enum { TtyCodeWR = 2 } ; //!< Baudot-Code für Wagenrücklauf.
enum { TtyCodeZL = 8 } ; //!< Baudot-Code für Zeilenvorschub.
enum { TtyCodeBuUm = 31 } ; //!< Baudot-Code für Buchstaben-Umschaltung.
enum { TtyCodeZiUm = 27 } ; //!< Baudot-Code für Ziffern-Umschaltung.
enum { TtyCodeSPC = 0 } ; //!< Baudot-Code für NUL-Zeichen (Code 32).
enum { TtyCodeZiPunkt = 7 } ; //!< Baudot-Code für einen Punkt.
enum { TtyCodeZiDoppelpunkt = 14 } ; //!< Baudot-Code für einen Doppelpunkt.
enum { TtyCodeLeer = 4 } ; //!< Baudot-Code für Leerzeichen.
enum { TtyCodeZiWerDa = 18 /* bei Ziffern! */ } ; //!< Baudot-Code für Kennungsgeber-Abfrage.

#ifdef USTTY
enum { TtyCodeZiKlingel = 20 /* bei Ziffern! */} ; //!< Baudot-Code für Klingelzeichen.
#else
enum { TtyCodeZiKlingel = 26 /* bei Ziffern! */} ; //!< Baudot-Code für Klingelzeichen.
#endif


#define CTRL(z) ((z) & 0b00011111)

enum { CodeChrBuUm = CTRL('b') } ; //!< Hilfs-ASCII-Code für Buchstaben-Umschaltung.
enum { CodeChrZiUm = CTRL('z') } ; //!< Hilfs-ASCII-Code für Ziffern-Umschaltung.
enum { CodeChrKlingel = CTRL('k') } ; //!< Hilfs-ASCII-Code für Klingelzeichen.
enum { CodeChrWerDa = CTRL('w') } ; //!< Hilfs-ASCII-Code für Kennungsgeber-Abfrage.

#define ESC 27

enum { BuMode = 'a', ZiMode = '1' } ; //!< Werte für BuZiMode / Mode.

extern uint8_t ZeichenZuCode(char c, char Mode);

extern bool ZeichenZuCode2(char c, char* Mode, uint8_t* Code1, uint8_t* Code2);
// true bei erfolgreicher Umsetzung

extern char CodeZuZeichen(uint8_t code, char *Mode);


#endif //ndef __BAUDOTCODE_H__
