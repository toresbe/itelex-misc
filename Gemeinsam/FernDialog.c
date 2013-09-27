#include "FernDialog.h"

#include <avr/wdt.h>

#include "Bits.h"
#include "MsTimer.h"

#include "TxP2-Defs.h"
#include "SeriellUmsetz.h"
#include "BusKomm.h"
#include "BaudotCode.h"


//! Startet ein Konfigurations-Dialog mit einem Fernschreiber als Verbindungspartner 
//! auf dem TWI-Bus.
//---------------------------------------------------------------------------------
//! Funktion baut Verbindung zum Dialogpartner auf. 
//! \param SucheStartAdresse Erste TWI-Bus-Adresse ( * 2 ) wo nach einem Fernschreiber
//!        gesucht wird.
//! \return Erfolgreicher Verbindungsaufbau.

bool FernDialogVerbinden(uint8_t SucheStartAdresse)
	{
	uint8_t Adresse, PartnerAdresse;

	if (SucheStartAdresse < BusAdrMin || SucheStartAdresse > BusAdrMax || SucheStartAdresse == BusAdrUngueltig)
		SucheStartAdresse = BusAdrMin;
	else
		CLR_BIT(SucheStartAdresse, 0);

	PartnerAdresse = BusAdrUngueltig;

	Adresse = SucheStartAdresse;
	while (PartnerAdresse == BusAdrUngueltig || BusEigenAdresse == BusAdrUngueltig)
		{
		int16_t Stat = GetStatus(Adresse);
		if (Stat < 0 && BusEigenAdresse == BusAdrUngueltig)
			BusEigenAdresse = Adresse;

		if (Stat >= 0 
		    && BIT_IS_SET(Stat, StatBit_Frei) 
			&& !BIT_IS_SET(Stat, StatBit_LeitungKennung)
			&& !BIT_IS_SET(Stat, StatBit_SpezialGeraetKennung))
			PartnerAdresse = Adresse;

		if (Adresse < BusAdrMax)
			Adresse += 2;
		else
			Adresse = BusAdrMin;

		if (Adresse == SucheStartAdresse)
			return false;

		wdt_reset();

		}

	if (!BusEigenAdressePruefenUndSetzen(BusEigenAdresse))
		return false;

	BusVerbPartner = PartnerAdresse;

	BusSenden(BusEigenAdresse >> 1);
	BusWarteFertig();
	
	if (BusErgebnis != Ok)
		return false;

	SET_BIT_Status(StatBit_FsMeldEin);
	SET_BIT_Status(StatBit_FsBefEin);

	BusErgebnis = Ok; // um spätere Probleme zu vermeiden
	BusAuftrag = Nichts;
	
	BusSenden(BusKdoEin);
	BusWarteFertig();

	TMsTimer Timer;
	
	StartTimer(&Timer);

	while (true)
		{
		uint8_t Code;

		SendeLebenszeichen();
		if (GetEmpfByte(&Code))
			{
			if (Code == BusQuittEin)
				break; // der einzige normale Ausstieg aus dieser Schleife
			else
				return false;
			} // if BusKdo empfangen
		
		if (TimerVal(&Timer) > 5000)
			// Einschalt-Quittung braucht zu lange... Achtung: Umschaltung aus Lokal-Betrieb berücksichtigen
			{
			BusSenden(BusKdoSchluss);
			WarteSchlussQuittung(1500);
			return false;
			}

		} // while true

	// kurz warten bevor es rappelt...
	StartTimer(&Timer);
	while (TimerVal(&Timer) < 1200)
		SendeLebenszeichen();

	SeriellUmsetzInit(); // wird für Sendung und Empfang benötigt...

	return true;
	}


//! Empfängt einen Baudot-Code vom Dialogpartner.
//-----------------------------------------------
//! \param[out] c Empfangener Baudot-Code.	
//! \retval false, wenn Verbindung abgebaut wurde.
bool CodeEmpfangenFern(uint8_t *c)
	{
	bool Dummy;
	static uint8_t Zaehler;
	
	SerUmEmpfBitNr = SerUmEmpfWarte;

#warning "HACK aktiv nur fuer ModemAnalog Debug-Ausgabe ueber LED bei Fernkonfiguration"
	
	// HACK Blau ein bei ModemAnalog:
	PORTD |= (1<<2)

	while (SerUmEmpfBitNr != SerUmEmpfFertig)
		{
		
		SendeLebenszeichen();

		//HACK keine LED mehr ansteuern... 
		//FernDialogCallback();

		if (!EmpfPufferLeer())
			return false;

		SeriellUmsetzung(BusEmpfMark, &Dummy);
		} // while SerUmEmpfBitNr != SerUmEmpfFertig
		
	*c = SerUmEmpfDaten;
	SerUmEmpfBitNr = SerUmEmpfWarte;

	// HACK Blau aus bei ModemAnalog:
	PORTD &= ~(1<<2)
	
	Zaehler++;
	
	// HACK nur für Analog-Modem:
	PORTC = (PORTC & ~7) | (Zaehler & 7) // die unteren 3 Bit = LED rot gelb grün mit den unteren 3 Bit des Zählers füttern
	
	return true;
	}

	
//! Empfängt ein Ascii-Zeichen vom Dialogpartner.
//-----------------------------------------------
//! Kehrt nicht zurück, wenn nur eine Buchstaben- oder Ziffern-Umschaltung
//! empfangen wurde.
//! \param[out] c Empfangenes Zeichen.
//! \param[in,out] BuZiMode Flag für Buchstaben- / Ziffern-Umschaltung.
//! \retval false, wenn Verbindung abgebaut wurde.
bool ZeichenEmpfangenFern(char *c, char *BuZiMode)
	{
	uint8_t Code;
	
	while (true)
		{
		if (!CodeEmpfangenFern(&Code))
			return false;
		*c = CodeZuZeichen(Code, BuZiMode);
		if (*c != '\0')
			return true;
		}
	}
		

//! Empfängt eine Ja/Nein-Antwort vom Dialogpartner.
//--------------------------------------------------
//! Als Ja wird akzeptiert j y 1 + q, als Nein n 0 - p
//! \param[out] b Die Antwort.
//! \retval false, wenn Verbindung abgebaut wurde.

bool BoolEmpfangenFern(bool *b)
	{
	char c;
	char BuZiMode = BuMode;

	while (true)
		{
		if (!ZeichenEmpfangenFern(&c, &BuZiMode))
			return false;
		switch (c)
			{
			case 'j':
			case 'y':
			case '1':
			case '+':
			case 'q': // Taste 1 im Buchstaben-Modus
				*b = true;
				return true;
				
			case 'n':
			case '0':
			case '-':
			case 'p': // Taste 0 im Buchstaben-Modus
				*b = false;
				return true;
				
			case '.':
			case ':':
			case '/':
				return true; // keine Änderung
				
			case '\0':
				break; // BU oder ZI ignorieren
				
			default:
				break; // ignorieren TODO Fehler???
				
			} // switch c
		} // while true
	} // BoolEmpfangenFern
	

//! Kann vom Anwendungsprogramm benutzt werden, um die Anzahl der in der Funktion
//! ZahlEmpfangenFern eingegebenen Ziffern zu erfahren.
uint8_t ZahlEmpfangenFernAnzahlZiffern;
	
	
//! Empfängt eine Zahl vom Dialogpartner.
//-----------------------------------------------
//! Nur für Positive Zahlen.
//! \param[out] n Empfangenes Zahl.
//! \retval false, wenn Verbindung abgebaut wurde.
//! \remarks Siehe auch ZahlEmpfangenFernAnzahlZiffern.
	
bool ZahlEmpfangenFern(uint8_t *n)
	{
	char c;
	char BuZiMode = ZiMode;
	ZahlEmpfangenFernAnzahlZiffern = 0;
	while (true)
		{
		if (!ZeichenEmpfangenFern(&c, &BuZiMode))
			return false;
		switch (c)
			{
			case '0' ... '9':
				if (ZahlEmpfangenFernAnzahlZiffern == 0)
					*n = c - '0';
				else
					*n = 10 * (*n) + c - '0';
				ZahlEmpfangenFernAnzahlZiffern++;
				break;
			
			case ' ':
			case '\r':
			case '\n':
				if (ZahlEmpfangenFernAnzahlZiffern > 0)
					return true;
				break; // ignorieren
				
			case '.':
			case '-':
			case ':':
			case '/':
			case '+':
				return true; // wenn keine Ziffer eingegeben wird halt der alte Wert zurückgeben
				
			case '\0':
				break; // BU oder ZI ignorieren
				
			default:
				break; // ignorieren TODO Fehler???
				
			} // switch c
		} // while true
	} // ZahlEmpfangenFern

	
//! Sendet ein Baudot-Code an den Dialogpartner.
//----------------------------------------------
//! \param code Der Baudot-Code.
//! \retval false, wenn Verbindung abgebaut wurde.

bool CodeAusgabeFern(uint8_t code)
	{
	bool WarMark = true, SendMark = true;

	SerUmSendDaten = code;
	SerUmSendBitNr = SerUmSendStart;
	while (SerUmSendBitNr != SerUmSendWarte)
		{
		SendeLebenszeichen();
		FernDialogCallback();

		SeriellUmsetzung(true, &SendMark);
		if (SendMark != WarMark)
			{
			if (SendMark)
				{
				BusSenden(BusKdoMark);
				SET_BIT_Status(StatBit_FsMeldEin);
				}
			else
				{
				BusSenden(BusKdoSpace);
				CLR_BIT_Status(StatBit_FsMeldEin);
				}

			WarMark = SendMark;
			}

		uint8_t Code;

		SendeLebenszeichen();
		if (GetEmpfByte(&Code))
			return false;
		
		}
	BusSenden(BusKdoMarkWdh); // auf erfolgreiche Verarbeitung der vorherigen Kommandos warten...
	return true;
	}
	
	
//! Sendet ein ASCII-Zeichen an den Dialogpartner.
//----------------------------------------------
//! \param[in] c Das ASCII-Zeichen.
//! \param[in,out] BuZiMode Flag für Buchstaben- / Ziffern-Umschaltung.
//! \retval false, wenn Verbindung abgebaut wurde.

bool ZeichenAusgabeFern(char c, char *BuZiMode)
	{
	uint8_t Code1 = 255;
	uint8_t Code2 = 255;

	ZeichenZuCode2(c, BuZiMode, &Code1, &Code2);
	if (Code1 != 255)
		if (!CodeAusgabeFern(Code1))
			return false;
			
	if (Code2 != 255)
		if (!CodeAusgabeFern(Code2))
			return false;
		
	return true;
	}

	
//! Sendet ein Text aus dem Programmspeicher an den Dialogpartner.
//----------------------------------------------------------------
//! \param[in] s Zeiger auf den ASCII-Text im Programmspeicher.
//! \retval false, wenn Verbindung abgebaut wurde.

bool TextAusgabeFern(PGM_P s)
	{
	char BuZiMode = '\0';
	while (pgm_read_byte(s) != '\0')
		{
		if (!ZeichenAusgabeFern(pgm_read_byte(s), &BuZiMode))
			return false;
		s++;
		}
	return true;
	}


//! Sendet ein "Ja" oder "Nein" an den Dialogpartner.
//----------------------------------------------------------------
//! \param[in] b Ja oder Nein.
//! \retval false, wenn Verbindung abgebaut wurde.

bool BoolAusgabeFern(bool b)
	{
	return TextAusgabeFern(b ? PSTR(" ja ") : PSTR(" nein "));
	}
	
	
//! Sendet eine Zahl an den Dialogpartner.
//----------------------------------------------------------------
//! <b>Vorher muss auf Ziffern umgeschaltet worden sein! </b>
//! \param n Die zu druckende Zahl.
//! \param Ziffern Mindestanzahl an zu druckenden Ziffern.
//! \retval false, wenn Verbindung abgebaut wurde.

bool ZahlAusgabeFern(uint8_t n, uint8_t Ziffern)
	{
	uint8_t z = 0; // z = Zehner
	while (n >= 10)
		z++, n -= 10;
	if (z > 0 || Ziffern > 1)
		if (!ZahlAusgabeFern(z, Ziffern - 1))
			return false;
	return CodeAusgabeFern(ZeichenZuCode(n + '0', ZiMode)); 
	}

	
PROGMEM const char CrLf[] = "\r\n ";
PROGMEM const char IstText[] = ": ist = ";
PROGMEM const char NeuText[] = "  neu =   ";
	
	
//! Führt eine vollständige Zahlen-Abfrage durch.
//----------------------------------------------------------------
//! Ablauf der Abfrage: Drucken eines Textes, Drucken des alten Werts, 
//! Drucken der Eingabeaufforderung, Warten auf die Eingabe.
//! \param[in] Prompt Zeiger auf den Beschreibungstext im Programmspeicher
//! \param[in,out] Wert Abgefragte Zahl.
//! \param[in] Ziffern Mindestanzahl an zu druckenden Ziffern bei der Ausgabe des alten Wertes.
//! \retval false, wenn Verbindung abgebaut wurde.

bool ZahlAbfrageFern(PGM_P Prompt, uint8_t *Wert, uint8_t Ziffern)
	{
	if (!TextAusgabeFern(CrLf)
		|| !TextAusgabeFern(Prompt)
		|| !TextAusgabeFern(IstText)
		|| !ZahlAusgabeFern(*Wert, Ziffern)
		|| !TextAusgabeFern(NeuText)
		|| !ZahlEmpfangenFern(Wert))
		return false;
	return true;
	}
	
	
//! Führt eine vollständige Ja-Nein-Abfrage durch.
//----------------------------------------------------------------
//! Ablauf der Abfrage: Drucken eines Textes, Drucken des alten Zustands, 
//! Drucken der Eingabeaufforderung, Warten auf die Eingabe.
//! \param[in] Prompt Zeiger auf den Beschreibungstext im Programmspeicher
//! \param[in,out] Wert Variable, dessen Bit Gegenstand der Abfrage ist.
//! \param[in] Mask Bitmaske für das relevante Bit.
//! \retval false, wenn Verbindung abgebaut wurde.

bool BitAbfrageFern(PGM_P Prompt, uint8_t *Wert, uint8_t Mask)
	{
	bool Eingabe;
	Eingabe = ((*Wert) & Mask) != 0;
	if (!TextAusgabeFern(CrLf)
		|| !TextAusgabeFern(Prompt)
		|| !TextAusgabeFern(IstText)
		|| !BoolAusgabeFern(Eingabe)
		|| !TextAusgabeFern(NeuText)
		|| !BoolEmpfangenFern(&Eingabe))
		return false;
	if (Eingabe)
		*Wert |= Mask;
	else
		*Wert &= ~Mask;
	return true;
	}


