#ifndef __BAUDOTCODE_H__

#define __BAUDOTCODE_H__

#include <bool.h>

enum { TtyCodeWR = 2 } ;
enum { TtyCodeZL = 8 } ;
enum { TtyCodeBuUm = 31 } ;
enum { TtyCodeZiUm = 27 } ;
enum { TtyCodeSPC = 0 } ;
enum { TtyCodeLeer = 4 } ;
enum { TtyCodeZiWerDa = 18 /* bei Ziffern! */ } ;
enum { TtyCodeZiKlingel = 26 /* bei Ziffern! */} ;

#define CTRL(z) ((z) & 0b00011111)

enum { CodeChrBuUm = CTRL('b') } ;   
enum { CodeChrZiUm = CTRL('z') } ;   
enum { CodeChrKlingel = CTRL('k') } ;
enum { CodeChrWerDa = CTRL('w') } ;

#define ESC 27

extern uint8_t ZeichenZuCode(char c, bool Ziffer);
// 255 bei nicht passendem c (nicht in Code-Tabelle oder falscher Modus Bu/Zi

extern bool ZeichenZuCode2(char c, bool* Ziffer, uint8_t* Code1, uint8_t* Code2);
// true bei erfolgreicher Umsetzung

extern char CodeZuZeichen(uint8_t code, bool *Ziffer);
// '\0' bei Steuerzeichen


#endif //ndef __BAUDOTCODE_H__
