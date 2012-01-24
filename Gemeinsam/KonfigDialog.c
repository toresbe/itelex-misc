#include "KonfigDialog.h"

#include <avr/pgmspace.h>
#include <avr/eeprom.h>

#include "TxP2-Defs.h"

#include "BusKomm.h"

#include "LokalAusgabe.h"


extern char LokalZeichenLesen();


uint8_t LokalZahlEingabe(uint8_t* z, uint8_t maxzif)
	// return: 0 = abbruch, 1 = unverändert, 2 = eingabe erfolgt
	// kann nur positive Zahlen
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


uint8_t LokalBoolEingabe(bool* b)
	// return: 0 = abbruch, 1 = unverändert, 2 = eingabe erfolgt
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
	
		
uint8_t LokalTextEingabe(char* s, uint8_t maxbuchst)
	// return: 0 = abbruch, 1 = unverändert, 2 = eingabe erfolgt
	// Abschluss nur mit CR oder LF, nur CR oder LF = alter wert, nur Leer = löschen
	// Leer am Anfang wird ignoriert
	{
	uint8_t Pos = 0; 
	char ErstesZeichen = '\0';

	// TODO: CR am Anfang kann durch Zeilenende bedingt sein.

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
				else
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


void LokalZeichenAusgabe(char c);


extern uint8_t BusEigenAdresse_EE EEMEM;


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
			LokalTextAusgabeP(PSTR("ok. "));

			if (BusEigenAdresse != eeprom_read_byte(&BusEigenAdresse_EE))
				eeprom_write_byte(&BusEigenAdresse_EE, BusEigenAdresse);

			return true;
			}

		LokalTextAusgabeP(PSTR(" schon vergeben oder ungueltig, andere waehlen!"));
		}
	}


