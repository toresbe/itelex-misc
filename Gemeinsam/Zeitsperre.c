#include <inttypes.h>
#include <avr/eeprom.h>

#include "LokalUhr.h"
#include "LokalAusgabe.h"
#include "KonfigDialog.h"

#include "Zeitsperre.h"


#ifndef OHNE_ZEITSPERRE
// damit Konfig-Abh�ngig alles entfallen kann


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

//! Wird gesetzt wenn wärend der Sperrzeit eine Bedienung vorgenommen wird.
//! nach der nächsten Zeit-Aktualisierung wird dann für 10 Minuten die Sperre 
//! ausgesetzt
static bool AussetzenAktivieren;

//! Speichert den Zeitraum der Aussetzung der Zeitsperre.
static TZeitspanne Aussetzung;

void SperrzeitInit()
	{
	Sperrzeit[0].Anf = 0;
	Sperrzeit[0].End = 0;
	Sperrzeit[1].Anf = 0;
	Sperrzeit[1].End = 0;
	SperrzeitWochenendAbhaengig = false;
	AussetzenAktivieren = false;
	Aussetzung.Anf = 0;
	Aussetzung.End = 0;
	}



static void ZeitBereichKorrektur(uint16_t *x)
	{
	if (*x > 24*60)
		*x = 24*60;
	}

	
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
	if (AussetzenAktivieren)
		{
		AussetzenAktivieren = false;
		Aussetzung.Anf = Stunde * 60 + Minute;
		Aussetzung.End = Aussetzung.Anf + 10; // Dauer in Minuten
		if (Aussetzung.End > 24*60) 
			Aussetzung.End -= 24*60;
		return false; // da auf jeden Fall jetzt die Aussetzung wirkt
		}

	if (InZeitspanne(Stunde, Minute, &Aussetzung))
		return false; // Aussetzung wirkt noch
	else
		Aussetzung.Anf = 0, Aussetzung.End = 0; // keine erneute Wirksamkeit der Aussetzung, jetzt die gesetzten Sperrzeiten prüfen
	
	if (!SperrzeitWochenendAbhaengig)
		return InZeitspanne(Stunde, Minute, &(Sperrzeit[0])) || InZeitspanne(Stunde, Minute, &(Sperrzeit[1]));
	else if (Wochentag >= 6) // Samstag/Sonntag
		return InZeitspanne(Stunde, Minute, &(Sperrzeit[1]));
	else
		return InZeitspanne(Stunde, Minute, &(Sperrzeit[0]));
	}

	
void SperrzeitAussetzen()
	{
	AussetzenAktivieren = true;
	}

	
void SperrzeitLadeEeprom(TSperrzeitDaten *eedat)
	{
	Sperrzeit[0].Anf = eeprom_read_word(&(*eedat)[0]);
	ZeitBereichKorrektur(&Sperrzeit[0].Anf);
	Sperrzeit[0].End = eeprom_read_word(&(*eedat)[1]);
	ZeitBereichKorrektur(&Sperrzeit[0].End);
	Sperrzeit[1].Anf = eeprom_read_word(&(*eedat)[2]);
	ZeitBereichKorrektur(&Sperrzeit[1].Anf);
	Sperrzeit[1].End = eeprom_read_word(&(*eedat)[3]);
	ZeitBereichKorrektur(&Sperrzeit[1].End);
	SperrzeitWochenendAbhaengig = (eeprom_read_word(&(*eedat)[8]) == 1);
	}



void SperrzeitSpeicherEeprom(TSperrzeitDaten *eedat)
	{
	if (Sperrzeit[0].Anf != eeprom_read_word(&(*eedat)[0]))
		eeprom_write_word(&(*eedat)[0], Sperrzeit[0].Anf);
	if (Sperrzeit[0].End != eeprom_read_word(&(*eedat)[1]))
		eeprom_write_word(&(*eedat)[1], Sperrzeit[0].End);
	if (Sperrzeit[1].Anf != eeprom_read_word(&(*eedat)[2]))
		eeprom_write_word(&(*eedat)[2], Sperrzeit[1].Anf);
	if (Sperrzeit[1].End != eeprom_read_word(&(*eedat)[3]))
		eeprom_write_word(&(*eedat)[3], Sperrzeit[1].End);
	if (SperrzeitWochenendAbhaengig != (eeprom_read_word(&(*eedat)[8]) == 1))
		eeprom_write_word(&(*eedat)[8], SperrzeitWochenendAbhaengig ? 1 : 0);
	}



static bool ZeitEingabe(uint16_t *hm)
// hm = Stunde * 60 + Minute
	{
	uint8_t h;
	uint8_t m;
	uint16_t res;

	h = *hm / 60;
	m = *hm - h * 60;
	LokalZahlAusgabe(h, 2);
	LokalZahlAusgabe(m, 2);
	LokalTextAusgabeP(NeuStrP);
	
	res = LokalZahlEingabe(&h, 2);
	if (res < 0) // abbruch
		return false;
	if (res == 0) // unverändert ( . eingegeben)
		return true;
	if (LokalZahlEingabe(&m, 2) < 0)
		return false;
	*hm = h * 60 + m;
	ZeitBereichKorrektur(hm);
	return true;
	}

	
bool SperrzeitEingabeDialog()
	{
	uint8_t i;
	
#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR("\r\n enter times with 4 digits without punctuation"));
#else
	LokalTextAusgabeP(PSTR("\r\n zeiten vierstellig ohne punkt und komma eingeben"));
#endif

	for (i = 0 ; i < 2 ; i++)
		{
#ifdef SPRACHE_EN
		LokalTextAusgabeP(PSTR("\r\n lock time "));
		LokalZeichenAusgabe('a' + i);
		LokalTextAusgabeP(PSTR(" starting at: "));
#else
		LokalTextAusgabeP(PSTR("\r\n sperrzeit "));
		LokalZeichenAusgabe('a' + i);
		LokalTextAusgabeP(PSTR(" von: "));
#endif

		if (!ZeitEingabe(&Sperrzeit[i].Anf))
			return false;
		LokalTextAusgabeP(OkStrP);

#ifdef SPRACHE_EN
		LokalTextAusgabeP(PSTR("\r\n ... until: "));
#else
		LokalTextAusgabeP(PSTR("\r\n ... bis: "));
#endif

		if (!ZeitEingabe(&Sperrzeit[i].End))
			return false;
		LokalTextAusgabeP(OkStrP);
		
		}

#ifdef SPRACHE_EN
	LokalTextAusgabeP(PSTR("\r\n lock times depending on weekend? current: "));
#else
	LokalTextAusgabeP(PSTR("\r\n sperrzeit wochenend-abhaengig? aktuell: "));
#endif

	LokalBoolAusgabe(SperrzeitWochenendAbhaengig);
	LokalTextAusgabeP(NeuStrP);

	if (LokalBoolEingabe(&SperrzeitWochenendAbhaengig) == 0)
		return false;
	LokalTextAusgabeP(OkStrP);

	return true;
	}


#endif //ndef OHNE_ZEITSPERRE
