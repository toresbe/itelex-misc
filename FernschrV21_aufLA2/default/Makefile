###############################################################################
# Makefile for the project FernschrV21
###############################################################################

## General Flags
PROJECT = FernschrV21
MCU = atmega168
TARGET = FernschrV21.elf
CC = avr-gcc

CPP = avr-g++

## Options common to compile, link and assembly rules
COMMON = -mmcu=$(MCU)

## Compile options common for all C compilation units.
CFLAGS = $(COMMON)
CFLAGS += -Wall -gdwarf-2 -std=gnu99 -fgnu89-inline   -DPLATINE_VERSION=21              -DF_CPU=11059200UL -Os -funsigned-char -funsigned-bitfields -fpack-struct -fshort-enums
CFLAGS += -MD -MP -MT $(*F).o -MF dep/$(@F).d 

## Assembly specific flags
ASMFLAGS = $(COMMON)
ASMFLAGS += $(CFLAGS)
ASMFLAGS += -x assembler-with-cpp -Wa,-gdwarf2

## Linker flags
LDFLAGS = $(COMMON)
LDFLAGS +=  -Wl,-Map=FernschrV21.map


## Intel Hex file production flags
HEX_FLASH_FLAGS = -R .eeprom -R .fuse -R .lock -R .signature

HEX_EEPROM_FLAGS = -j .eeprom
HEX_EEPROM_FLAGS += --set-section-flags=.eeprom="alloc,load"
HEX_EEPROM_FLAGS += --change-section-lma .eeprom=0 --no-change-warnings


## Include Directories
INCLUDES = -I"D:\Daten\AvrProgs\TxP2\FernschrV21_aufLA2\..\..\C-Libs" -I"D:\Daten\AvrProgs\TxP2\FernschrV21_aufLA2\..\Gemeinsam" 

## Objects that must be built in order to link
OBJECTS = FernschrV21.o TxP2-Endgeraet.o FifoPuffer.o BaudotCode.o BusKomm.o Taste.o SeriellUmsetz.o KonfigDialog.o LokalAusgabe.o MsTimer.o LokalUhr.o Zeitsperre.o 73K221.o KonfigSpeicher.o 

## Objects explicitly added by the user
LINKONLYOBJECTS = 

## Build
all: $(TARGET) FernschrV21.hex FernschrV21.eep FernschrV21.lss## Compile
FernschrV21.o: ../FernschrV21.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

TxP2-Endgeraet.o: ../../Gemeinsam/TxP2-Endgeraet.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

FifoPuffer.o: ../../Gemeinsam/FifoPuffer.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

BaudotCode.o: ../../Gemeinsam/BaudotCode.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

BusKomm.o: ../../Gemeinsam/BusKomm.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

Taste.o: ../Taste.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

SeriellUmsetz.o: ../../Gemeinsam/SeriellUmsetz.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

KonfigDialog.o: ../../Gemeinsam/KonfigDialog.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

LokalAusgabe.o: ../../Gemeinsam/LokalAusgabe.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

MsTimer.o: ../../../C-Libs/MsTimer.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

LokalUhr.o: ../../Gemeinsam/LokalUhr.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

Zeitsperre.o: ../../Gemeinsam/Zeitsperre.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

73K221.o: ../73K221.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

KonfigSpeicher.o: ../../Gemeinsam/KonfigSpeicher.c
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
	-rm -rf $(OBJECTS) FernschrV21.elf dep/* FernschrV21.hex FernschrV21.eep FernschrV21.lss FernschrV21.map


## Other dependencies
-include $(shell mkdir dep 2>NUL) $(wildcard dep/*)

