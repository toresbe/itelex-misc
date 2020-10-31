// Definitions for the Client Communication interface of the UniversalIF
// =====================================================================

// ---------------------------------------------------------------------
// All constants have to be chosen in accordance to the TxP2-Defs.h 
// file. SO DON'T CHANGE ANYTHINNG UNLESS YOU KNOW EXACTLY WHAT YOU DO.
// ---------------------------------------------------------------------

// This file may be useful for any application connected to the 
// UniversalIF. Therefor all definitions are preceded with Txi_
// as Telex interface

#ifndef __CLIENT_COM_DEFS_H__

#define __CLIENT_COM_DEFS_H__

// Command codes and reply codes
// -----------------------------

#define Txi_Idle			0x00

#define Txi_ConnectMin		0x01

#define Txi_ConnectMax		0x6E

#define Txi_ConnectOK		0x77

#define Txi_ConnectBusy		0x78

#define Txi_NoAnswer		0x79

#define Txi_QueryStatus		0x7D

#define Txi_SetParam		0x7E

#define Txi_QueryParam		0x7F

#define Txi_PrintcodeMin	0x80

#define Txi_PrintcodeMax	0x9F

#define Txi_DirectMin		0xA0

#define Txi_DirectMax		0xFE

#define Txi_ActivateCmd		0xA3

#define Txi_ActivateAck		0xA5

#define Txi_DialAck			0xA6

#define Txi_Dial0			0xB0

#define Txi_Dial9			0xB9

#define Txi_DisconnCmd		0xAA

#define Txi_DisconnAck		0xAC

#define Txi_Error			0xFF


// Error Flags
// -----------

#define Txi_ErrFlag_InputBufferOverflow		0x01

#define Txi_ErrFlag_OutputBufferOverflow	0x02

#define Txi_ErrFlag_AlreadyConnected		0x04

#define Txi_ErrFlag_NotConnected			0x08

#define Txi_ErrFlag_TwiTimeout				0x10

#define Txi_ErrFlag_TwiCodeError			0x20  // invalid code received on TWI bus

#define Txi_ErrFlag_InvalidCmd				0x80


// Parameter Addresses
// -------------------

#define Txi_Param_OwnAddress			0x00

#define Txi_Param_DefaultStatus			0x01

#define Txi_Param_ErrorCode				0x40

#define Txi_Param_CurrentPartner		0x42

#define Txi_Param_CurrentStatus			0x43


#endif //ndef __CLIENT_COM_DEFS_H__
