/*
 * HellCodes.h
 *
 * Created: 05.03.2021 12:33:19
 *  Author: sonne-fr
 */ 


#ifndef HELLCODES_H_
#define HELLCODES_H_


#ifdef DEBUG

// lesbare Codes
#define HellEinschaltBefehl '^'
#define HellAusschaltBefehl '~'
#define HellEinschaltMeldung '\005' 
#define HellAusschaltMeldung '\001'

#else

// Codes mit Bit 7 gesetzt
#define HellEinschaltBefehl '\205'
#define HellAusschaltBefehl '\213'
#define HellEinschaltMeldung '\206'
#define HellAusschaltMeldung '\214'

#endif

#endif /* HELLCODES_H_ */