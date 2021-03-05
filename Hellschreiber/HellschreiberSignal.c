//================================================================
// Schnittstelle für i-Telex-System für Hellschreiber (z.B. Hell GL72)
// für ATmega168 auf Platine Seriell+Spezial
// Teil Signalverarbeitung (auch Stand-Alone nutzbar)
//================================================================

#include <avr/io.h>


// Typen
// =========================================

typedef enum {
	Ruhe, // Stillstand
	Rufsignal, // Hellschreiber soll bei einer kommenden Verbindung eingeschaltet werden
	Ausschaltsignal, // Hellschreiber soll stillgesetzt werden
	
	// Eingeschaltet ist einer der folgenden Zustände:
	Schreiben, // Zeichen werden an den Hellschreiber gesendet
	Lesen // Zeichen können am Hellschreiber eingegeben werden
	} TBetriebsart;
	
	
// Globale Variablen


int main(void)
{
    /* Replace with your application code */
    while (1) 
    {
	kkk();
    }
}

