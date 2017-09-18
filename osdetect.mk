# osdetect.mk
#
# Guesses the current operating system and sets the PLATFORM variable to one of
# windows, linux

ifeq ($(PLATFORM),)
  ifeq ($(OS), Linux)
    PLATFORM := linux
  else ifeq ($(shell uname), Linux)
    PLATFORM := linux
  else ifeq ($(OS), Windows_NT)
    PLATFORM := windows
  else
    $(error Could not guess operating system. Please set the PLATFORM environment variable.)
  endif
endif