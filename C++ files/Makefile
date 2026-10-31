# Makefile for the Lothian bus display

# Directories
DIR_C_LIB = ../C/lib
DIR_LCD = $(DIR_C_LIB)/lcd
DIR_CONFIG = $(DIR_C_LIB)/Config
DIR_GFX = ../gfx

# The LCD and touch drivers need wiringPi. If its header is missing, busTrack
# still builds without a display: --snapshot and --fetch work, the display loop
# is left out.
WIRINGPI_H := $(wildcard /usr/include/wiringPi.h /usr/local/include/wiringPi.h)
ifneq ($(WIRINGPI_H),)
  HW_CFLAGS   := -DUSE_WIRINGPI_LIB
  HW_CXXFLAGS := -DBUSTRACKER_HW
  HW_SOURCES  := $(DIR_LCD)/st7796.c $(DIR_LCD)/ft6336u.c $(DIR_CONFIG)/DEV_Config.c
  HW_LIBS     := -lwiringPi
else
  $(info note: wiringPi not found, building busTrack without display support)
endif

# Source files
C_SOURCES = $(HW_SOURCES)

# Anti-aliased canvas, baked fonts and widgets (gfx/), shared with lbd-ui-board
GFX_SOURCES = $(DIR_GFX)/canvas.cpp $(DIR_GFX)/text.cpp $(DIR_GFX)/widgets.cpp

# Library code shared by busTrack and the tests
LIB_SOURCES = departures.cpp timetables.cpp direction.cpp diskcache.cpp openmappull.cpp refresh.cpp \
              png_write.cpp renderDisplay.cpp screens.cpp screens_draw.cpp $(GFX_SOURCES)
CPP_SOURCES = busTrack.cpp $(LIB_SOURCES)

# Compiler settings
CC = gcc
CXX = g++
CFLAGS = -Wall -O2 -MMD -MP -I$(DIR_CONFIG) -I$(DIR_LCD) $(HW_CFLAGS)
CXXFLAGS = -Wall -O2 -MMD -MP -std=c++17 -I$(DIR_CONFIG) -I$(DIR_LCD) $(HW_CXXFLAGS)

# Output
TARGET = busTrack
TEST_SCREENS = test_screens
TESTS = tests/refresh_test tests/direction_test tests/live_format_test $(TEST_SCREENS)

all: $(TARGET)

# busTrack: display (if wiringPi), snapshot PNG, and one-stop fetch
$(TARGET): $(C_SOURCES:.c=.o) $(CPP_SOURCES:.cpp=.o)
	$(CXX) $^ -o $@ $(HW_LIBS) -lcurl -lz -lm

# Logic checks that need no display or network. Run on the Pi from this folder: make test
tests/refresh_test: tests/refresh_test.o refresh.o departures.o
	$(CXX) $^ -o $@ -lcurl -lm

tests/direction_test: tests/direction_test.o direction.o timetables.o diskcache.o
	$(CXX) $^ -o $@ -lcurl -lm

tests/live_format_test: tests/live_format_test.o departures.o timetables.o diskcache.o
	$(CXX) $^ -o $@ -lcurl -lm

# Screen state, transitions and touch hit areas (screens.cpp, no drawing)
$(TEST_SCREENS): test_screens.cpp screens.cpp screens.h departures.h
	$(CXX) -Wall -Wextra -O1 -std=c++17 test_screens.cpp screens.cpp -o $@

test: $(TESTS)
	./tests/refresh_test
	./tests/direction_test
	./tests/live_format_test
	./$(TEST_SCREENS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(TESTS) *.o *.d tests/*.o tests/*.d $(DIR_GFX)/*.o $(DIR_GFX)/*.d \
	      $(DIR_LCD)/*.o $(DIR_CONFIG)/*.o $(DIR_LCD)/*.d $(DIR_CONFIG)/*.d

.PHONY: all test clean

-include $(wildcard *.d tests/*.d $(DIR_GFX)/*.d)
