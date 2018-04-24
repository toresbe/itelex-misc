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
enum { TtyCodeZiSchraegstrich = 23 } ; //!< Baudot-Code für einen Schrägstrich / .
enum { TtyCodeZiIstgleich = 15 } ; //!< Baudot-Code für einen Gleichheitszeichen = .
enum { TtyCodeLeer = 4 } ; //!< Baudot-Code für Leerzeichen.
enum { TtyCodeZiWerDa = 18 /* bei Ziffern! */ } ; //!< Baudot-Code für Kennungsgeber-Abfrage.

#ifdef USTTY
enum { TtyCodeZiKlingel = 20 /* bei Ziffern! */} ; //!< Baudot-Code für Klingelzeichen.
#else
enum { TtyCodeZiKlingel = 26 /* bei Ziffern! */} ; //!< Baudot-Code für Klingelzeichen.
#endif


#define CTRL(z) ((z) & 0b00011111)

enum { CodeChrBuUm = '\016' } ; //!< Hilfs-ASCII-Code für Buchstaben-Umschaltung. SO = shift out = Ctrl-N
enum { CodeChrZiUm = '\017' } ; //!< Hilfs-ASCII-Code für Ziffern-Umschaltung. SI = shift in = Ctrl-O
			//	 	   ^^^ oktal!
enum { CodeChrKlingel = CTRL('g') } ; //!< Hilfs-ASCII-Code für Klingelzeichen.
enum { CodeChrWerDa = CTRL('w') } ; //!< Hilfs-ASCII-Code für Kennungsgeber-Abfrage.

#define ESC 27

typedef uint8_t TBaudotMode; //!< Speichert Buchstaben-Ziffern-Umschaltung und letzte Konvertierungs-Richtung

enum { BaudotMode_ZiffernBitMaske = 0x01 }; //!< Dieses Bit ist gesetzt, wenn Ziffern-Ebene aktiv ist.
enum { BaudotMode_SendenBitMaske = 0x02 }; //!< Dieses Bit ist gesetzt, wenn die letzte Umwandlung von ASCII nach Baudot war.
									//!< Sofern dieses Bit gesetzt ist, findet keine erneute Sendung von BuUm oder ZiUm statt.

enum { BaudotMode_ZiffernGesendet = BaudotMode_ZiffernBitMaske | BaudotMode_SendenBitMaske };
enum { BaudotMode_BuchstabenGesendet = BaudotMode_SendenBitMaske };
enum { BaudotMode_ZiffernEmpfangen = BaudotMode_ZiffernBitMaske };
enum { BaudotMode_BuchstabenEmpfangen = 0 };


#define BaudotMode_IstZiffern(m) (((m) & BaudotMode_ZiffernBitMaske) != 0)
#define BaudotMode_SetZiffern(m) ((m) |= BaudotMode_ZiffernBitMaske)
#define BaudotMode_IstBuchstaben(m) (((m) & BaudotMode_ZiffernBitMaske) == 0)
#define BaudotMode_SetBuchstaben(m) ((m) &= ~BaudotMode_ZiffernBitMaske)

#define BaudotMode_IstSenden(m) (((m) & BaudotMode_SendenBitMaske) != 0)
#define BaudotMode_SetSenden(m) ((m) |= BaudotMode_SendenBitMaske)
#define BaudotMode_IstEmpfangen(m) (((m) & BaudotMode_SendenBitMaske) == 0)
#define BaudotMode_SetEmpfangen(m) ((m) &= ~BaudotMode_SendenBitMaske)


									
extern uint8_t ZeichenZuCode(char c, TBaudotMode Mode);

extern bool ZeichenZuCode2(char c, TBaudotMode* Mode, uint8_t* Code1, uint8_t* Code2);
// true bei erfolgreicher Umsetzung

extern char CodeZuZeichen(uint8_t code, TBaudotMode *Mode);


#endif //ndef __BAUDOTCODE_H__
