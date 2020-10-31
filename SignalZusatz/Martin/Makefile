###############################################################################
# Makefile for the project SignalZusatz
###############################################################################

## General Flags
PROJECT = SignalZusatz
MCU = atmega168
TARGET = SignalZusatz.elf
CC = avr-gcc

CPP = avr-g++

## Options common to compile, link and assembly rules
COMMON = -mmcu=$(MCU)

## Compile options common for all C compilation units.
CFLAGS = $(COMMON)
CFLAGS += -Wall -gdwarf-2 -std=gnu99 -DF_CPU=6144000UL -O2 -funsigned-char -funsigned-bitfields -fpack-struct -fshort-enums -DKONF_MARTIN -DPROGIDZUSATZ=\"Martin\" 
CFLAGS += -MD -MP -MT $(*F).o -MF dep/$(@F).d 

## Assembly specific flags
ASMFLAGS = $(COMMON)
ASMFLAGS += $(CFLAGS)
ASMFLAGS += -x assembler-with-cpp -Wa,-gdwarf2

## Linker flags
LDFLAGS = $(COMMON)
LDFLAGS +=  -Wl,-Map=SignalZusatz.map


## Intel Hex file production flags
HEX_FLASH_FLAGS = -R .eeprom -R .fuse -R .lock -R .signature

HEX_EEPROM_FLAGS = -j .eeprom
HEX_EEPROM_FLAGS += --set-section-flags=.eeprom="alloc,load"
HEX_EEPROM_FLAGS += --change-section-lma .eeprom=0 --no-change-warnings


## Include Directories
INCLUDES = -I"D:\Daten\AvrProgs\TxP2\SignalZusatz\..\Gemeinsam" -I"D:\Daten\AvrProgs\TxP2\SignalZusatz\..\..\C-Libs" 

## Objects that must be built in order to link
OBJECTS = SignalZusatz.o BusKomm.o MsTimer.o 

## Objects explicitly added by the user
LINKONLYOBJECTS = 

## Build
all: $(TARGET) SignalZusatz.hex SignalZusatz.eep SignalZusatz.lss## Compile
SignalZusatz.o: ../SignalZusatz.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

BusKomm.o: ../../Gemeinsam/BusKomm.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

MsTimer.o: ../../../C-Libs/MsTimer.c
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
	-rm -rf $(OBJECTS) SignalZusatz.elf dep/* SignalZusatz.hex SignalZusatz.eep SignalZusatz.lss SignalZusatz.map


## Other dependencies
-include $(shell mkdir dep 2>NUL) $(wildcard dep/*)

