#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <stdarg.h>
#include <string.h>

// Include USB CDC configuration
#include "usb_cdc_config.h"

// Define USBSerial for ESP32-S3
#if defined(ARDUINO_ESP32S3_DEV) && !defined(USBSerial)
  #define USBSerial Serial
#endif

// Log levels
typedef enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_VERBOSE
} LogLevel;

// ANSI color codes
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

// Helper macros for easier logging
#define LOG_ERROR(fmt, ...)   Logger::error(fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)    Logger::warn(fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)    Logger::info(fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...)   Logger::debug(fmt, ##__VA_ARGS__)
#define LOG_VERBOSE(fmt, ...) Logger::verbose(fmt, ##__VA_ARGS__)

class Logger {
public:
    static void begin(LogLevel level = LOG_LEVEL_INFO, bool color = true, bool timestamp = true) {
        logLevel = level;
        useColor = color;
        useTimestamp = timestamp;
        
        // Initialize serial port
        #if defined(ARDUINO_ESP32S3_DEV) && defined(ARDUINO_ARCH_ESP32)
            // USB CDC is initialized in usb_cdc_init()
            USBSerial.setDebugOutput(true);
            #ifdef DEBUG
                while (!USBSerial) {
                    delay(10);
                }
            #endif
        #else
            // Fallback for other platforms
            Serial.begin(115200);
            #ifdef DEBUG
                while (!Serial);
            #endif
        #endif
        
        LOG_INFO("Logger initialized with level: %d", logLevel);
    }
    
    static void setLogLevel(LogLevel level) {
        logLevel = level;
    }
    
    static void setColorEnabled(bool enabled) {
        useColor = enabled;
    }
    
    static void setTimestampEnabled(bool enabled) {
        useTimestamp = enabled;
    }
    
    // Log methods
    static void error(const char* format, ...) {
        va_list args;
        va_start(args, format);
        log(LOG_LEVEL_ERROR, "ERROR", ANSI_COLOR_RED, format, args);
        va_end(args);
    }
    
    static void warn(const char* format, ...) {
        if (logLevel < LOG_LEVEL_WARN) return;
        va_list args;
        va_start(args, format);
        log(LOG_LEVEL_WARN, "WARN ", ANSI_COLOR_YELLOW, format, args);
        va_end(args);
    }
    
    static void info(const char* format, ...) {
        if (logLevel < LOG_LEVEL_INFO) return;
        va_list args;
        va_start(args, format);
        log(LOG_LEVEL_INFO, "INFO ", ANSI_COLOR_GREEN, format, args);
        va_end(args);
    }
    
    static void debug(const char* format, ...) {
        if (logLevel < LOG_LEVEL_DEBUG) return;
        va_list args;
        va_start(args, format);
        log(LOG_LEVEL_DEBUG, "DEBUG", ANSI_COLOR_CYAN, format, args);
        va_end(args);
    }
    
    static void verbose(const char* format, ...) {
        if (logLevel < LOG_LEVEL_VERBOSE) return;
        va_list args;
        va_start(args, format);
        log(LOG_LEVEL_VERBOSE, "VERB ", ANSI_COLOR_MAGENTA, format, args);
        va_end(args);
    }
    
    // Performance monitoring
    static void logPerformance(const char* tag, uint32_t timeMs) {
        if (logLevel >= LOG_LEVEL_DEBUG) {
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "[PERF] %-20s: %5lu ms", tag, timeMs);
            debug(buffer);
        }
    }
    
    // Memory usage
    static void logMemoryUsage() {
        if (logLevel >= LOG_LEVEL_DEBUG) {
            debug("Free heap: %d bytes", ESP.getFreeHeap());
            debug("Min free heap: %d bytes", ESP.getMinFreeHeap());
            debug("Max alloc heap: %d bytes", ESP.getMaxAllocHeap());
        }
    }
    
    // Task status
    static void logTaskStatus() {
        if (logLevel >= LOG_LEVEL_VERBOSE) {
            verbose("Task Status:");
            verbose("  Name       State   Pri  Free   Stack   Core");
            verbose("  --------- ------- ----- ------- ------- ----");
            
            char *pbuffer = (char*)malloc(1024);
            if (pbuffer) {
                vTaskList(pbuffer);
                verbose("%s", pbuffer);
                free(pbuffer);
            }
        }
    }

private:
    static LogLevel logLevel;
    static bool useColor;
    static bool useTimestamp;
    
    static void log(LogLevel level, const char* tag, const char* color, const char* format, va_list args) {
        if (level > logLevel) return;
        
        char buffer[384];  // Increased buffer size for safety
        vsnprintf(buffer, sizeof(buffer), format, args);
        
        // Get task name if available
        const char* taskName = "";
        #ifdef ESP32
        taskName = pcTaskGetName(NULL);
        if (taskName == NULL) {
            taskName = "main";
        }
        #endif
        
        #if defined(ARDUINO_ESP32S3_DEV) && defined(ARDUINO_ARCH_ESP32)
            // Use USBSerial for ESP32-S3
            Stream &output = USBSerial;
        #else
            // Fallback to Serial for other platforms
            Stream &output = Serial;
        #endif
            
            if (useColor) {
                output.print(color);
            }
            
            if (useTimestamp) {
                unsigned long ms = millis();
                output.printf("[%6lu.%03lu] ", ms / 1000, ms % 1000);
            }
            
            output.printf("[%s] [%-10s] %s\n", tag, taskName, buffer);
            
            if (useColor) {
                output.print(ANSI_COLOR_RESET);
            }
            
            output.flush();
            if (useColor) {
                Serial.print(color);
            }
            
            if (useTimestamp) {
                unsigned long ms = millis();
                Serial.printf("[%6lu.%03lu] ", ms / 1000, ms % 1000);
            }
            
            Serial.printf("[%s] [%-10s] %s\n", tag, taskName, buffer);
            
            if (useColor) {
                Serial.print(ANSI_COLOR_RESET);
            }
            
            Serial.flush();
        #endif
    }
};

// Initialize static members
LogLevel Logger::logLevel = LOG_LEVEL_INFO;
bool Logger::useColor = true;
bool Logger::useTimestamp = true;

#endif // LOGGER_H
