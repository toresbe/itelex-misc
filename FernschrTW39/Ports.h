#ifndef __PORTS_H__

#define __PORTS_H__


// Platinen-Version
// ================

#define PLATINE_VERSION 13 // 1.3x


// Schnittstellen
// --------------

#if (PLATINE_VERSION < 13)

#warning "Nur fuer alte Platinen-Version 1.0 bis 1.2"

#define TAST_PORT PORTD
#define TAST_IPORT PIND
#define TAST_BIT 6

#define LED_ROT_PORT PORTD
#define LED_ROT_DDR DDRD
#define LED_ROT_BIT 7

#define LED_GELB_PORT PORTB
#define LED_GELB_DDR DDRB
#define LED_GELB_BIT 0

#define LED_GRUEN_PORT PORTB
#define LED_GRUEN_DDR DDRB
#define LED_GRUEN_BIT 1

#define LED_BLAU_PORT PORTB
#define LED_BLAU_DDR DDRB
#define LED_BLAU_BIT 2

// Schleifenschluﬂ-Ausgabe
#define FS_AUSG_PORT PORTD
#define FS_AUSG_DDR DDRD
#define FS_AUSG_BIT 0

// Einschaltung (Ausgabe, Polarit‰t der Schleife)
#define FS_AKTIV_PORT PORTD
#define FS_AKTIV_DDR DDRD
#define FS_AKTIV_BIT 1

// Strom-Einlesung
#define FS_EING_PORT PORTC
#define FS_EING_IPORT PINC
#define FS_EING_BIT 3

#else // PLATINE_VERSION >= 13

#define TAST_PORT PORTD
#define TAST_IPORT PIND
#define TAST_BIT 0

#define LED_ROT_PORT PORTD
#define LED_ROT_DDR DDRD
#define LED_ROT_BIT 1

#define LED_GELB_PORT PORTD
#define LED_GELB_DDR DDRD
#define LED_GELB_BIT 2

#define LED_GRUEN_PORT PORTD
#define LED_GRUEN_DDR DDRD
#define LED_GRUEN_BIT 3

#define LED_BLAU_PORT PORTD
#define LED_BLAU_DDR DDRD
#define LED_BLAU_BIT 4

// Schleifenschluﬂ-Ausgabe
#define FS_AUSG_PORT PORTB
#define FS_AUSG_DDR DDRB
#define FS_AUSG_BIT 0

// Einschaltung (Ausgabe, Polarit‰t der Schleife)
#define FS_AKTIV_PORT PORTD
#define FS_AKTIV_DDR DDRD
#define FS_AKTIV_BIT 7

// Strom-Einlesung
#define FS_EING_PORT PORTD
#define FS_EING_IPORT PIND
#define FS_EING_BIT 6

#ifdef PARALLELAUSGABE

// Schleifenschluﬂ-Ausgabe
#define FS2_AUSG_PORT PORTB
#define FS2_AUSG_DDR DDRB
#define FS2_AUSG_BIT 2

// Einschaltung (Ausgabe, Polarit‰t der Schleife)
#define FS2_AKTIV_PORT PORTB
#define FS2_AKTIV_DDR DDRB
#define FS2_AKTIV_BIT 1

// Strom-Einlesung
#define FS2_EING_PORT PORTD
#define FS2_EING_IPORT PIND
#define FS2_EING_BIT 5

#endif //def PARALLELAUSGABE


#define LED_EIN(LED) SET_BIT(LED_ ## LED ## _PORT, LED_ ## LED ## _BIT)

#define LED_AUS(LED) CLR_BIT(LED_ ## LED ## _PORT, LED_ ## LED ## _BIT)


#endif

#endif //ndef __PORTS_H__
