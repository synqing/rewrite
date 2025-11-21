#ifndef PERFORMANCE_REGRESSION_SUITE_H
#define PERFORMANCE_REGRESSION_SUITE_H

/**
 * Performance Regression Test Suite
 *
 * CRITICAL: Must pass at end of each migration phase
 *
 * Purpose: Ensure real-time constraints maintained during refactoring
 * - Audio processing: 120+ FPS (8.3ms max per frame)
 * - LED rendering: 60+ FPS (16.7ms max per frame)
 * - Total latency: <10ms audio-to-light
 *
 * Usage:
 *   PerformanceTest::runAll();  // Returns true if all pass
 */

#include <Arduino.h>
#include <functional>

namespace PerformanceTest {

// Test result structure
struct TestResult {
    const char* name;
    bool passed;
    float measured_value;
    float target_value;
    const char* units;
    const char* failure_reason;
};

// Performance targets (from requirements)
constexpr float TARGET_AUDIO_FPS = 120.0f;
constexpr float TARGET_LED_FPS = 60.0f;
constexpr float TARGET_MAX_LATENCY_MS = 10.0f;
constexpr float TARGET_MIN_FREE_HEAP = 50000.0f;  // 50KB minimum
constexpr float TARGET_MAX_GDFT_TIME_US = 5000.0f;  // 5ms max for GDFT

// Tolerance (5% margin)
constexpr float FPS_TOLERANCE = 0.95f;  // 95% of target

//=============================================================================
// Test 1: Audio Processing Frame Rate
//=============================================================================

TestResult test_audio_processing_fps() {
    TestResult result = {
        "Audio Processing FPS",
        false,
        0.0f,
        TARGET_AUDIO_FPS,
        "FPS",
        nullptr
    };

    extern float SYSTEM_FPS;  // From globals.h

    // Measure over 1 second
    uint32_t start_time = millis();
    float fps_sum = 0.0f;
    uint32_t sample_count = 0;

    while (millis() - start_time < 1000) {
        fps_sum += SYSTEM_FPS;
        sample_count++;
        delay(10);
    }

    float avg_fps = sample_count > 0 ? fps_sum / sample_count : 0.0f;
    result.measured_value = avg_fps;

    if (avg_fps >= TARGET_AUDIO_FPS * FPS_TOLERANCE) {
        result.passed = true;
    } else {
        result.failure_reason = "Audio FPS below target";
    }

    return result;
}

//=============================================================================
// Test 2: LED Rendering Frame Rate
//=============================================================================

TestResult test_led_rendering_fps() {
    TestResult result = {
        "LED Rendering FPS",
        false,
        0.0f,
        TARGET_LED_FPS,
        "FPS",
        nullptr
    };

    extern float LED_FPS;  // From globals.h

    // Measure over 1 second
    uint32_t start_time = millis();
    float fps_sum = 0.0f;
    uint32_t sample_count = 0;

    while (millis() - start_time < 1000) {
        fps_sum += LED_FPS;
        sample_count++;
        delay(10);
    }

    float avg_fps = sample_count > 0 ? fps_sum / sample_count : 0.0f;
    result.measured_value = avg_fps;

    if (avg_fps >= TARGET_LED_FPS * FPS_TOLERANCE) {
        result.passed = true;
    } else {
        result.failure_reason = "LED FPS below target";
    }

    return result;
}

//=============================================================================
// Test 3: GDFT Processing Time
//=============================================================================

TestResult test_gdft_processing_time() {
    TestResult result = {
        "GDFT Processing Time",
        false,
        0.0f,
        TARGET_MAX_GDFT_TIME_US,
        "microseconds",
        nullptr
    };

    // Measure actual GDFT execution time
    uint32_t start_us = micros();

    // Call actual GDFT function
    extern void process_GDFT();  // From GDFT.h
    process_GDFT();

    uint32_t end_us = micros();
    float execution_time = end_us - start_us;

    result.measured_value = execution_time;

    if (execution_time <= TARGET_MAX_GDFT_TIME_US) {
        result.passed = true;
    } else {
        result.failure_reason = "GDFT took too long";
    }

    return result;
}

//=============================================================================
// Test 4: Memory Usage
//=============================================================================

TestResult test_memory_usage() {
    TestResult result = {
        "Free Heap Memory",
        false,
        0.0f,
        TARGET_MIN_FREE_HEAP,
        "bytes",
        nullptr
    };

    uint32_t free_heap = esp_get_free_heap_size();
    result.measured_value = free_heap;

    if (free_heap >= TARGET_MIN_FREE_HEAP) {
        result.passed = true;
    } else {
        result.failure_reason = "Low heap memory";
    }

    return result;
}

//=============================================================================
// Test 5: Audio-to-Light Latency
//=============================================================================

TestResult test_audio_to_light_latency() {
    TestResult result = {
        "Audio-to-Light Latency",
        false,
        0.0f,
        TARGET_MAX_LATENCY_MS,
        "milliseconds",
        nullptr
    };

    // Estimate latency from FPS
    extern float SYSTEM_FPS;
    extern float LED_FPS;

    // Latency ≈ 1/SYSTEM_FPS + 1/LED_FPS
    float audio_frame_time = SYSTEM_FPS > 0 ? (1000.0f / SYSTEM_FPS) : 999.0f;
    float led_frame_time = LED_FPS > 0 ? (1000.0f / LED_FPS) : 999.0f;
    float estimated_latency = audio_frame_time + led_frame_time;

    result.measured_value = estimated_latency;

    if (estimated_latency <= TARGET_MAX_LATENCY_MS) {
        result.passed = true;
    } else {
        result.failure_reason = "Latency exceeds target";
    }

    return result;
}

//=============================================================================
// Test 6: Stack Usage (All Tasks)
//=============================================================================

TestResult test_stack_usage() {
    TestResult result = {
        "Task Stack Safety",
        false,
        0.0f,
        512.0f,  // Minimum 512 bytes free
        "bytes (worst task)",
        nullptr
    };

    extern TaskHandle_t main_loop_task;
    extern TaskHandle_t led_task;
    extern TaskHandle_t encoder_task_handle;

    // Check all task stacks
    UBaseType_t min_stack_free = 65535;

    if (main_loop_task) {
        UBaseType_t free_stack = uxTaskGetStackHighWaterMark(main_loop_task);
        if (free_stack < min_stack_free) min_stack_free = free_stack;
    }

    if (led_task) {
        UBaseType_t free_stack = uxTaskGetStackHighWaterMark(led_task);
        if (free_stack < min_stack_free) min_stack_free = free_stack;
    }

    if (encoder_task_handle) {
        UBaseType_t free_stack = uxTaskGetStackHighWaterMark(encoder_task_handle);
        if (free_stack < min_stack_free) min_stack_free = free_stack;
    }

    result.measured_value = min_stack_free;

    if (min_stack_free >= 512) {
        result.passed = true;
    } else {
        result.failure_reason = "Stack running low";
    }

    return result;
}

//=============================================================================
// Test 7: Memory Fragmentation
//=============================================================================

TestResult test_heap_fragmentation() {
    TestResult result = {
        "Heap Fragmentation",
        false,
        0.0f,
        0.90f,  // Max 90% fragmentation acceptable
        "ratio",
        nullptr
    };

    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

    // Fragmentation ratio: largest_block / free_heap
    // Higher is better (1.0 = no fragmentation)
    float frag_ratio = free_heap > 0 ? (float)largest_block / free_heap : 0.0f;

    result.measured_value = frag_ratio;

    if (frag_ratio >= 0.10f) {  // At least 10% of heap in one block
        result.passed = true;
    } else {
        result.failure_reason = "Severe heap fragmentation";
    }

    return result;
}

//=============================================================================
// Master Test Runner
//=============================================================================

bool runAll(bool verbose = true) {
    const int NUM_TESTS = 7;
    TestResult results[NUM_TESTS];

    if (verbose) {
        Serial.println("\n╔════════════════════════════════════════════════════════════╗");
        Serial.println("║     PERFORMANCE REGRESSION TEST SUITE                     ║");
        Serial.println("╚════════════════════════════════════════════════════════════╝\n");
    }

    // Run all tests
    results[0] = test_audio_processing_fps();
    results[1] = test_led_rendering_fps();
    results[2] = test_gdft_processing_time();
    results[3] = test_memory_usage();
    results[4] = test_audio_to_light_latency();
    results[5] = test_stack_usage();
    results[6] = test_heap_fragmentation();

    // Count pass/fail
    int passed = 0;
    int failed = 0;

    for (int i = 0; i < NUM_TESTS; i++) {
        if (results[i].passed) {
            passed++;
        } else {
            failed++;
        }

        if (verbose) {
            const char* status = results[i].passed ? "✅ PASS" : "❌ FAIL";
            Serial.printf("%-30s %s\n", results[i].name, status);
            Serial.printf("    Measured: %.2f %s\n", results[i].measured_value, results[i].units);
            Serial.printf("    Target:   %.2f %s\n", results[i].target_value, results[i].units);

            if (!results[i].passed && results[i].failure_reason) {
                Serial.printf("    Reason:   %s\n", results[i].failure_reason);
            }
            Serial.println();
        }
    }

    if (verbose) {
        Serial.println("─────────────────────────────────────────────────────────────");
        Serial.printf("Results: %d/%d tests passed (%.1f%%)\n",
                      passed, NUM_TESTS, (float)passed / NUM_TESTS * 100.0f);
        Serial.println("─────────────────────────────────────────────────────────────\n");
    }

    return (failed == 0);
}

//=============================================================================
// Continuous Monitoring (for CI/CD)
//=============================================================================

void startContinuousMonitoring(uint32_t interval_ms = 10000) {
    static uint32_t last_test = 0;

    if (millis() - last_test >= interval_ms) {
        bool all_passed = runAll(false);  // Silent mode

        if (!all_passed) {
            Serial.println("⚠️  PERFORMANCE REGRESSION DETECTED!");
            runAll(true);  // Print detailed results
        }

        last_test = millis();
    }
}

//=============================================================================
// Golden Test (Baseline Capture)
//=============================================================================

struct GoldenMetrics {
    float audio_fps;
    float led_fps;
    float gdft_time_us;
    uint32_t free_heap;
    float latency_ms;
};

GoldenMetrics captureGolden() {
    GoldenMetrics golden;

    auto r1 = test_audio_processing_fps();
    auto r2 = test_led_rendering_fps();
    auto r3 = test_gdft_processing_time();
    auto r4 = test_memory_usage();
    auto r5 = test_audio_to_light_latency();

    golden.audio_fps = r1.measured_value;
    golden.led_fps = r2.measured_value;
    golden.gdft_time_us = r3.measured_value;
    golden.free_heap = r4.measured_value;
    golden.latency_ms = r5.measured_value;

    Serial.println("\n📸 Golden Metrics Captured:");
    Serial.printf("  Audio FPS:     %.2f\n", golden.audio_fps);
    Serial.printf("  LED FPS:       %.2f\n", golden.led_fps);
    Serial.printf("  GDFT Time:     %.2f µs\n", golden.gdft_time_us);
    Serial.printf("  Free Heap:     %u bytes\n", golden.free_heap);
    Serial.printf("  Latency:       %.2f ms\n", golden.latency_ms);
    Serial.println();

    return golden;
}

} // namespace PerformanceTest

#endif // PERFORMANCE_REGRESSION_SUITE_H
