#include "KonfigDialog.h"

#include <avr/pgmspace.h>
#include <avr/eeprom.h>

#include "TxP2-Defs.h"

#include "BusKomm.h"

#include "LokalAusgabe.h"


//! Bestätigungsmeldung. weil's häufig benutzt wird.
PROGMEM char OkStrP[] = " ok. ";


extern char LokalZeichenLesen();
// Diese Funktion muss in der Anwendung definiert werden.


//! Dialog-Abfrage einer Zahl.
//----------------------------
//! kann nur positive Zahlen.
//! \param[out] z Eingegebene Zahl.
//! \param[in] maxzif Maximale Anzahl Ziffern bei der Eingabe.
//! \retval 0 abbruch
//! \retval 1 unverändert
//! \retval 2 eingabe erfolgt

uint8_t LokalZahlEingabe(uint8_t* z, uint8_t maxzif)
	{
	uint8_t Pos = 0; // 0 = noch keine Ziffer eingegeben, 1 = erste Ziffer, ...

	while (true)
		{
		char Zeichen = LokalZeichenLesen();

		if (Zeichen >= '0' && Zeichen <= '9')
			{
			if (Pos > 0)
				*z = 10 * (*z) + Zeichen - '0';
			else
				*z = Zeichen - '0';
			Pos++;
			if (Pos == maxzif)
				return 2;
			}
		else if (Zeichen == '.' || Zeichen == '-' || Zeichen == '+' || Zeichen == '=' || Zeichen == '/')
			{
			return (Pos > 0) ? 2 : 1;
			}

		else if (Zeichen == '\0') // abschaltung
			{
			return 0;
			}
		else if (Zeichen == '\t') // ignorieren
			{
			}
		else // nicht erkanntes ZeichenZuCode
			{
			if (Pos > 0)
				return 2; // da Zahl mit irgendwas abgeschlossen
			}
		}
	}


//! Dialog-Abfrage für Ja / Nein.
//----------------------------
//! \param[out] b Das Ergebnis Ja oder Nein.
//! \retval 0 abbruch
//! \retval 1 unverändert
//! \retval 2 eingabe erfolgt

uint8_t LokalBoolEingabe(bool* b)
	{
	while (true)
		{
		char Zeichen;
		Zeichen = LokalZeichenLesen(); // macht ggf. wdt_reset();

		switch (Zeichen)
			{
			case 'j':
			case 'J':
			case 'y':
			case 'Y':
			case '1':
			case '+':
				*b = true;
				return 2;
				
			case 'n':
			case 'N':
			case '0':
			case '-':
				*b = false;
				return 2;
				
			case '=':
			case '.':
			case '/':
				return 1;

			case '\0':
				return 0;
			
			}
		}
	}
	


#ifndef FUER_TW39

//! Dialog-Abfrage für einen Text.
//--------------------------------
//! Abschluss nur mit WR oder ZL.
//! WR, ZL oder Leerzeichen am Anfang wird ignoriert. 
//! nur Leerzeichen und WR oder ZL = löschen
//! . (Punkt) oder - oder + oder = oder / als einziges Zeichen vor WR oder ZL = alten Wert behalten.
//! \param[out] s Puffer des eingegebenen Textes.
//! \param[in] maxbuchst Anzahl erlaubter Zeichen bei der Eingabe, auch Puffergröße.
//! \retval 0 abbruch
//! \retval 1 unverändert
//! \retval 2 eingabe erfolgt

uint8_t LokalTextEingabe(char* s, uint8_t maxbuchst)
	{
	uint8_t Pos = 0; 
	char ErstesZeichen = '\0';

	while (true)
		{
		char Zeichen = LokalZeichenLesen();

		switch (Zeichen)
			{
			case '\0': // abschaltung
				if (Pos > 0)
					s[Pos] = '\0';
				return 0;

			case '\t': // Taste ignorieren
				break;

			case '\r':
			case '\n':
				if (Pos == 0)
					if (ErstesZeichen == '\0')
						break; // CR oder LF am Anfang ignorieren
					else
						if (ErstesZeichen == '.' || ErstesZeichen == '-' || ErstesZeichen == '+' 
						    || ErstesZeichen == '=' || ErstesZeichen == '/')
							return 1; // keine Änderung
						else if (ErstesZeichen == ' ')
							{
							s[0] = '\0';
							return 2;
							}
						else
							{
							s[0] = ErstesZeichen;
							s[1] = '\0';
							return 2;
							}
				else // Pos > 0
					{
					s[Pos] = '\0';
					return 2;
					}

			case '#':
				// ungültig
				break;

			case ' ':
				if (Pos == 0)
					{
					ErstesZeichen = ' ';
					break; // hier abbrechen, sonst weiter wie bei Buchstaben...
					}

			default:
				if (Zeichen >= ' ' && Pos < maxbuchst)
					{
					if (Pos == 0)
						if (ErstesZeichen == '\0') 
							// erstes eingegebenes Zeichen
							ErstesZeichen = Zeichen;
						else if (ErstesZeichen == ' ')
							s[Pos++] = Zeichen;
						else
							{
							s[Pos++] = ErstesZeichen;
							s[Pos++] = Zeichen;
							}
					else
						s[Pos++] = Zeichen;
					}
				break;
					
			} // switch Zeichen
		} // while true
	} // LokalTextEingabe

#endif


extern void LokalZeichenAusgabe(char c);
// Diese Funktion muss in der Anwendung definiert werden.


//! Eigene Adresse des Endgeräts, abgelegt im EEPROM.
EEMEM extern uint8_t BusEigenAdresse_EE;


//! Dialog-Abfrage für die allgemeinen Einstellungen eines Endgeräts.
//-------------------------------------------------------------------
//! Bisher nur Abfrage der eigenen Adresse = eigene Durchwahl.
//! Eingegebene Adresse / Durchwahl wird auch im EEPROM gespeichert.
//! \returns Erfolgreiche Eingabe der eigenen Adresse.

bool KonfigurationAllgemein()
	{
	while (true)
		{ // solange Durchwahl abfragen, bis gültige Eingabe erfolgt
		uint8_t ZifferAnz;
		uint8_t Durchwahl = AdresseZuWahl(BusEigenAdresse, &ZifferAnz);
		
		LokalTextAusgabeP(PSTR("\r\n durchwahl aktuell: "));
		LokalZahlAusgabe(Durchwahl, ZifferAnz);
		LokalTextAusgabeP(PSTR(" neu:     "));
		if (LokalZahlEingabe(&Durchwahl, 0) == 0)
			return false;

		Durchwahl &= ~(BusEigenAdrMehrfach - 1);
			// erreicht, dass bei (Bsp.) 8 Adressen die Basisadresse 8, 16, 24, ...
			// ist

		LokalTextAusgabeP(PSTR("\r\n pruefe "));
		LokalZahlAusgabe(Durchwahl, 2);
		LokalZeichenAusgabe(' ');	

		if (BusEigenAdressePruefenUndSetzen(WahlZuAdresse(Durchwahl, 2)))
			{
			LokalTextAusgabeP(OkStrP);

			if (BusEigenAdresse != eeprom_read_byte(&BusEigenAdresse_EE))
				eeprom_write_byte(&BusEigenAdresse_EE, BusEigenAdresse);

			return true;
			}

		LokalTextAusgabeP(PSTR(" schon vergeben oder ungueltig, andere waehlen!"));
		}
	}


