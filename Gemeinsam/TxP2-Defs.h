// TxP2-Übergreifende definitionen
// ===============================

// I²C-Adressen
// ------------

// Meldungsnummer für Verbindungsaufnahme = BusAdr / 2 !

#define BusAdrMin (1*2) // = 0x02
//!< kleinste zulässige TWI-Busadresse.

#define BusAdrEndgeraetMax (100*2) // = 0xC8
//!< größte zulässige TWI-Busadresse für "echte" Endgeräte.

#define BusAdrMax (110*2)  // = 0xDC
//!< größte zulässige TWI-Busadresse.

#define BusAdrFirmwareLoad 238    // = 0xEE
//!< noch nicht verwendet, wird Adresse für optionalen Download.

#define BusAdrUngueltig 255 	  // = 0xFF
//!< Hilfswert für ungültige / nicht vergebene TWI-Busadressen.

//#define MaxLeitungen ((BusAdrLeitungMax - BusAdrLeitungMin) / 2 + 1)


// TWI-Kommandos
// -------------

#define BusKdoVerbAufnahme	0x7F 
	//!< TWI-Kommando für Verbindungsaufbau ist die TWI-Adresse / 2 der Gegenstelle.
	//!< Somit ist BusKdoVerbAufnahme der Maximalwert für dieses Kommando.
#define BusKdoEin			0xA3
	//!< TWI-Kommando zur Einschaltung des Geräts.
#define BusQuittEin			0xA5
	//!< TWI-Kommando zur Quittierung der Einschaltung des Geräts.
#define BusKdoWahlFreigabe 	0xA6
	//!< TWI-Kommando zur Wahlaufforderung.
#define BusKdoWahlziffer0	0xB0
	//!< TWI-Kommando für Wahlziffer 0
#define BusKdoWahlziffer9	0xB9
	//!< TWI-Kommando für Wahlziffer 9
#define BusLebenszeichen	0xA9
	//!< TWI-Kommando für regelmäßige Lebenszeichen bei sonstiger Kommunikationspause.
#define BusKdoSchluss		0xAA
	//!< TWI-Kommando zur Ausschaltung des Geräts.
#define BusQuittSchluss		0xAC
	//!< TWI-Kommando zur Quittierung der Ausschaltung des Geräts.
#define BusKdoSpace			0x93
	//!< TWI-Kommando für den Wechsel von Mark auf Space.
#define BusKdoMark			0x9C
	//!< TWI-Kommando für den Wechsel von Space auf Mark.
#define BusKdoSpaceWdh		0x96
	//!< TWI-Kommando für die Wiederholungsmeldung des Wechsels von Mark auf Space.
#define BusKdoMarkWdh		0x9A
	//!< TWI-Kommando für die Wiederholungsmeldung des Wechsels von Space auf Mark.


// I²C-Parameter
// -------------

#define BusFrequenz 30000
	//!< Standard-TWI-Busfrequenz 30 kHz (100 kHz sollten auch machbar sein).
	
// Hinweise zur Initialisierung
// ----------------------------
// Reset + 0,5 Sek.: I²C initialisieren
// Reset + 0,75 Sek.: I²C nochmal resetten
// Reset + 1 Sek.: früheste Sendung auf I²C 
