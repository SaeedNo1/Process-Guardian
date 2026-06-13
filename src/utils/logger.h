#ifndef LOGGER_H
#define LOGGER_H

#include <windows.h>

typedef enum {
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR
} LogLevel;

// Initialize logger
void InitLogger(void);

// Log a message
void Log(LogLevel level, const wchar_t *format, ...);

// Close logger
void CloseLogger(void);

#endif