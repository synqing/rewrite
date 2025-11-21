#ifndef PHASE0_WATCHDOG_HAL_H
#define PHASE0_WATCHDOG_HAL_H

/**
 * Phase 0: Watchdog Management HAL
 *
 * Purpose: Abstract ESP32 watchdog API for safer task management
 *
 * Features:
 * - Task watchdog timer (TWDT) management
 * - Per-task registration/deregistration
 * - Configurable timeout periods
 * - Integration with crash dump system
 * - Safe reset handling
 *
 * Usage:
 *   Watchdog::init(5000);  // 5 second timeout
 *   Watchdog::subscribe();  // Current task
 *   Watchdog::feed();       // Reset timer
 *   Watchdog::unsubscribe();
 */

#include <Arduino.h>
#include <esp_task_wdt.h>
#include "phase0_crash_dump.h"

namespace Phase0 {
namespace Watchdog {

// Watchdog configuration
struct Config {
    uint32_t timeout_ms = 5000;     // Default 5 second timeout
    bool panic_on_timeout = false;  // If true, panic instead of reset
    bool enable_interrupt = true;   // Enable TWDT interrupt
};

// Global config
static Config config;
static bool initialized = false;

//=============================================================================
// Initialization
//=============================================================================

/**
 * Initialize watchdog timer with specified timeout
 *
 * @param timeout_ms Timeout in milliseconds (default: 5000ms)
 * @param panic_on_timeout If true, triggers panic instead of reset
 * @return true if successful
 */
bool init(uint32_t timeout_ms = 5000, bool panic_on_timeout = false) {
    if (initialized) {
        USBSerial.println("WARNING: Watchdog already initialized");
        return true;
    }

    config.timeout_ms = timeout_ms;
    config.panic_on_timeout = panic_on_timeout;

    // Configure watchdog
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = timeout_ms,
        .idle_core_mask = 0,  // Don't watch idle tasks (we handle this manually)
        .trigger_panic = panic_on_timeout
    };

    esp_err_t err = esp_task_wdt_init(&wdt_config);
    if (err != ESP_OK) {
        USBSerial.printf("ERROR: Watchdog init failed: %d\n", err);
        return false;
    }

    initialized = true;
    USBSerial.printf("Watchdog initialized: %u ms timeout\n", timeout_ms);
    return true;
}

/**
 * Deinitialize watchdog timer
 */
void deinit() {
    if (!initialized) return;

    esp_task_wdt_deinit();
    initialized = false;
    USBSerial.println("Watchdog deinitialized");
}

//=============================================================================
// Task Management
//=============================================================================

/**
 * Subscribe current task to watchdog monitoring
 *
 * @return true if successful
 */
bool subscribe() {
    if (!initialized) {
        USBSerial.println("ERROR: Watchdog not initialized");
        return false;
    }

    esp_err_t err = esp_task_wdt_add(NULL);  // NULL = current task
    if (err == ESP_OK) {
        const char* task_name = pcTaskGetName(NULL);
        USBSerial.printf("Task '%s' subscribed to watchdog\n", task_name);
        return true;
    } else if (err == ESP_ERR_INVALID_STATE) {
        // Task already subscribed, this is fine
        return true;
    } else {
        USBSerial.printf("ERROR: Failed to subscribe task: %d\n", err);
        return false;
    }
}

/**
 * Subscribe specific task to watchdog monitoring
 *
 * @param task Task handle to subscribe
 * @return true if successful
 */
bool subscribe(TaskHandle_t task) {
    if (!initialized) {
        USBSerial.println("ERROR: Watchdog not initialized");
        return false;
    }

    if (task == NULL) {
        return subscribe();  // Subscribe current task
    }

    esp_err_t err = esp_task_wdt_add(task);
    if (err == ESP_OK) {
        const char* task_name = pcTaskGetName(task);
        USBSerial.printf("Task '%s' subscribed to watchdog\n", task_name);
        return true;
    } else if (err == ESP_ERR_INVALID_STATE) {
        return true;  // Already subscribed
    } else {
        USBSerial.printf("ERROR: Failed to subscribe task: %d\n", err);
        return false;
    }
}

/**
 * Unsubscribe current task from watchdog monitoring
 *
 * @return true if successful
 */
bool unsubscribe() {
    if (!initialized) return false;

    esp_err_t err = esp_task_wdt_delete(NULL);
    if (err == ESP_OK || err == ESP_ERR_NOT_FOUND) {
        return true;
    }

    USBSerial.printf("ERROR: Failed to unsubscribe task: %d\n", err);
    return false;
}

/**
 * Unsubscribe specific task from watchdog monitoring
 *
 * @param task Task handle to unsubscribe
 * @return true if successful
 */
bool unsubscribe(TaskHandle_t task) {
    if (!initialized) return false;

    if (task == NULL) {
        return unsubscribe();
    }

    esp_err_t err = esp_task_wdt_delete(task);
    if (err == ESP_OK || err == ESP_ERR_NOT_FOUND) {
        return true;
    }

    USBSerial.printf("ERROR: Failed to unsubscribe task: %d\n", err);
    return false;
}

//=============================================================================
// Feeding
//=============================================================================

/**
 * Feed (reset) the watchdog timer for current task
 *
 * This must be called periodically (more frequently than timeout)
 * to prevent watchdog reset.
 *
 * @return true if successful
 */
bool feed() {
    if (!initialized) return false;

    esp_err_t err = esp_task_wdt_reset();
    return (err == ESP_OK);
}

/**
 * Feed watchdog for specific task
 *
 * @param task Task handle to feed watchdog for
 * @return true if successful
 */
bool feed(TaskHandle_t task) {
    if (!initialized) return false;

    if (task == NULL) {
        return feed();
    }

    // Note: ESP32 API doesn't support per-task reset
    // This just resets the global watchdog
    return feed();
}

//=============================================================================
// Status
//=============================================================================

/**
 * Check if watchdog is initialized
 */
bool isInitialized() {
    return initialized;
}

/**
 * Get current timeout in milliseconds
 */
uint32_t getTimeout() {
    return config.timeout_ms;
}

/**
 * Check if task is subscribed to watchdog
 *
 * Note: ESP32 doesn't provide a direct API for this,
 * so this is a best-effort check
 */
bool isSubscribed(TaskHandle_t task = NULL) {
    if (!initialized) return false;

    // Try to delete and re-add to check subscription status
    // This is a workaround for lack of query API
    esp_err_t err = esp_task_wdt_delete(task);
    if (err == ESP_ERR_NOT_FOUND) {
        return false;  // Was not subscribed
    }

    // Re-subscribe if it was subscribed
    if (err == ESP_OK) {
        esp_task_wdt_add(task);
        return true;
    }

    return false;
}

//=============================================================================
// Integration with Crash Dump System
//=============================================================================

/**
 * Watchdog timeout handler (called from interrupt)
 *
 * This should be registered as a callback if custom handling is needed.
 * By default, ESP32 will reset on watchdog timeout.
 */
void timeoutHandler() {
    // Capture crash dump before reset
    CrashDump::captureCrashDump(
        CrashDump::CrashType::WATCHDOG,
        "Watchdog timeout"
    );

    // Note: System will reset after this
    // Safe mode will be activated on next boot if crash dump is valid
}

/**
 * Register watchdog timeout callback
 *
 * This integrates with the crash dump system to capture state
 * before watchdog reset.
 */
void registerTimeoutCallback() {
    // Note: ESP-IDF doesn't provide a direct callback mechanism
    // for task watchdog timeout. The integration happens through
    // the panic handler which calls CrashDump::watchdogTimeoutHandler()

    // For now, we rely on the panic handler integration
    USBSerial.println("Watchdog timeout handler registered via panic handler");
}

//=============================================================================
// Utility Functions
//=============================================================================

/**
 * Print watchdog status
 */
void printStatus() {
    USBSerial.println("\n╔═══════════════════════════════════════╗");
    USBSerial.println("║   WATCHDOG STATUS                     ║");
    USBSerial.println("╚═══════════════════════════════════════╝");
    USBSerial.printf("  Initialized:      %s\n", initialized ? "YES" : "NO");
    if (initialized) {
        USBSerial.printf("  Timeout:          %u ms\n", config.timeout_ms);
        USBSerial.printf("  Panic on timeout: %s\n", config.panic_on_timeout ? "YES" : "NO");
    }
    USBSerial.println();
}

/**
 * Helper: Disable watchdog for idle tasks
 *
 * This is useful when tasks monopolize a core and prevent
 * idle tasks from running.
 */
void disableIdleTasks() {
    for (int cpu = 0; cpu < portNUM_PROCESSORS; cpu++) {
        TaskHandle_t idle = xTaskGetIdleTaskHandleForCPU(cpu);
        if (idle != nullptr) {
            unsubscribe(idle);
        }
    }
    USBSerial.println("Idle tasks unsubscribed from watchdog");
}

//=============================================================================
// RAII Helper Class
//=============================================================================

/**
 * RAII helper for automatic watchdog subscription/unsubscription
 *
 * Usage:
 *   void myTask(void* param) {
 *       Watchdog::Guard guard;  // Auto-subscribe on construction
 *
 *       while (true) {
 *           // Do work
 *           guard.feed();  // Feed watchdog
 *           vTaskDelay(100);
 *       }
 *
 *       // Auto-unsubscribe on destruction
 *   }
 */
class Guard {
private:
    bool subscribed = false;

public:
    Guard() {
        subscribed = subscribe();
    }

    ~Guard() {
        if (subscribed) {
            unsubscribe();
        }
    }

    void feed() {
        if (subscribed) {
            Phase0::Watchdog::feed();
        }
    }

    // Disable copy
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};

} // namespace Watchdog
} // namespace Phase0

//=============================================================================
// Integration Macros
//=============================================================================

// Convenience macros for common patterns
#define WATCHDOG_INIT(timeout_ms) Phase0::Watchdog::init(timeout_ms)
#define WATCHDOG_SUBSCRIBE() Phase0::Watchdog::subscribe()
#define WATCHDOG_FEED() Phase0::Watchdog::feed()
#define WATCHDOG_UNSUBSCRIBE() Phase0::Watchdog::unsubscribe()

// RAII guard for automatic subscription
#define WATCHDOG_GUARD() Phase0::Watchdog::Guard __watchdog_guard

#endif // PHASE0_WATCHDOG_HAL_H
