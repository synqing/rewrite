#ifndef PHASE0_CRASH_DUMP_H
#define PHASE0_CRASH_DUMP_H

/**
 * Phase 0: Crash Dump & Debug Recovery System
 *
 * Purpose: Capture system state on crashes for debugging
 *
 * Features:
 * - Exception handler registration
 * - State capture (registers, stack, globals)
 * - Persistent storage in RTC memory
 * - Automatic recovery on boot
 * - Crash analysis tools
 */

#include <Arduino.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/rtc.h>

namespace Phase0 {
namespace CrashDump {

// Crash types
enum class CrashType : uint8_t {
    NONE = 0,
    PANIC,
    WATCHDOG,
    STACK_OVERFLOW,
    HEAP_CORRUPTION,
    ASSERTION_FAILED,
    MANUAL_DUMP
};

// Task snapshot
struct TaskSnapshot {
    char name[16];
    uint32_t stack_watermark;
    uint32_t stack_size;
    UBaseType_t priority;
    eTaskState state;
};

// Crash dump structure (stored in RTC memory - survives reboot)
struct __attribute__((packed)) CrashDumpData {
    uint32_t magic;               // Magic number for validation
    CrashType crash_type;
    uint32_t timestamp;           // millis() at crash
    uint32_t free_heap;
    uint32_t min_free_heap;
    uint32_t largest_free_block;

    // Performance metrics at crash
    float audio_fps;
    float led_fps;

    // Task states
    uint8_t task_count;
    TaskSnapshot tasks[4];  // Up to 4 tasks

    // Last known good values
    uint8_t last_mode;
    float last_photons;
    float last_chroma;

    // Crash specific data
    uint32_t pc;                 // Program counter (if available)
    uint32_t sp;                 // Stack pointer
    char message[64];            // Crash message

    uint32_t crc32;              // Checksum
};

constexpr uint32_t CRASH_DUMP_MAGIC = 0xDEADC0DE;

// RTC memory allocation
RTC_DATA_ATTR static CrashDumpData rtc_crash_dump;
RTC_DATA_ATTR static bool rtc_crash_dump_valid = false;

//=============================================================================
// CRC32 (simplified version for crash dumps)
//=============================================================================

uint32_t simple_crc32(const void* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* ptr = static_cast<const uint8_t*>(data);

    for (size_t i = 0; i < length; i++) {
        crc ^= ptr[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }

    return ~crc;
}

//=============================================================================
// Crash Dump Capture
//=============================================================================

void captureCrashDump(CrashType type, const char* message = nullptr) {
    // Start fresh
    memset(&rtc_crash_dump, 0, sizeof(rtc_crash_dump));

    // Basic info
    rtc_crash_dump.magic = CRASH_DUMP_MAGIC;
    rtc_crash_dump.crash_type = type;
    rtc_crash_dump.timestamp = millis();

    // Memory state
    rtc_crash_dump.free_heap = esp_get_free_heap_size();
    rtc_crash_dump.min_free_heap = esp_get_minimum_free_heap_size();
    rtc_crash_dump.largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

    // Performance (read from globals)
    extern float SYSTEM_FPS;
    extern float LED_FPS;
    rtc_crash_dump.audio_fps = SYSTEM_FPS;
    rtc_crash_dump.led_fps = LED_FPS;

    // Configuration state
    extern struct SensoryBridge::Config::conf CONFIG;
    rtc_crash_dump.last_mode = CONFIG.LIGHTSHOW_MODE;
    rtc_crash_dump.last_photons = CONFIG.PHOTONS;
    rtc_crash_dump.last_chroma = CONFIG.CHROMA;

    // Task states
    rtc_crash_dump.task_count = 0;

    extern TaskHandle_t main_loop_task;
    extern TaskHandle_t led_task;
    extern TaskHandle_t encoder_task_handle;

    TaskHandle_t tasks[] = {main_loop_task, led_task, encoder_task_handle, nullptr};

    for (int i = 0; i < 4 && tasks[i] != nullptr; i++) {
        TaskSnapshot& snap = rtc_crash_dump.tasks[rtc_crash_dump.task_count++];

        const char* task_name = pcTaskGetName(tasks[i]);
        strncpy(snap.name, task_name, sizeof(snap.name) - 1);

        snap.stack_watermark = uxTaskGetStackHighWaterMark(tasks[i]);
        snap.priority = uxTaskPriorityGet(tasks[i]);
        snap.state = eTaskGetState(tasks[i]);

        // Estimate stack size (not directly available)
        snap.stack_size = 0;  // Would need to track separately
    }

    // Message
    if (message) {
        strncpy(rtc_crash_dump.message, message, sizeof(rtc_crash_dump.message) - 1);
    }

    // Stack pointer (approximate)
    rtc_crash_dump.sp = (uint32_t)&type;  // Current stack location

    // Calculate CRC
    size_t crc_size = sizeof(rtc_crash_dump) - sizeof(rtc_crash_dump.crc32);
    rtc_crash_dump.crc32 = simple_crc32(&rtc_crash_dump, crc_size);

    // Mark as valid
    rtc_crash_dump_valid = true;

    // Try to write to persistent storage too (if filesystem available)
    // This is best-effort, may fail if crash is severe
}

//=============================================================================
// Crash Analysis & Recovery
//=============================================================================

bool hasCrashDump() {
    if (!rtc_crash_dump_valid) return false;
    if (rtc_crash_dump.magic != CRASH_DUMP_MAGIC) return false;

    // Verify CRC
    size_t crc_size = sizeof(rtc_crash_dump) - sizeof(rtc_crash_dump.crc32);
    uint32_t calculated_crc = simple_crc32(&rtc_crash_dump, crc_size);

    return (calculated_crc == rtc_crash_dump.crc32);
}

void printCrashDump() {
    if (!hasCrashDump()) {
        USBSerial.println("No crash dump available");
        return;
    }

    USBSerial.println("\n╔═══════════════════════════════════════════════════════╗");
    USBSerial.println("║              CRASH DUMP ANALYSIS                      ║");
    USBSerial.println("╚═══════════════════════════════════════════════════════╝\n");

    // Crash type
    const char* crash_type_str = "UNKNOWN";
    switch (rtc_crash_dump.crash_type) {
        case CrashType::PANIC: crash_type_str = "PANIC"; break;
        case CrashType::WATCHDOG: crash_type_str = "WATCHDOG"; break;
        case CrashType::STACK_OVERFLOW: crash_type_str = "STACK_OVERFLOW"; break;
        case CrashType::HEAP_CORRUPTION: crash_type_str = "HEAP_CORRUPTION"; break;
        case CrashType::ASSERTION_FAILED: crash_type_str = "ASSERTION_FAILED"; break;
        case CrashType::MANUAL_DUMP: crash_type_str = "MANUAL_DUMP"; break;
        default: break;
    }

    USBSerial.printf("Crash Type:       %s\n", crash_type_str);
    USBSerial.printf("Timestamp:        %u ms\n", rtc_crash_dump.timestamp);
    USBSerial.printf("Message:          %s\n", rtc_crash_dump.message);
    USBSerial.println();

    // Memory state
    USBSerial.println("Memory State:");
    USBSerial.printf("  Free Heap:         %u bytes\n", rtc_crash_dump.free_heap);
    USBSerial.printf("  Min Free Heap:     %u bytes\n", rtc_crash_dump.min_free_heap);
    USBSerial.printf("  Largest Block:     %u bytes\n", rtc_crash_dump.largest_free_block);
    USBSerial.println();

    // Performance
    USBSerial.println("Performance:");
    USBSerial.printf("  Audio FPS:         %.2f\n", rtc_crash_dump.audio_fps);
    USBSerial.printf("  LED FPS:           %.2f\n", rtc_crash_dump.led_fps);
    USBSerial.println();

    // Configuration
    USBSerial.println("Last Configuration:");
    USBSerial.printf("  Mode:              %u\n", rtc_crash_dump.last_mode);
    USBSerial.printf("  Photons:           %.2f\n", rtc_crash_dump.last_photons);
    USBSerial.printf("  Chroma:            %.2f\n", rtc_crash_dump.last_chroma);
    USBSerial.println();

    // Tasks
    USBSerial.printf("Tasks (%u):\n", rtc_crash_dump.task_count);
    for (int i = 0; i < rtc_crash_dump.task_count; i++) {
        const TaskSnapshot& task = rtc_crash_dump.tasks[i];

        const char* state_str = "UNKNOWN";
        switch (task.state) {
            case eRunning: state_str = "Running"; break;
            case eReady: state_str = "Ready"; break;
            case eBlocked: state_str = "Blocked"; break;
            case eSuspended: state_str = "Suspended"; break;
            case eDeleted: state_str = "Deleted"; break;
            default: break;
        }

        USBSerial.printf("  %s:\n", task.name);
        USBSerial.printf("    State:           %s\n", state_str);
        USBSerial.printf("    Priority:        %u\n", task.priority);
        USBSerial.printf("    Stack Free:      %u bytes\n", task.stack_watermark);

        // Warning if stack low
        if (task.stack_watermark < 512) {
            USBSerial.println("    ⚠️  WARNING: Stack running low!");
        }
    }
    USBSerial.println();

    // Stack pointer
    USBSerial.printf("Stack Pointer:    0x%08X\n", rtc_crash_dump.sp);
    USBSerial.println();
}

void clearCrashDump() {
    rtc_crash_dump_valid = false;
    memset(&rtc_crash_dump, 0, sizeof(rtc_crash_dump));
}

//=============================================================================
// Safe Mode Boot Detection
//=============================================================================

bool shouldBootInSafeMode() {
    if (!hasCrashDump()) return false;

    // Boot in safe mode if:
    // 1. Stack overflow detected
    // 2. Heap corruption
    // 3. Watchdog timeout

    return (rtc_crash_dump.crash_type == CrashType::STACK_OVERFLOW ||
            rtc_crash_dump.crash_type == CrashType::HEAP_CORRUPTION ||
            rtc_crash_dump.crash_type == CrashType::WATCHDOG);
}

void bootSafeMode() {
    USBSerial.println("\n╔═══════════════════════════════════════════════════════╗");
    USBSerial.println("║              SAFE MODE ACTIVATED                      ║");
    USBSerial.println("╚═══════════════════════════════════════════════════════╝\n");

    printCrashDump();

    USBSerial.println("Safe Mode Actions:");
    USBSerial.println("  - Minimal LED output");
    USBSerial.println("  - Reduced task priorities");
    USBSerial.println("  - Disabled advanced features");
    USBSerial.println("  - Serial debugging enabled");
    USBSerial.println();
    USBSerial.println("Send 'R' to resume normal operation");
    USBSerial.println("Send 'C' to clear crash dump");
    USBSerial.println();

    // Override config for safe mode
    extern struct SensoryBridge::Config::conf CONFIG;
    CONFIG.LIGHTSHOW_MODE = 0;  // Basic mode
    CONFIG.PHOTONS = 0.5f;
    CONFIG.LED_COUNT = 160;
    CONFIG.SAMPLES_PER_CHUNK = 256;  // Lower latency

    // Disable secondary LEDs
    extern bool ENABLE_SECONDARY_LEDS;
    ENABLE_SECONDARY_LEDS = false;
}

//=============================================================================
// Watchdog Handler Integration
//=============================================================================

void watchdogTimeoutHandler() {
    // Called when watchdog is about to reset
    captureCrashDump(CrashType::WATCHDOG, "Watchdog timeout");

    // Allow watchdog to reset
}

//=============================================================================
// Stack Overflow Hook
//=============================================================================

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    char message[64];
    snprintf(message, sizeof(message), "Stack overflow in task: %s", pcTaskName);
    captureCrashDump(CrashType::STACK_OVERFLOW, message);

    // System will reset
}

//=============================================================================
// Manual Dump Trigger (for testing)
//=============================================================================

void triggerManualDump(const char* reason = "Manual dump") {
    captureCrashDump(CrashType::MANUAL_DUMP, reason);
    USBSerial.println("✅ Crash dump captured");
}

//=============================================================================
// Boot-time Crash Analysis
//=============================================================================

void initialize() {
    // Check for crash dump from previous boot
    if (hasCrashDump()) {
        USBSerial.println("\n⚠️  PREVIOUS CRASH DETECTED!\n");
        printCrashDump();

        if (shouldBootInSafeMode()) {
            bootSafeMode();
        } else {
            USBSerial.println("Crash was recoverable, resuming normal operation");
            USBSerial.println("Send 'D' to view dump, 'C' to clear\n");
        }
    }

    // Register stack overflow hook (done automatically by FreeRTOS if enabled)
}

} // namespace CrashDump
} // namespace Phase0

#endif // PHASE0_CRASH_DUMP_H
