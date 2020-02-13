// Speichert Konfigurationswerte im EEPROM, und zwar redundant.
// ------------------------------------------------------------
// Fred Sonnenrein

#include "KonfigSpeicher.h"


#include <avr/eeprom.h>


uint8_t FehlerCode;
uint8_t FehlerAdresse;


enum { BankOffset = 80 }; // Offset der redundanten Speicherbereiche.


static void FehlerSpeichern(uint8_t code, uint8_t adresse)
	{
	if (code > FehlerCode)
		{
		FehlerCode = code;
		FehlerAdresse = adresse;
		}
	}
	
	
void KonfigSpeicherInit()
	{
	FehlerCode = KonfigSpeicherOK;
	FehlerAdresse = 0xFF;
	}
	

uint8_t KonfigLeseByte(uint8_t Adresse, uint8_t Default)
	{
	uint8_t a, b, c;
	
	a = eeprom_read_byte(Adresse);
	b = eeprom_read_byte(Adresse + BankOffset);
	c = eeprom_read_byte(Adresse + 2 * BankOffset);

	if (a == b && b == c)
		return a;
	
	if (b == 0xFF && c == 0xFF)
		{ // nicht initialisierte redundanz --> speichern
		KonfigSchreibeByte(Adresse, a);
		return a;
		}
		
	if (a == b)
		{ // c war offensichtlich betroffen
		FehlerSpeichern(KonfigSpeicherLesefehler, Adresse + 2 * BankOffset);
		KonfigSchreibeByte(Adresse, a);
		return a;
		}

	if (a == c)
		{ // b war offensichtlich betroffen
		FehlerSpeichern(KonfigSpeicherLesefehler, Adresse + BankOffset);
		KonfigSchreibeByte(Adresse, a);
		return a;
		}

	if (b == c)
		{ // a war offensichtlich betroffen
		FehlerSpeichern(KonfigSpeicherLesefehler, BankOffset);
		KonfigSchreibeByte(Adresse, b);
		return b;
		}
		
	FehlerSpeichern(KonfigSpeicherLesefehler, Adresse);
	KonfigSchreibeByte(Adresse, Default);
	return Default;
	} // KonfigLeseByte()

	
uint8_t KonfigLeseByteBegrenzt(uint8_t Adresse, uint8_t Default, uint8_t Min, uint8_t Max)
	{
	uint8_t wert;
	
	wert = KonfigLeseByte(Adresse, Default);
	if (wert < Min || wert > Max)
		{
		FehlerSpeichern(KonfigSpeicherNichtInit, Adresse);
		wert = Default;
		KonfigSchreibeByte(Adresse, wert);
		}
	return wert;
	}


void SchreibeEinzelByte(uint8_t Adresse, uint8_t Wert)
	{
	if (Wert == eeprom_read_byte(Adresse))
		return; // nichts zu tun
	eeprom_write_byte(Adresse, Wert);
	if (Wert != eeprom_read_byte(Adresse)) // rücklesen zur Fehleroffenbarung
		FehlerSpeichern(KonfigSpeicherSchreibfehler, Adresse);
	}
	
	
void KonfigSchreibeByte(uint8_t Adresse, uint8_t Wert)
	{
	SchreibeEinzelByte(Adresse, Wert);
	SchreibeEinzelByte(Adresse + BankOffset, Wert);
	SchreibeEinzelByte(Adresse + 2 * BankOffset, Wert);
	}


uint8_t KonfigSpeicherFehlercode(bool Loeschen)
	{
	uint8_t code = FehlerCode;
	if (Loeschen)
		FehlerCode = KonfigSpeicherOK;
	return code;
	}
	
	
uint8_t KonfigSpeicherFehlerAdresse()
	{
	return KonfigSpeicherFehlerAdresse;
	}
	
