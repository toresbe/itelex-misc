// Speichert Konfigurationswerte im EEPROM, und zwar redundant.
// ------------------------------------------------------------
// Fred Sonnenrein

#include <avr/eeprom.h>

#include "KonfigSpeicher.h"

#include "LokalAusgabe.h"


uint8_t FehlerCode;
uint16_t FehlerAdresse;


enum { BankOffset = 160 }; // Offset der redundanten Speicherbereiche.


static void FehlerSpeichern(uint8_t code, uint16_t adresse)
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
	

uint8_t KonfigLeseByte(uint16_t Adresse, uint8_t Default)
	{
	uint8_t a, b, c;
	
	a = eeprom_read_byte((uint8_t *) Adresse);
	b = eeprom_read_byte((uint8_t *) Adresse + BankOffset);
	c = eeprom_read_byte((uint8_t *) Adresse + 2 * BankOffset);

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
		
	FehlerSpeichern(KonfigSpeicherLesefehlerSchwer, Adresse);
	KonfigSchreibeByte(Adresse, Default);
	return Default;
	} // KonfigLeseByte()

	
uint8_t KonfigLeseByteBegrenzt(uint16_t Adresse, uint8_t Default, uint8_t Min, uint8_t Max)
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


uint16_t KonfigLeseWortBegrenzt(uint16_t Adresse, uint16_t Default, uint16_t Min, uint16_t Max)
	{
	uint16_t wert;
	
	// little endian:
	wert = (KonfigLeseByte(Adresse + 1, Default >> 8) << 8) | KonfigLeseByte(Adresse, Default & 0xFF);
	if (wert < Min || wert > Max)
		{
		FehlerSpeichern(KonfigSpeicherNichtInit, Adresse);
		wert = Default;
		KonfigSchreibeWort(Adresse, wert);
		}
	return wert;
	}


bool KonfigLeseBool(uint16_t Adresse, bool Default)
	{
	return KonfigLeseByteBegrenzt(Adresse, Default ? 1 : 0, 0, 1) == 1;
	}


void SchreibeEinzelByte(uint16_t Adresse, uint8_t Wert)
	{
	if (Wert == eeprom_read_byte((uint8_t *) Adresse))
		return; // nichts zu tun
	eeprom_write_byte((uint8_t *) Adresse, Wert);
	if (Wert != eeprom_read_byte((uint8_t *) Adresse)) // rücklesen zur Fehleroffenbarung
		FehlerSpeichern(KonfigSpeicherSchreibfehler, Adresse);
	}
	
	
void KonfigSchreibeByte(uint16_t Adresse, uint8_t Wert)
	{
	SchreibeEinzelByte(Adresse, Wert);
	SchreibeEinzelByte(Adresse + BankOffset, Wert);
	SchreibeEinzelByte(Adresse + 2 * BankOffset, Wert);
	}


void KonfigSchreibeWort(uint16_t Adresse, uint16_t Wert)
	{
	KonfigSchreibeByte(Adresse + 1, Wert >> 8); // little endian
	KonfigSchreibeByte(Adresse, Wert && 0xFF);
	}


void KonfigSchreibeBool(uint16_t Adresse, bool Wert)
	{
	KonfigSchreibeByte(Adresse, Wert ? 1 : 0);
	}


uint8_t KonfigSpeicherFehlercode(bool Loeschen)
	{
	uint8_t code = FehlerCode;
	if (Loeschen)
		FehlerCode = KonfigSpeicherOK;
	return code;
	}
	
	
uint16_t KonfigSpeicherFehlerAdresse()
	{
	return FehlerAdresse;
	}


void KonfigSpeicherFehlerAusgeben()
	{
	if (FehlerCode == KonfigSpeicherOK)
		return;
	
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR("\r\r\n eeprom ")); 
#else	
	LokalTextAusgabeP(PSTR("\r\r\n eeprom ")); 
#endif //def SPRACHE_EN

	switch (FehlerCode)
		{
#ifdef SPRACHE_EN
			case KonfigSpeicherNichtInit:			LokalTextAusgabeP(PSTR("info initialized")); 	break;
			case KonfigSpeicherLesefehler:			LokalTextAusgabeP(PSTR("warning read failed")); break;
			case KonfigSpeicherLesefehlerSchwer:	LokalTextAusgabeP(PSTR("error reading")); 		break;
			case KonfigSpeicherSchreibfehler:		LokalTextAusgabeP(PSTR("error writing")); 		break;
			default: LokalTextAusgabeP(PSTR("error code ")); 	LokalZahlAusgabe(FehlerCode);		break;
#else	
			case KonfigSpeicherNichtInit:			LokalTextAusgabeP(PSTR("info initialisiert")); 	break;
			case KonfigSpeicherLesefehler:			LokalTextAusgabeP(PSTR("warnung lesefehler"));	break;
			case KonfigSpeicherLesefehlerSchwer:	LokalTextAusgabeP(PSTR("schwerer lesefehler")); break;
			case KonfigSpeicherSchreibfehler:		LokalTextAusgabeP(PSTR("schreibfehler")); 		break;
			default: LokalTextAusgabeP(PSTR("fehlercode ")); 	LokalZahlAusgabe(FehlerCode, 0);	break;
#endif //def SPRACHE_EN
		} // switch (FehlerCode)

#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR(" address ")); 
#else	
	LokalTextAusgabeP(PSTR(" adresse ")); 
#endif //def SPRACHE_EN

	LokalHexAusgabe(FehlerAdresse >> 8);
	LokalHexAusgabe(FehlerAdresse & 0xFF);

	LokalTextAusgabeP(PSTR("    \r\r\n")); 

	FehlerCode = KonfigSpeicherOK;
	}
	
	
