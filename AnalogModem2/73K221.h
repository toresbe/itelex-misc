#ifndef __73K221_H__

#define __73K221_H__

#include <inttypes.h>


extern void ModemInit();

extern void ModemSetReg(uint8_t RegNr, uint8_t Wert);

extern uint8_t ModemGetReg(uint8_t RegNr);

extern void ModemReset();

extern void StartDTMF(uint8_t Ziffer);
	
extern void StopDTMF();

extern void StartV21(bool Originate);

extern void StopV21();

extern void StartDetection(bool Originate);

extern void StopDetection();

extern void StartAnswerTone();

extern void StartGuardTone(bool High);

extern void StopSpecialTone();

extern void Transmit(bool Mark);

extern bool ReceiveMark();

extern bool StateChange();

extern uint8_t GetState(bool Force);

#define STATEBIT_LONGLOOP 0
#define STATEBIT_CALLPROGRESS 1 // Wählton
#define STATEBIT_ANSWERTONE 2 // Antwortton
#define STATEBIT_CARRIER 3 // Träger
#define STATEBIT_UNSCRMARKS 4 // nur für 2400
#define STATEBIT_RXD 5 // Empfagsbit

#endif //ndef __73K221_H__

