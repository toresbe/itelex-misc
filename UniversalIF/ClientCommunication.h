#ifndef __CLIENT_COMMUNICATION_H__

#define __CLIENT_COMMUNICATION_H__

#include "FifoPuffer.h"

//! Buffer for data coming from the client interface to be sent to the TWI bus.
extern TPuffer ClientInputBuffer;

//! Buffer for data to be sent to the client interface
extern TPuffer ClientOutputBuffer;

//! Any initialisation 
extern void InitClientCom();

//! Doing the in- and output on the client interface. Data will be stored in #ClientInputBuffer 
//! and read from #ClientOutputBuffer.
extern void DoClientCommunication();

#endif //def __CLIENT_COMMUNICATION_H__
