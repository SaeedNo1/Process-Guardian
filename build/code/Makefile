# Process Guardian Makefile
CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lcomctl32 -lpsapi -lshell32 -luser32 -lkernel32 -ladvapi32 -lsetupapi

TARGET = process_guardian.exe
SRC = main.c src/core/process_monitor.c src/core/process_protector.c src/core/service_manager.c src/core/registry_manager.c src/core/partition_manager.c src/utils/logger.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# DLLs
service_manager.dll: src/core/service_manager.c
	$(CC) $(CFLAGS) -DBUILD_SERVICE_MANAGER_DLL -shared -o $@ $^ $(LDFLAGS)

registry_manager.dll: src/core/registry_manager.c
	$(CC) $(CFLAGS) -DBUILD_REGISTRY_MANAGER_DLL -shared -o $@ $^ $(LDFLAGS)

partition_manager.dll: src/core/partition_manager.c
	$(CC) $(CFLAGS) -DBUILD_PARTITION_MANAGER_DLL -shared -o $@ $^ $(LDFLAGS)

dlls: service_manager.dll registry_manager.dll partition_manager.dll

clean:
	rm -f $(TARGET) *.dll

.PHONY: all clean dlls
