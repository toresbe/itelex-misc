#ifndef __SWTWI_H__

#define __SWTWI_H__

// das Ganze nur, wenn es nicht abgeschaltet ist
#ifndef OHNE_SPEICHER

// direkte Eeprom-Zugriffsfunktionen
// ---------------------------------

bool EeAbschliessen();

void XEepromDebug();

void DoSwTwi();

// Schmittstellenfunktionen für den "Anrufbeantworter"
// ---------------------------------------------------

void MsgSpeicherInit();

void AufzeichnungZeichen(char c);

void AufzeichnungZahl2(uint8_t x);

bool AufzeichnungBeginn(uint8_t Jahr, uint8_t Monat, uint8_t Tag, uint8_t Stunde, uint8_t Minute);

void AufzeichnungEnde();

void AufzeichnungAbbruch();

void WiedergabeStart();
	// initialisiert die Wiedergabe. 
	// Danach muss WiedergabeNaechsteMeldung aufgerufen werden
	
bool WiedergabeNaechsteMeldung();
	// liefert true, wenn noch eine ungelesene Meldung gefunden wurde...
	// darf auch mehrfach aufgerufen werden, bleibt bei neuer Meldung stehen

char WiedergabeZeichen();
	// liefert fortlaufend '\0' am Meldungsende

void WiedergabeLoescheAktuelleMeldung();
	// springt auch automatisch zur nächsten Meldung

void WiedergabeEnde();

#endif //ndef OHNE_SPEICHER

#endif //ndef __SWTWI_H__
