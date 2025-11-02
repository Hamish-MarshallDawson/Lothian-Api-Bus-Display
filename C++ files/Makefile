# Makefile for Bus Tracker with Display

# Directories
DIR_C_LIB = ../C/lib
DIR_LCD = $(DIR_C_LIB)/lcd
DIR_GUI = $(DIR_C_LIB)/GUI
DIR_CONFIG = $(DIR_C_LIB)/Config
DIR_FONTS = $(DIR_C_LIB)/Fonts

# Source files
C_SOURCES = $(DIR_LCD)/st7796.c \
            $(DIR_LCD)/ft6336u.c \
            $(DIR_GUI)/GUI_Paint.c \
            $(DIR_CONFIG)/DEV_Config.c \
            $(DIR_FONTS)/font8.c \
            $(DIR_FONTS)/font12.c \
            $(DIR_FONTS)/font16.c \
            $(DIR_FONTS)/font20.c \
            $(DIR_FONTS)/font24.c \
            $(DIR_FONTS)/font48.c \
            $(DIR_FONTS)/font50.c

CPP_SOURCES = busTrack.cpp openmappull.cpp
#CPP_SOURCES = displayTest.cpp

# Compiler settings
CC = gcc
CXX = g++
CFLAGS = -Wall -O2 -I$(DIR_CONFIG) -I$(DIR_LCD) -I$(DIR_GUI) -I$(DIR_FONTS) -DUSE_WIRINGPI_LIB
CXXFLAGS = -Wall -O2 -std=c++11 -I$(DIR_CONFIG) -I$(DIR_LCD) -I$(DIR_GUI) -I$(DIR_FONTS)
LDFLAGS = -lwiringPi -lcurl -lm

# Output
# default target builds both helper and main display app
TARGET = busTrack

# Build
all: $(TARGET) openmappull

# Build busTrack (links C device libs + wiringPi)
$(TARGET): $(C_SOURCES:.c=.o) busTrack.o openmappull.o busStopInfo.o config_utils.o
	$(CXX) $^ -o $@ $(LDFLAGS)

# Build openmappull (standalone helper) - link the implementation with the CLI main and config utils
openmappull: openmappull.o openmappull_main.o config_utils.o
	$(CXX) $^ -o $@ -lcurl -lm

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) openmappull *.o $(DIR_LCD)/*.o $(DIR_GUI)/*.o $(DIR_CONFIG)/*.o $(DIR_FONTS)/*.o

.PHONY: all clean run run-openmappull

# Convenience run targets
run: $(TARGET)
	./$(TARGET)

run-openmappull: openmappull
	./openmappull