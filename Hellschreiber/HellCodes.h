/*
 * HellCodes.h
 *
 * Created: 05.03.2021 12:33:19
 *  Author: sonne-fr
 */ 

// Kommunikation zwischen Kommunikationsprozessor und Signalprozessor läuft über TWI.
// Wenn das Makro TESTSER gesetzt ist zu Debugging-Zwecken über Seriell (UART).

// Im TWI-Modus:
// Der Kommunikationsprozessor ist immer der Master, der Signalprozessor der Slave.
// Zu druckende Zeichen werden als ASCII-Code gesendet. Belegt ist nur der Bereich von 0x0A bis 0x7F
// Zum Starten und Stoppen des Hellschreibers werden die Bytes #HellEinschaltBefehl bzw. #HellAusschaltBefehl
// gesendet.
// Wenn es nichts zu senden gibt, dann wird ein Byte vom Signalprozessor gelesen.
// Wenn Bit 7 vom gelesenen Byte *nicht* gesetzt ist, dann handelt es sich um ein auf dem Hellschreiber 
// eingetastetes Zeichen (ASCII-Code). Die Dauersignal-Taste wird durch die Codes 
// HELLC_BREAK1 bis HELLC_BREAK6 gemeldet.
// Wenn Bit 7 vom gelesenen Byte *gesetzt* ist, dann wird der allgemeine Betriebszustand als Bitmaske gesendet.
// siehe Definitionen HellStatus...

#ifndef HELLCODES_H_
#define HELLCODES_H_

#define HellTwiAdresse 111

#define HellStatBitSendeTon 0
#define HellStatBitEmpfTon 1
#define HellStatBitEmpfStoer 2
#define HellStatBitLaeuft 3 // schreib- und druckbereit
#define HellStatBitSendeAnruf 4
#define HellStatBitPufferVoll 5
#define HellStatBitStatFlag 7 // wird immer gesetzt, wenn es eine Statusmeldung ist


#define HellEinschaltBefehl '\002' // schaltet den Hellschreiber ein, ohne dass ein Zeichen gedruckt wird.
#define HellAusschaltBefehl '\004' // schaltet den Hellschreiber aus und löscht den Empfangspuffer
// geeignet sind alle Codes außerhalb des Bereichs HELL_FONT_TAB_START bis HELL_FONT_TAB_END


#define HELL_FONT_CHAR_SIZE 6 // 6 Einträge in der Tabelle je Zeichen
#define HELL_FONT_TAB_START 10 // Linefeed
#define HELL_FONT_TAB_END 127


const PROGMEM uint16_t HellFontTab[] = {
	#include "HellFont.h"
0 };

// HellFont.h enthält auch die Definition der Sonderzeichen. Die ggf. nicht genutzte
// Tabelle der Bitmuster wird wegoptimiert.

#endif /* HELLCODES_H_ */