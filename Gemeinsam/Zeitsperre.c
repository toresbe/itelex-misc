#include <inttypes.h>
#include <avr/eeprom.h>

#include "LokalUhr.h"
#include "LokalAusgabe.h"
#include "KonfigDialog.h"

#include "Zeitsperre.h"


typedef struct 
	{
	uint16_t Anf; //!< Anfangszeit in Minuten ab 0:00 Uhr
	uint16_t End; //!< Endzeit in Minuten ab 0:00 Uhr
	} TZeitspanne;
	

//! Zeitraum, in der das Endgerät nicht aktiv sein soll
TZeitspanne Sperrzeit[2];

//! Soll der Sperrzeitraum Wochenend-Abhängig sein.
//! bei false gelten beide Sperrzeiten täglich.
//! bei true gilt Mo-Fr Sperrzeit[0] und Samstag/Sonntag Sperrzeit[1].
bool SperrzeitWochenendAbhaengig;


//! Sperrzeiten im EEPROM, mit Reserve
//! extern, damit die Reihenfolge bestimmt werden kann
extern EEMEM uint16_t Sperrzeit_EE[10];


static bool InZeitspanne(uint8_t h, uint8_t m, TZeitspanne *zs)
	{
	if (zs->Anf == zs->End)
		return false;
	else if (zs->Anf < zs->End)
		return h*60+m >= zs->Anf && h*60+m < zs->End;
	else
		return h*60+m < zs->End || h*60+m >= zs->Anf;
		// Beispiel: Anf = 20:00 Uhr, End = 6:00 Uhr --> vor 6:00 Uhr oder nach 20 Uhr
	}
	
	
bool SperrzeitAktiv()
	{
	if (!SperrzeitWochenendAbhaengig)
		return InZeitspanne(Stunde, Minute, &(Sperrzeit[0])) || InZeitspanne(Stunde, Minute, &(Sperrzeit[1]));
	else if (Wochentag >= 6) // Samstag/Sonntag
		return InZeitspanne(Stunde, Minute, &(Sperrzeit[1]));
	else
		return InZeitspanne(Stunde, Minute, &(Sperrzeit[0]));
	}

	
void SperrzeitInit()
	{
	Sperrzeit[0].Anf = 0;
	Sperrzeit[0].End = 0;
	Sperrzeit[1].Anf = 0;
	Sperrzeit[1].End = 0;
	SperrzeitWochenendAbhaengig = false;
	}


void SperrzeitLadeEeprom()
	{
	Sperrzeit[0].Anf = eeprom_read_word(&Sperrzeit_EE[0]);
	Sperrzeit[0].End = eeprom_read_word(&Sperrzeit_EE[1]);
	Sperrzeit[1].Anf = eeprom_read_word(&Sperrzeit_EE[2]);
	Sperrzeit[1].End = eeprom_read_word(&Sperrzeit_EE[3]);
	SperrzeitWochenendAbhaengig = eeprom_read_byte(&Sperrzeit_EE[8]);
	}



void SperrzeitSpeicherEeprom()
	{
	}



static bool ZeitEingabe(uint16_t *hm)
// hm = Stunde * 60 + Minute
	{
	uint8_t h;
	uint8_t m;

	h = *hm / 60;
	m = *hm - h * 60;
	LokalZahlAusgabe(h, 2);
	LokalZahlAusgabe(m, 2);
	LokalTextAusgabeP(PSTR(" neu:     "));
	
	if (LokalZahlEingabe(&h, 2) == 0)
		return false;
	if (LokalZahlEingabe(&m, 2) == 0)
		return false;
	*hm = h * 60 + m;
	return true;
	}

	
bool SperrzeitEingabeDialog()
	{
	uint8_t i;
	
	LokalTextAusgabeP(PSTR("\r\n zeiten vierstellig eingeben"));
	for (i = 0 ; i < 2 ; i++)
		{
		LokalTextAusgabeP(PSTR("\r\n sperrzeit "));
		LokalZeichenAusgabe('a' + i);
		LokalTextAusgabeP(PSTR(" von:     "));
		if (!ZeitEingabe(&Sperrzeit[i].Anf))
			return false;
		LokalTextAusgabeP(OkStrP);
		LokalTextAusgabeP(PSTR("\r\n ... bis:     "));
		if (!ZeitEingabe(&Sperrzeit[i].End))
			return false;
		LokalTextAusgabeP(OkStrP);
		}

	LokalTextAusgabeP(PSTR("\r\n sperrzeit wochenend-abhaengig?      "));

	if (LokalBoolEingabe(&SperrzeitWochenendAbhaengig) == 0)
		return false;
	LokalTextAusgabeP(OkStrP);

	return true;
	}
