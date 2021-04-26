#ifndef __BAUDOTCODE_H__

#define __BAUDOTCODE_H__

#include <stdbool.h>
#include <inttypes.h>


enum { TtyCodeWR = 2 } ; //!< Baudot-Code für Wagenrücklauf.
enum { TtyCodeZL = 8 } ; //!< Baudot-Code für Zeilenvorschub.
enum { TtyCodeBuUm = 31 } ; //!< Baudot-Code für Buchstaben-Umschaltung.
enum { TtyCodeZiUm = 27 } ; //!< Baudot-Code für Ziffern-Umschaltung.
enum { TtyCode3EUm =  0 } ; //!< Baudot-Code für Umschaltung auf 3. Ebene
enum { TtyCodeSPC = 0 } ; //!< Baudot-Code für NUL-Zeichen (Code 32).
enum { TtyCodeZiPunkt = 7 } ; //!< Baudot-Code für einen Punkt.
enum { TtyCodeZiDoppelpunkt = 14 } ; //!< Baudot-Code für einen Doppelpunkt.
enum { TtyCodeZiSchraegstrich = 23 } ; //!< Baudot-Code für einen Schrägstrich / .
enum { TtyCodeZiIstgleich = 15 } ; //!< Baudot-Code für einen Gleichheitszeichen = .
enum { TtyCodeLeer = 4 } ; //!< Baudot-Code für Leerzeichen.
enum { TtyCodeZiWerDa = 18 /* bei Ziffern! */ } ; //!< Baudot-Code für Kennungsgeber-Abfrage.

#if defined(USTTY) || defined(TTYCODE_US)
enum { TtyCodeZiKlingel = 20 /* bei Ziffern! */} ; //!< Baudot-Code für Klingelzeichen.
#else
enum { TtyCodeZiKlingel = 26 /* bei Ziffern! */} ; //!< Baudot-Code für Klingelzeichen.
#endif


#define CTRL(z) ((z) & 0b00011111)

enum { CodeChrBuUm = '\016' } ; //!< Hilfs-ASCII-Code für Buchstaben-Umschaltung. SO = shift out = Ctrl-N
enum { CodeChrZiUm = '\017' } ; //!< Hilfs-ASCII-Code für Ziffern-Umschaltung. SI = shift in = Ctrl-O
enum { CodeChr3EUm = '\020' } ; //!< Hilfs-ASCII-Code für Dritte-Ebene-Umschaltung. DLE = Ctrl-P
			//	 	   ^^^ oktal!

enum { CodeChrKlingel = CTRL('g') } ; //!< Hilfs-ASCII-Code für Klingelzeichen.
enum { CodeChrWerDa = CTRL('w') } ; //!< Hilfs-ASCII-Code für Kennungsgeber-Abfrage.

#define ESC 27

typedef uint8_t TBaudotMode; //!< Speichert Buchstaben-Ziffern-Umschaltung und letzte Konvertierungs-Richtung

enum { BaudotMode_SendenBitMaske = 0x01 }; //!< Dieses Bit ist gesetzt, wenn die letzte Umwandlung von ASCII nach Baudot war.
									//!< Sofern dieses Bit gesetzt ist, findet keine erneute Sendung von BuUm oder ZiUm statt.
enum { BaudotMode_ZiffernBitMaske = 0x02 }; //!< Dieses Bit ist gesetzt, wenn Ziffern-Ebene aktiv ist.
enum { BaudotMode_DritteEbBitMaske = 0x04 }; //!< Dieses Bit ist gesetzt, wenn Ziffern-Ebene aktiv ist.
enum { BaudotMode_EbeneBitMaske   = BaudotMode_ZiffernBitMaske | BaudotMode_DritteEbBitMaske };

enum { BaudotMode_ZiffernGesendet = BaudotMode_ZiffernBitMaske | BaudotMode_SendenBitMaske };
enum { BaudotMode_DritteEbGesendet = BaudotMode_DritteEbBitMaske | BaudotMode_SendenBitMaske };
enum { BaudotMode_BuchstabenGesendet = BaudotMode_SendenBitMaske };
enum { BaudotMode_ZiffernEmpfangen = BaudotMode_ZiffernBitMaske };
enum { BaudotMode_DritteEbEmpfangen = BaudotMode_DritteEbBitMaske };
enum { BaudotMode_BuchstabenEmpfangen = 0 };


#define BaudotMode_IstZiffern(m)    (((m) & BaudotMode_EbeneBitMaske) == BaudotMode_ZiffernBitMaske)
#define BaudotMode_IstDritteEb(m)   (((m) & BaudotMode_EbeneBitMaske) == BaudotMode_DritteEbBitMaske)
#define BaudotMode_IstBuchstaben(m) (((m) & BaudotMode_EbeneBitMaske) == 0)

#define BaudotMode_SetZiffern(m)    ((m) |= BaudotMode_ZiffernBitMaske, (m) &= ~BaudotMode_DritteEbBitMaske)
#define BaudotMode_SetDritteEb(m)   ((m) |= BaudotMode_DritteEbBitMaske, (m) &= ~BaudotMode_ZiffernBitMaske)
#define BaudotMode_SetBuchstaben(m) ((m) &= ~BaudotMode_EbeneBitMaske)

#define BaudotMode_IstSenden(m) 	(((m) & BaudotMode_SendenBitMaske) != 0)
#define BaudotMode_IstEmpfangen(m) 	(((m) & BaudotMode_SendenBitMaske) == 0)

#define BaudotMode_SetSenden(m) 	((m) |= BaudotMode_SendenBitMaske)
#define BaudotMode_SetEmpfangen(m) 	((m) &= ~BaudotMode_SendenBitMaske)


extern uint8_t ZeichenZuCode(char c, TBaudotMode Mode);

extern bool ZeichenZuCode2(char c, TBaudotMode* Mode, uint8_t* Code1, uint8_t* Code2);
// true bei erfolgreicher Umsetzung

extern char CodeZuZeichen(uint8_t code, TBaudotMode *Mode);

#ifdef AF_TTYCODE_SWITCHABLE

extern void CodeTabWechsel(uint8_t Mode);

#define MAX_CODETAB_IDX 6

#endif //def AF_TTYCODE_SWITCHABLE


#endif //ndef __BAUDOTCODE_H__
