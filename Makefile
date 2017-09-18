
include osdetect.mk

CC := gcc
WINDRES := windres
SOURCES := src/config.c src/gameboy.c src/gpu.c src/memory.c
PROGRAM := gbemu
CFLAGS := -std=c11 -Wall -Wextra -pedantic -Werror=implicit -Wno-switch
LDFLAGS :=

ifeq ($(BUILD), release)
  CFLAGS += -s -Ofast -DNDEBUG
else
  CFLAGS += -g -O0 -DDEBUG -fno-inline
endif

ifeq ($(PLATFORM), windows)
  LDFLAGS += -lmingw32
endif

# Set default frontend based on OS
ifeq ($(FRONTEND),)
  ifeq ($(PLATFORM), windows)
    FRONTEND := windows
  else ifeq ($(PLATFORM), linux)
    FRONTEND := gtk2
  endif
endif

# Frontends
ifeq ($(FRONTEND), windows)
  WINRSRC := src/platform/winrsrc.o
  SOURCES += src/platform/windows.c
  CFLAGS += -DFRONTEND_WINDOWS
  LDFLAGS += -lcomctl32 -lcomdlg32 -lgdi32 -lwinmm
  ifeq ($(BUILD), release)
    LDFLAGS += -mwindows
  endif
else ifeq ($(FRONTEND), sdl1)
  SOURCES += src/platform/sdl1.c
  CFLAGS += -DFRONTEND_SDL1
  LDFLAGS += -lSDLmain -lSDL
else ifeq ($(FRONTEND), sdl2)
  SOURCES += src/platform/sdl2.c
  CFLAGS += -DFRONTEND_SDL2
  LDFLAGS += -lSDL2main -lSDL2
else ifeq ($(FRONTEND), gtk2)
  SOURCES += src/platform/gtk2.c
  CFLAGS += -DFRONTEND_GTK2 $(shell pkg-config --cflags gtk+-2.0)
  LDFLAGS += $(shell pkg-config --libs gtk+-2.0)
else
  $(error Unknown frontend $(FRONTEND))
endif


# Main build rules

$(PROGRAM): $(SOURCES) $(WINRSRC)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

%.o: %.rc
	$(WINDRES) $^ -o $@

clean:
	$(RM) $(PROGRAM) $(PROGRAM).exe
