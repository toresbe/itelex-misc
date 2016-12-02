###############################################################################
# Makefile for the project UniversalIF
###############################################################################

## General Flags
PROJECT = UniversalIF
MCU = atmega168
TARGET = UniversalIF.elf
CC = avr-gcc

CPP = avr-g++

## Options common to compile, link and assembly rules
COMMON = -mmcu=$(MCU)

## Compile options common for all C compilation units.
CFLAGS = $(COMMON)
CFLAGS += -Wall -gdwarf-2 -std=gnu99 -DRS232 -DHEX_FORMAT -DF_CPU=6144000UL -O2 -funsigned-char -funsigned-bitfields -fpack-struct -fshort-enums  -DPROGIDZUSATZ=\"RS232-Hex\" 
CFLAGS += -MD -MP -MT $(*F).o -MF dep/$(@F).d 

## Assembly specific flags
ASMFLAGS = $(COMMON)
ASMFLAGS += $(CFLAGS)
ASMFLAGS += -x assembler-with-cpp -Wa,-gdwarf2

## Linker flags
LDFLAGS = $(COMMON)
LDFLAGS +=  -Wl,-Map=UniversalIF.map


## Intel Hex file production flags
HEX_FLASH_FLAGS = -R .eeprom -R .fuse -R .lock -R .signature

HEX_EEPROM_FLAGS = -j .eeprom
HEX_EEPROM_FLAGS += --set-section-flags=.eeprom="alloc,load"
HEX_EEPROM_FLAGS += --change-section-lma .eeprom=0 --no-change-warnings


## Include Directories
INCLUDES = -I"D:\Daten\AvrProgs\TxP2\UniversalIF\..\Gemeinsam" -I"D:\Daten\AvrProgs\TxP2\UniversalIF\..\..\C-Libs" 

## Objects that must be built in order to link
OBJECTS = UniversalIF.o ClientCommunication.o BusKomm.o FifoPuffer.o SeriellUmsetz.o MsTimer.o 

## Objects explicitly added by the user
LINKONLYOBJECTS = 

## Build
all: $(TARGET) UniversalIF.hex UniversalIF.eep UniversalIF.lss## Compile
UniversalIF.o: ../UniversalIF.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

ClientCommunication.o: ../ClientCommunication.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

BusKomm.o: ../../Gemeinsam/BusKomm.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

FifoPuffer.o: ../../Gemeinsam/FifoPuffer.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

SeriellUmsetz.o: ../../Gemeinsam/SeriellUmsetz.c
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
	-rm -rf $(OBJECTS) UniversalIF.elf dep/* UniversalIF.hex UniversalIF.eep UniversalIF.lss UniversalIF.map


## Other dependencies
-include $(shell mkdir dep 2>NUL) $(wildcard dep/*)

