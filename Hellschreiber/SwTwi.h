#ifndef __SWTWI_H__

#define __SWTWI_H__

// direkte Eeprom-Zugriffsfunktionen
// ---------------------------------

//! Variablen für Software I²C-Bus
// ------------------------------
//!	nur ein Master, nur Master-Mode

typedef struct
	{
	uint8_t Phase; //!< 0 = untätig, 1-254 = Transfer läuft, 255 = Transfer abgeschlossen
	//!< Wechsel von 255 auf 0 muß durch Applikation erfolgen, vorher aber AnzDaten auf 0 setzen
	uint8_t Adresse; //!< TWI-Adresse des Slave
	uint8_t *Puffer; //!< Zeiger auf bereitgestellten / gefüllten Datenpuffer.
	uint8_t AnzDaten; //!< Soll-Anzahl Daten-Bytes, bei >= 1 geht es los.
	//!< 1 = Adresse + 1 Daten-Byte!
	uint8_t Ergebnis; //!< Ist-Anzahl übertragene Bytes; ideal: AnzDaten + 1 (1 für Adresse)
	// intern:
	uint8_t AktByte; //!< Aktuell zu übertragenes Daten-Byte (ggf. auch Adresse)
	uint8_t BitNr; //!< Aktuell gesendetes / empfangenes Bit.
	uint8_t ByteNr; //!< wandert durch den Puffer
	} T_SwTwiTransferdaten;

// gültige "Zustände":
// Phase	AnzDaten	Beschreibung
//	0			0		nichts zu tun
//  0		   >=1		Transfer kann gestartet werden
// 1-254		x		Transfer läuft
//	255			x		Transfer abgeschlossen

enum { SwTwiRecoverPhase = 60 };
	//!< Element Phase auf diesen Wert setzen um einen hängen gebliebenen Bus möglichst wieder frei zu bekommen.

extern void SwTwiAktion(T_SwTwiTransferdaten *p);

extern void InitSwTwi();

#endif //ndef __SWTWI_H__
