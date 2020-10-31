###############################################################################
# Makefile for the project AnalogModem2
###############################################################################

## General Flags
PROJECT = AnalogModem2
MCU = atmega168
TARGET = AnalogModem2.elf
CC = avr-gcc

CPP = avr-g++

## Options common to compile, link and assembly rules
COMMON = -mmcu=$(MCU)

## Compile options common for all C compilation units.
CFLAGS = $(COMMON)
CFLAGS += -Wall -gdwarf-2 -std=gnu99   -DPLATINE_VERSION=20             -DF_CPU=11059200UL -O2 -funsigned-char -funsigned-bitfields -fpack-struct -fshort-enums
CFLAGS += -MD -MP -MT $(*F).o -MF dep/$(@F).d 

## Assembly specific flags
ASMFLAGS = $(COMMON)
ASMFLAGS += $(CFLAGS)
ASMFLAGS += -x assembler-with-cpp -Wa,-gdwarf2

## Linker flags
LDFLAGS = $(COMMON)
LDFLAGS +=  -Wl,-Map=AnalogModem2.map


## Intel Hex file production flags
HEX_FLASH_FLAGS = -R .eeprom -R .fuse -R .lock -R .signature

HEX_EEPROM_FLAGS = -j .eeprom
HEX_EEPROM_FLAGS += --set-section-flags=.eeprom="alloc,load"
HEX_EEPROM_FLAGS += --change-section-lma .eeprom=0 --no-change-warnings


## Include Directories
INCLUDES = -I"D:\Daten\AvrProgs\TxP2\AnalogModem2\..\Gemeinsam" -I"D:\Daten\AvrProgs\TxP2\AnalogModem2\..\..\C-Libs" 

## Objects that must be built in order to link
OBJECTS = 73K221.o BaudotCode.o SeriellUmsetz.o AnalogModem2.o BusKomm.o MsTimer.o Taste.o FernDialog.o BusKomm2.o LokalUhr.o 

## Objects explicitly added by the user
LINKONLYOBJECTS = 

## Build
all: $(TARGET) AnalogModem2.hex AnalogModem2.eep AnalogModem2.lss## Compile
73K221.o: ../73K221.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

BaudotCode.o: ../../Gemeinsam/BaudotCode.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

SeriellUmsetz.o: ../../Gemeinsam/SeriellUmsetz.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

AnalogModem2.o: ../AnalogModem2.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

BusKomm.o: ../../Gemeinsam/BusKomm.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

MsTimer.o: ../../../C-Libs/MsTimer.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

Taste.o: ../Taste.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

FernDialog.o: ../../Gemeinsam/FernDialog.c
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
	-rm -rf $(OBJECTS) AnalogModem2.elf dep/* AnalogModem2.hex AnalogModem2.eep AnalogModem2.lss AnalogModem2.map


## Other dependencies
-include $(shell mkdir dep 2>NUL) $(wildcard dep/*)

