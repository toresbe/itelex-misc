#include <avr/io.h>
#include <inttypes.h>

#include "Ports.h"
#include "bits.h"

#include "73K221.h"

//! Initdialisiert das Hardware-Interface zum Modem-IC.
void ModemInit()
	{
	init_EXCLK();
	init_RD();
	init_WR();
	init_AD0();
	init_AD1();
	init_AD2();
	init_AD7();
	init_TXD();
	init_RXD();
	init_INT();
	set_RD();
	set_WR();
	set_EXCLK();
	}


//! Wartet einen halben Taktzyklus.
static void HalfClock()
	{
	for (volatile uint16_t x = 0 ; x < 100 ; x++)
		;
	}


//! Setzt ein Register des Modem-IC auf einen bestimmten Wert.
//! \param RegNr Register-Nummer.
//! \param Wert Zu speichernder Wert.
	
void ModemSetReg(uint8_t RegNr, uint8_t Wert)
	{
	bset_AD0(BIT_IS_SET(RegNr, 0));
	bset_AD1(BIT_IS_SET(RegNr, 1));
	bset_AD2(BIT_IS_SET(RegNr, 2));
	for (uint8_t bit = 0 ; bit <= 7 ; bit++)
		{
		bseto_AD7(BIT_IS_SET(Wert, 0)); // Wert wird als nächstes geschoben
		HalfClock();
		clr_EXCLK();
		HalfClock();
		set_EXCLK();
		Wert >>= 1;
		}
	clr_WR();
	HalfClock();
	set_WR();
	HalfClock();
	inp_AD7();
	}


//! Fragt ein Register des Modem-IC ab.
//! \param RegNr Register-Nummer.
//! \returns Inhalt des Registers.
	
uint8_t ModemGetReg(uint8_t RegNr)
	{
	uint8_t Res = 0;

	clr_EXCLK();
	bset_AD0(BIT_IS_SET(RegNr, 0));
	bset_AD1(BIT_IS_SET(RegNr, 1));
	bset_AD2(BIT_IS_SET(RegNr, 2));
	clr_RD();
	for (uint8_t bit = 0 ; bit <= 7 ; bit++)
		{
		clr_EXCLK();
		HalfClock();
		Res >>= 1;
		if (get_AD7())
			Res |= (1<<7);
		set_EXCLK();
		HalfClock();
		}
	set_RD();
	return Res;
	}
	
	
//! Führt ein Reset des Modem-IC durch.	
void ModemReset()
	{
	ModemSetReg(1, 1<<2);
	}


//! Schaltet einen DTMF-Ton ein.
//! \param Ziffer DTMF-Ziffer.
void StartDTMF(uint8_t Ziffer)
	{ 
	if (Ziffer == 0)
		Ziffer = 10;
	ModemSetReg(3, 0x10 | (Ziffer & 0xF));
	ModemSetReg(0, 0x1A);
	}


//! Schaltet DTMF-Töne wieder aus.
void StopDTMF()
	{
	ModemSetReg(0, 0);
	ModemSetReg(3, 0);
	}


//! Schaltet das Modem-IC in die V21-Betriebsart.
//! \param Originate Ist das Modem der Anrufer?	
void StartV21(bool Originate)
	{
	ModemSetReg(3, 0);
	ModemSetReg(0, Originate ? 0x33 : 0x32);
	ModemSetReg(1, 0x20);
	}


//! Schaltet den V21-Mode des Modem-IC wieder aus.	
void StopV21()
	{
	ModemSetReg(0, 0);
	ModemSetReg(1, 0x20);
	}


//! Schaltet das Modem-IC in den Kennton-Detektions-Modus.
void StartDetection(bool Originate)
	{
	ModemSetReg(3, 0);
	ModemSetReg(0, Originate ? 0x31 : 0x30);
	ModemSetReg(1, 0x20);
	}
	
	
//! Schaltet den Kennton-Detektions-Modus des Modem-IC aus.
void StopDetection()
	{
	ModemSetReg(0, 0);
	ModemSetReg(1, 0x20);
	}

	
//! Schaltet den Antworton des Modem-IC ein.
void StartAnswerTone()
	{
	ModemSetReg(1, 0x20);
	ModemSetReg(3, 0x20);
	ModemSetReg(0, 0x32);
	}
	

//! Schaltet den Guard-Ton des Modem-IC ein.
void StartGuardTone(bool High)
	{
	ModemSetReg(0, 0x30);
	ModemSetReg(1, 0x20);
	ModemSetReg(3, High ? 0x40 : 0x41);
	}


//! Schaltet Antworton und Guard-Ton des Modem-IC aus.	
void StopSpecialTone()
	{
	ModemSetReg(3, 0);
	}
	

//! Schaltet im V21-Betrieb auf Mark oder Space.
//! \param Mark Mark = true, Space = false. Grundstellung ist Mark.
void Transmit(bool Mark)
	{
	bset_TXD(Mark);
	}


//! Frage das Modem-IC nach dem aktuell empfangenen Pegel.
//! \retval true bei Empfang von Mark (Grundstellung).
bool ReceiveMark()
	{
	return get_RXD();
	}


//! Prüft auf Status-Änderungen des Modem-IC.
//! \retval true wenn sich ein wesentlicher Status im Modem-IC geändert hat.
bool StateChange()
	{
	return !get_INT();
	}
	
	
//! Liefert das Status-Byte des Modem-IC.
//! \param Force wenn false, wird das Register des Modem-IC nur dann tatsächlich 
//! ausgelesen, wenn es sich entsprechend StateChange() auch geändert hat. Sonst
//! wird der bekannte letzte Wert geliefert.
//! \returns Status-Byte des Modem-IC.
uint8_t GetState(bool Force)
	{
	static uint8_t Last = 0;
	
	if (Force || StateChange())
		Last = ModemGetReg(2);
	return Last;
	}
	
