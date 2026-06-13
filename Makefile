# Process Guardian Makefile
CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lcomctl32 -lpsapi -lshell32 -luser32 -lkernel32

TARGET = process_guardian.exe
SRC = main.c src/gui/gui.c src/core/process_monitor.c src/core/process_protector.c src/utils/logger.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean