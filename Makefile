# TrueAudioRand - Makefile
# Backend audio: PortAudio (cross-platform)

CC       ?= gcc
CFLAGS   ?= -Wall -Wextra -O2 -std=c11
LDFLAGS  ?=

LIB_SRCS  = trueaudiorand.c sha256.c
LIB_OBJS  = $(LIB_SRCS:.c=.o)
LIBRARY   = libtrueaudiorand.a

CLI_SRCS  = main_test.c
CLI_OBJS  = $(CLI_SRCS:.c=.o)
CLI_TARGET = main_test

GUI_SRCS  = main_gui.c
GUI_OBJS  = $(GUI_SRCS:.c=.o)
GUI_TARGET = TrueAudioRand.exe

UNAME_S := $(shell uname -s 2>/dev/null || echo Unknown)

ifeq ($(UNAME_S),Darwin)
    PA_CFLAGS ?= $(shell pkg-config --cflags portaudio-2.0 2>/dev/null)
    PA_LIBS   ?= $(shell pkg-config --libs portaudio-2.0 2>/dev/null)
    ifeq ($(PA_LIBS),)
        PA_CFLAGS ?= -I/opt/homebrew/include -I/usr/local/include
        PA_LIBS   ?= -L/opt/homebrew/lib -L/usr/local/lib -lportaudio
    endif
else ifeq ($(UNAME_S),Linux)
    PA_CFLAGS ?= $(shell pkg-config --cflags portaudio-2.0 2>/dev/null)
    PA_LIBS   ?= $(shell pkg-config --libs portaudio-2.0 2>/dev/null)
    ifeq ($(PA_LIBS),)
        PA_LIBS = -lportaudio -lpthread -lm
    endif
else
    CC        ?= gcc
    PA_CFLAGS ?= -I/mingw64/include
    PA_LIBS   ?= -L/mingw64/lib -lportaudio -lwinmm -lole32 -luuid
    WIN32_GUI  = 1
endif

CFLAGS  += $(PA_CFLAGS)
LDFLAGS += $(PA_LIBS)

WIN32_LDFLAGS = -mwindows -municode -lcomctl32 -lgdi32 -luser32 -lkernel32 -lshell32

.PHONY: all clean run analyze demo process gui cli

all: cli
ifdef WIN32_GUI
all: gui
endif

cli: $(CLI_TARGET)

gui: $(GUI_TARGET)

$(LIBRARY): $(LIB_OBJS)
	$(AR) rcs $@ $^

$(CLI_TARGET): $(CLI_OBJS) $(LIBRARY)
	$(CC) $(CFLAGS) -o $@ $(CLI_OBJS) $(LIBRARY) $(LDFLAGS)

$(GUI_TARGET): $(GUI_OBJS) $(LIBRARY)
	$(CC) $(CFLAGS) -o $@ $(GUI_OBJS) $(LIBRARY) $(LDFLAGS) $(WIN32_LDFLAGS)

%.o: %.c trueaudiorand.h sha256.h
	$(CC) $(CFLAGS) -c $< -o $@

demo: $(CLI_TARGET)
	./$(CLI_TARGET) --demo

process:
	powershell -NoProfile -ExecutionPolicy Bypass -File run_process.ps1

run: $(CLI_TARGET)
	./$(CLI_TARGET)

analyze: random_samples.txt
	python verify_rand.py random_samples.txt

clean:
	rm -f $(LIB_OBJS) $(CLI_OBJS) $(GUI_OBJS) $(LIBRARY) $(CLI_TARGET) $(GUI_TARGET)
