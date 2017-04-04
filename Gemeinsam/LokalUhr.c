#include "LokalUhr.h"

#include "BaudotCode.h"

uint8_t Jahr; //!< aktuelles Jahr - 2000
uint8_t Monat; //!< aktueller Monat
uint8_t Tag; //!< aktueller Tag. Auch Kennzeichen für überhaupt gesetztes Datum (wenn >0)
uint8_t Stunde; //!< aktuelle Stunde
uint8_t Minute; //!< aktuelle Minute
uint8_t Wochentag; //!< Aktueller Wochentag.

//! Initialisiert alles
//---------------------------------------------------------------------

void LokalUhrInit()
	{
	Jahr = 0;
	Monat = 0;
	Tag = 0;
	Stunde = 0;
	Minute = 0;
	Wochentag = 0;
	}
	

//! Prüft empfangene Rundsendedaten, ob diese ein Uhrzeit-Stellkommando enthalten
//---------------------------------------------------------------------
	
bool LokalUhrPruefeRundsendung(volatile uint8_t *buf, uint8_t anz)
	{
	if (anz >= 8 
		&& buf[0] == 'c'
		&& buf[1] == 'l'
		&& buf[2] == 'k')
		{
		Jahr = buf[3];
		Monat = buf[4];
		Tag = buf[5];
		Stunde = buf[6];
		Minute = buf[7];
		if (anz >= 9)
			Wochentag = buf[8];
		return true;
		}
	else
		return false;
	}
	

//! Gibt einen dezimale Zahl zweistellig in den Baudot-Code-Puffer
//----------------------------------------------------------------
//! \param p Zeiger auf den Puffer mit den Baudot-Daten
//! \param i die auszugebende Zahl.
//! \param minzif Mindestzahl an Ziffern (kann führende Nullen bewirken).

static void BaudotZahlAusgabeZweistellig(uint8_t *p, uint8_t i)
	{
	uint8_t z = 0; // Zehner

	while (i >= 10)
		z++, i -= 10;

	p[0] = ZeichenZuCode(z + '0', ZiMode);
	p[1] = ZeichenZuCode(i + '0', ZiMode);
	}
	
	
//! Füllt einen Puffer mit Baudot-Daten für die gespeicherte Uhrzeit
//! \param buf Zeiger auf einen Puffer für die Baudot-Daten. Muss mindestens 17 Zeichen umfassen
//! \return Anzahl der Zeichen, die in den Puffer geschrieben worden sind.

uint8_t LokalUhrBaudotAusgabe(uint8_t *buf)
	{
	if (Tag == 0) 
		return 0;
	buf[0] = TtyCodeZiUm;
	BaudotZahlAusgabeZweistellig(buf + 1, Tag); 
	buf[3] = TtyCodeZiPunkt;
	BaudotZahlAusgabeZweistellig(buf + 4, Monat); 
	buf[6] = TtyCodeZiPunkt;
	BaudotZahlAusgabeZweistellig(buf + 7, Jahr); 
	buf[9] = TtyCodeLeer;
	BaudotZahlAusgabeZweistellig(buf + 10, Stunde); 
	buf[12] = TtyCodeZiDoppelpunkt;
	BaudotZahlAusgabeZweistellig(buf + 13, Minute); 
	return 15;
	}
	
	
