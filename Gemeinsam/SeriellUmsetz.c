#include "SeriellUmsetz.h"

#include "bits.h"

#include "MsTimer.h"

// Empfang: Umsetzung Seriell (Baudot) --> Parallel (Daten)

//! Aktuell von Seriell nach Parallel umgesetztes Bit.
//----------------------------------------------------
//! 0 = Grundzustand, 1 = Startbit-Prüfung, 2-6 = Datenbits 1-5, 7 = Stopbit-Prüfung, 
//! 8 = Empfang beendet, Daten zur Verarbeitung bereit.
volatile uint8_t SerUmEmpfBitNr; 

//! Hier wird das von seriell zu parallel umgesetzte Byte gespeichert.
volatile uint8_t SerUmEmpfDaten; 

//! Wird auf true gesetzt, wenn Stop-Bit  nicht 1 war.
volatile bool SerUmEmpfFehler; 

//! Zähler zum Ausfiltern von kurzen Störimpulsen.
static volatile int16_t SerUmEmpfPegel; 

// Senden: Umsetzung Parallel (Daten) --> Seriell (Baudot)
//--------------------------------------------------------

//! Aktuell von parallel nach seriell umgesetztes Bit.
// ---------------------------------------------------
//! 0 = Grundzustand, 1 = Sendedaten bereit, 2 = Startbit, 3-7 = Datenbits 1-5, 8 = Stopbit.
volatile uint8_t SerUmSendBitNr; 
	
//! Aktuell von parallel nach seriell umzusetzendes Byte.
volatile uint8_t SerUmSendDaten;


//! Initialisiert die serielle Umsetzung 
void SeriellUmsetzInit()
	{
	SerUmEmpfBitNr = SerUmEmpfWarte;
	SerUmSendBitNr = SerUmSendWarte;
	SerUmEmpfPegel = 128;
	}


// falls die Bitlänge (1000 / Baudrate) abweichend sein soll, muss diese als Compiler-Define gesetzt werden

#ifndef BIT_LENGTH

//! Bit-Länge in Millisekunden
#define BIT_LENGTH 20

#endif


	
//! Zeitgeber für Umsetzung seriell - parallel.	
TMsTimer SerUmTimerE;


//! Zeitgeber für Umsetzung parallel - seriell.
TMsTimer SerUmTimerA;
	

static void EmpfPegelBearbeiten(bool SeriellEing)
	{
	if (SeriellEing)
		{
		if (SerUmEmpfPegel < 255)
			SerUmEmpfPegel++;
		}
	else
		{
		if (SerUmEmpfPegel > 0)
			SerUmEmpfPegel--;
		}
	}


// HACK: Debugging passt für TW39-Platine:
#include <avr/io.h> 
#define Debug_SerUmEmpfAbtastStart(bit) SET_BIT(PORTB, 3)
#define Debug_SerUmEmpfAbtastEnde(bit) CLR_BIT(PORTB, 3)
// PORT B3 = MOSI = Pin 1 Programmierstecker


#ifndef Debug_SerUmEmpfAbtastStart

static inline void nop(uint8_t bit) { }

#define Debug_SerUmEmpfAbtastStart(bit) nop(bit)
#define Debug_SerUmEmpfAbtastEnde(bit) nop(bit)

#endif


//! Durchführung der Seriell - Parallel - Umsetzung und umgekehrt.
// ----------------------------------------------------------------
//! Funktion ist zyklisch aufzurufen, um die Umsetzung durchzuführen.
//! Keine Synchronisation erforderlich, bei Umsetzung von seriell nach parallel
//! sollte aber die Zykluszeit dieses Aufrufs unter 1 Millisekunde sein.
//! \param[in] SeriellEing Zustand Mark / Space für Umsetzung seriell - parallel.
//! \param[out] SeriellAusg Zustand Mark / Space für Umsetzung parallel - seriell.
void SeriellUmsetzung(bool SeriellEing, bool *SeriellAusg) // und auswerten
	{
	// Code-Ausgabe
	// ------------
	switch (SerUmSendBitNr)
		{
		case SerUmSendStart: // Ausgabe starten, aber nur, wenn nicht gerade empfangen wird
			if ((SerUmEmpfBitNr == SerUmEmpfWarte || SerUmEmpfBitNr == SerUmEmpfFertig)
				&& TimerVal(&SerUmTimerE) >= 3 * BIT_LENGTH) 
				{
				*SeriellAusg = false; // Dies ist das Start-Bit
				StartTimer(&SerUmTimerA);
				SerUmSendBitNr = 2;
				SerUmSendDaten &= 0x1F;
				SerUmSendDaten <<= 2; // Bit 7 wird zuerst gesendet, dies ist das Startbit
				SerUmSendDaten |= 3; // damit als letztes eine 1 gesendet wird
					// Bildlich: 76543210 <-- Bit-Nr
					//           ---DDDDD <-- zu sendende Datenbits for dem 'Start'
					//           0DDDDD11 <-- zu sendende Bits nach dem 'Start'
					//           ^   dieses Bit wird gesendet, danach wird nach links geschoben
				}
			break;

		case 2 ... 7: // Start- oder Daten-Bit läuft gerade
			*SeriellAusg = BIT_IS_SET(SerUmSendDaten, 7);
			if (TimerVal(&SerUmTimerA) >= BIT_LENGTH)
				{ // Bit beendet
				DecrementTimer(&SerUmTimerA, BIT_LENGTH);
				SerUmSendDaten <<= 1;
				SerUmSendBitNr++;
				}
			break;

		case 8: // Stop-Bit läuft gerade
			*SeriellAusg = true;
			if (TimerVal(&SerUmTimerA) >= BIT_LENGTH * 3/2)
				{ // Stopbit beendet
				StartTimer(&SerUmTimerA);
				SerUmSendBitNr = SerUmSendWarte; // fertig für die nächsten Daten
				}
			break;

		} // switch SerUmSendBitNr

	// Code-Interpretation
	// -------------------
	
	switch (SerUmEmpfBitNr)
		{
		case SerUmEmpfFertig: // letzer Empfang fertig, aber nicht ausgewertet, neues Zeichen überschreibt...
			if (TimerVal(&SerUmTimerE) <= 2)
				break;
			//! 2 ms warten, damit das auswertende Programm die Chance hat, den Empfang zu verwenden.
			
			// absichtlich kein break!
			
		case SerUmEmpfWarte: // warte auf Start-Bit
			if (!SeriellEing) // Pausenschritt
				{
				StartTimer(&SerUmTimerE);
				SerUmEmpfBitNr = 1;
				SerUmEmpfDaten = 0;
				SerUmEmpfPegel = 128; // Mittel
				SerUmEmpfFehler = false;
				Debug_SerUmEmpfAbtastStart(1); // Start-Bit
				}
			break;

		case 1: // im Start-Bit
			EmpfPegelBearbeiten(SeriellEing);
			if (SerUmEmpfPegel > 150) // zu viele 1-Impulse im Startbit --> von vorn
				{
				SerUmEmpfBitNr = SerUmEmpfWarte; //! \todo Zum debuggen etwas vorsehen.
				Debug_SerUmEmpfAbtastEnde(1);
				}
			else if (TimerVal(&SerUmTimerE) > BIT_LENGTH / 2) 
				{
				// Wir sind in der Mitte des Startbits...
				Debug_SerUmEmpfAbtastEnde(1);
				if (SerUmEmpfPegel < 128) // Startbit gültig, Daten empfangen
					{
					SerUmEmpfBitNr = 2;
					SerUmEmpfPegel = 128;
					DecrementTimer(&SerUmTimerE, BIT_LENGTH / 2 - 2);
						// da in der Mitte des Startbits der Timer neu gestartet wird, werden die 
						// Datenbits auch in der Mitte abgetastet. Da 4 ms abgetastet werden
						// soll der Beginn auf Bit-Mitte - 2 ms liegen.
					}
				else // Startbit nicht gültig, von vorne...
					{
					SerUmEmpfBitNr = SerUmEmpfWarte;
					}
				}	
			break;

		case 2 ... 6 : // Datenbit
			if (TimerVal(&SerUmTimerE) < BIT_LENGTH - 4) 
				break; // nur die letzten 4 Milli-Sekunden auswerten
			Debug_SerUmEmpfAbtastStart(SerUmEmpfBitNr); // wird ggf. mehrfach aufgerufen!
			EmpfPegelBearbeiten(SeriellEing);
			if (TimerVal(&SerUmTimerE) >= BIT_LENGTH) // Bit beendet
				{
				Debug_SerUmEmpfAbtastEnde(SerUmEmpfBitNr);
				SerUmEmpfDaten <<= 1;
				if (SerUmEmpfPegel >= 128)
					SerUmEmpfDaten |= 1;
				SerUmEmpfBitNr++;
				DecrementTimer(&SerUmTimerE, BIT_LENGTH);
				SerUmEmpfPegel = 128;
				}
			break;
			
		case 7: // Stopbit
			if (TimerVal(&SerUmTimerE) < BIT_LENGTH - 4) 
				// Bei den Datenbits wurden 4 ms in der Bit-Mitte abgetastet, also 8 ms vom 
				// Anfang beginnend. Beim Stop-Bit wird das genauso gemacht, da bleiben dann 
				// aber nach Ende des Abtast-Bereichs noch 28 ms übrig.
				break;
			Debug_SerUmEmpfAbtastStart(7); // wird ggf. mehrfach aufgerufen!
			EmpfPegelBearbeiten(SeriellEing);
			if (TimerVal(&SerUmTimerE) >= BIT_LENGTH * 3/4) // die Hälfte des 3/4 Bit beendet
				{
				Debug_SerUmEmpfAbtastEnde(7);
				if (SeriellEing) // Strom wieder da
					{
					SerUmEmpfFehler = (SerUmEmpfPegel < 128); 
					SerUmEmpfBitNr = SerUmEmpfFertig;
					StartTimer(&SerUmTimerE); 
						// wird noch mal gestartet, damit beim Umsetzen für die Ausgabe
						// noch der beginn des nächsten ggf. im Empfang laufenden Zeichens 
						// gewartet wird.
					}
				else
					{ // Strom immer noch unterbrochen -> Kann eigentlich kein Stopbit sein.
					// TODO: Was soll das und was bringt das eigentlich?
					}
				}
			break;

		} // switch (SerUmEmpfBitNr)
	}


