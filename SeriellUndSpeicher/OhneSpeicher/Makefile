###############################################################################
# Makefile for the project SeriellUndSpeicher
###############################################################################

## General Flags
PROJECT = SeriellUndSpeicher
MCU = atmega168
TARGET = SeriellUndSpeicher.elf
CC = avr-gcc

CPP = avr-g++

## Options common to compile, link and assembly rules
COMMON = -mmcu=$(MCU)

## Compile options common for all C compilation units.
CFLAGS = $(COMMON)
CFLAGS += -Wall -gdwarf-2 -std=gnu99 -DOHNE_SPEICHER           -fgnu89-inline          -DPLATINE_VERSION=13          -DF_CPU=6144000UL -Os -funsigned-char -funsigned-bitfields -fpack-struct -fshort-enums -DPROGIDZUSATZ=\"OHNESP\"
CFLAGS += -MD -MP -MT $(*F).o -MF dep/$(@F).d 

## Assembly specific flags
ASMFLAGS = $(COMMON)
ASMFLAGS += $(CFLAGS)
ASMFLAGS += -x assembler-with-cpp -Wa,-gdwarf2

## Linker flags
LDFLAGS = $(COMMON)
LDFLAGS +=  -Wl,-Map=SeriellUndSpeicher.map


## Intel Hex file production flags
HEX_FLASH_FLAGS = -R .eeprom -R .fuse -R .lock -R .signature

HEX_EEPROM_FLAGS = -j .eeprom
HEX_EEPROM_FLAGS += --set-section-flags=.eeprom="alloc,load"
HEX_EEPROM_FLAGS += --change-section-lma .eeprom=0 --no-change-warnings


## Include Directories
INCLUDES = -I"D:\Daten\AvrProgs\TxP2\SeriellUndSpeicher\..\Gemeinsam" -I"D:\Daten\AvrProgs\TxP2\SeriellUndSpeicher\..\..\C-Libs" 

## Objects that must be built in order to link
OBJECTS = SeriellUndSpeicher.o Taste.o TxP2-Endgeraet.o BaudotCode.o BusKomm.o FifoPuffer.o KonfigDialog.o LokalAusgabe.o SeriellUmsetz.o EepromTools.o SwTwi.o MsTimer.o BusKomm2.o LokalUhr.o 

## Objects explicitly added by the user
LINKONLYOBJECTS = 

## Build
all: $(TARGET) SeriellUndSpeicher.hex SeriellUndSpeicher.eep SeriellUndSpeicher.lss## Compile
SeriellUndSpeicher.o: ../SeriellUndSpeicher.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

Taste.o: ../Taste.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

TxP2-Endgeraet.o: ../../Gemeinsam/TxP2-Endgeraet.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

BaudotCode.o: ../../Gemeinsam/BaudotCode.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

BusKomm.o: ../../Gemeinsam/BusKomm.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

FifoPuffer.o: ../../Gemeinsam/FifoPuffer.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

KonfigDialog.o: ../../Gemeinsam/KonfigDialog.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

LokalAusgabe.o: ../../Gemeinsam/LokalAusgabe.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

SeriellUmsetz.o: ../../Gemeinsam/SeriellUmsetz.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

EepromTools.o: ../../../C-Libs/EepromTools.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

SwTwi.o: ../SwTwi.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

MsTimer.o: ../../../C-Libs/MsTimer.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

BusKomm2.o: ../../Gemeinsam/BusKomm2.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

LokalUhr.o: ../../Gemeinsam/LokalUhr.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

##Link
$(TARGET): $(OBJECTS)
	 $(CC) $(LDFLAGS) $(OBJECTS) $(LINKONLYOBJECTS) $(LIBDIRS) $(LIBS) -o $(TARGET)

%.hex: $(TARGET)
	avr-objcopy -O ihex $(HEX_FLASH_FLAGS)  $< $@

%.eep: $(TARGET)
	-avr-objcopy $(HEX_EEPROM_FLAGS) -O ihex $< $@ || exit 0

%.lss: $(TARGET)
	avr-objdump -h -S $< > $@

## Clean target
.PHONY: clean
clean:
	-rm -rf $(OBJECTS) SeriellUndSpeicher.elf dep/* SeriellUndSpeicher.hex SeriellUndSpeicher.eep SeriellUndSpeicher.lss SeriellUndSpeicher.map


## Other dependencies
-include $(shell mkdir dep 2>NUL) $(wildcard dep/*)

