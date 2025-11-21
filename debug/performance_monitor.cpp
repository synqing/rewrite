#include "performance_monitor.h"
#include "../serial_config.h"  // USBSerial configuration

// External globals
extern struct freq {
    float target_freq;
    int32_t coeff_q14;
    uint16_t block_size;
    float block_size_recip;
    float inv_block_size_half;
    uint8_t zone;
    float a_weighting_ratio;
    float window_mult;
} frequencies[NUM_FREQS];

PerformanceMetrics perf_metrics;
static bool perf_summary_logging = false;
static bool perf_frequency_logging = false;

// Rolling average buffers
static float fps_history[30] = {0};
static uint8_t fps_index = 0;
static uint32_t gdft_history[30] = {0};
static uint8_t gdft_index = 0;

void init_performance_monitor() {
    memset(&perf_metrics, 0, sizeof(PerformanceMetrics));
    perf_metrics.min_free_heap = ESP.getFreeHeap();
    
    USBSerial.println("=== PERFORMANCE MONITOR INITIALIZED ===");
    USBSerial.printf("Target Config: %d bins, 16000Hz sample rate, 128 samples/chunk\n", 
                     NUM_FREQS);
}

void update_performance_metrics() {
    perf_metrics.frame_count++;
    
    // Calculate FPS
    if (perf_metrics.total_frame_time > 0) {
        float current_fps = 1000000.0 / perf_metrics.total_frame_time;
        fps_history[fps_index++] = current_fps;
        if (fps_index >= 30) fps_index = 0;
        
        float fps_sum = 0;
        for (int i = 0; i < 30; i++) {
            fps_sum += fps_history[i];
        }
        perf_metrics.fps_avg = fps_sum / 30.0;
    }
    
    // Update GDFT average
    gdft_history[gdft_index++] = perf_metrics.gdft_compute_time;
    if (gdft_index >= 30) gdft_index = 0;
    
    uint32_t gdft_sum = 0;
    for (int i = 0; i < 30; i++) {
        gdft_sum += gdft_history[i];
    }
    perf_metrics.gdft_time_avg = gdft_sum / 30;
    
    // CPU usage estimate (rough)
    perf_metrics.cpu_usage = (perf_metrics.total_frame_time / 10000.0); // percentage of 10ms frame
    
    // Track memory
    track_memory_usage();
    
    // Update stress test if running
    update_stress_test();
}

void track_memory_usage() {
    perf_metrics.free_heap = ESP.getFreeHeap();
    perf_metrics.largest_free_block = ESP.getMaxAllocHeap();
    
    if (perf_metrics.free_heap < perf_metrics.min_free_heap) {
        perf_metrics.min_free_heap = perf_metrics.free_heap;
    }
}

void track_audio_metrics(float* magnitudes, uint8_t num_bins) {
    float sum = 0;
    float max_val = 0;
    uint8_t active = 0;
    uint8_t peak = 0;
    
    // Debug: Track high frequency activity
    static uint32_t debug_counter = 0;
    static float high_freq_max = 0;
    static uint8_t high_freq_peak = 0;
    debug_counter++;
    
    for (uint8_t i = 0; i < num_bins; i++) {
        perf_metrics.bin_magnitudes[i] = magnitudes[i];
        sum += magnitudes[i];
        
        if (magnitudes[i] > max_val) {
            max_val = magnitudes[i];
            peak = i;
        }
        
        // Track high frequency bins (above 1kHz, roughly bin 48+)
        if (i >= 48 && magnitudes[i] > high_freq_max) {
            high_freq_max = magnitudes[i];
            high_freq_peak = i;
        }
        
        if (magnitudes[i] > 0.1) { // Threshold for "active"
            active++;
        }
    }
    
    // Debug output every ~2 seconds when enabled
    if (perf_frequency_logging && (debug_counter % 200 == 0)) {
        USBSerial.printf("FREQ_SPECTRUM: Peak=%d(%.0fHz,mag=%.1f) | HighFreq=%d(%.0fHz,mag=%.1f)\n",
            peak, frequencies[peak].target_freq, max_val,
            high_freq_peak, frequencies[high_freq_peak].target_freq, high_freq_max);
        
        // Show magnitude distribution across frequency ranges
        float low_sum = 0, mid_sum = 0, high_sum = 0;
        int low_active = 0, mid_active = 0, high_active = 0;
        
        // Low: 0-32 (55-493Hz)
        for (uint8_t i = 0; i < 32; i++) {
            low_sum += magnitudes[i];
            if (magnitudes[i] > 1.0) low_active++;
        }
        // Mid: 32-64 (523-1975Hz)
        for (uint8_t i = 32; i < 64; i++) {
            mid_sum += magnitudes[i];
            if (magnitudes[i] > 1.0) mid_active++;
        }
        // High: 64-96 (2093-13289Hz)
        for (uint8_t i = 64; i < NUM_FREQS; i++) {
            high_sum += magnitudes[i];
            if (magnitudes[i] > 1.0) high_active++;
        }
        
        USBSerial.printf("FREQ_DIST: Low[%d active,sum=%.1f] Mid[%d active,sum=%.1f] High[%d active,sum=%.1f]\n",
            low_active, low_sum, mid_active, mid_sum, high_active, high_sum);
        
        high_freq_max = 0;
        high_freq_peak = 0;
    }
    
    perf_metrics.avg_magnitude = sum / num_bins;
    perf_metrics.max_magnitude = max_val;
    perf_metrics.active_bins = active;
    perf_metrics.peak_bin = peak;
    perf_metrics.peak_frequency = frequencies[peak].target_freq;
    
    // Get current audio state
    // perf_metrics.vu_level = audio_vu_level; // Skip due to type mismatch
    // perf_metrics.dc_offset_current = audio_raw_state.getDCOffsetSum() / 256; // Skip due to missing include
}

void track_gdft_performance(uint32_t bin_count, uint32_t total_time) {
    if (bin_count > 0) {
        perf_metrics.gdft_per_bin_time = total_time / bin_count;
    }
}

void log_performance_data() {
    static uint32_t last_log = 0;
    uint32_t now = millis();
    
    if (!perf_summary_logging) {
        return;
    }
    // Log summary every 2 seconds (was every second)
    if (now - last_log >= 2000) {
        USBSerial.println(format_perf_summary());
        last_log = now;
    }
}

String format_perf_summary() {
    char buffer[256];
    snprintf(buffer, sizeof(buffer),
        "PERF|FPS:%.1f|GDFT:%luus|HEAP:%lu|CPU:%.1f%%|BINS:%d|ACTIVE:%d|PEAK:%.0fHz",
        perf_metrics.fps_avg,
        perf_metrics.gdft_time_avg,
        perf_metrics.free_heap,
        perf_metrics.cpu_usage,
        NUM_FREQS,
        perf_metrics.active_bins,
        perf_metrics.peak_frequency
    );
    return String(buffer);
}

String format_perf_detailed() {
    String output = "\n=== DETAILED PERFORMANCE REPORT ===\n";
    
    output += "TIMING (us):\n";
    output += "  I2S Read: " + String(perf_metrics.i2s_read_time) + "\n";
    output += "  GDFT Total: " + String(perf_metrics.gdft_compute_time) + "\n";
    output += "  GDFT/Bin: " + String(perf_metrics.gdft_per_bin_time) + "\n";
    output += "  Post-Process: " + String(perf_metrics.post_process_time) + "\n";
    output += "  LED Update: " + String(perf_metrics.led_update_time) + "\n";
    output += "  Frame Total: " + String(perf_metrics.total_frame_time) + "\n";
    
    output += "\nMEMORY:\n";
    output += "  Free Heap: " + String(perf_metrics.free_heap) + "\n";
    output += "  Min Free: " + String(perf_metrics.min_free_heap) + "\n";
    output += "  Largest Block: " + String(perf_metrics.largest_free_block) + "\n";
    
    output += "\nAUDIO:\n";
    output += "  Max Magnitude: " + String(perf_metrics.max_magnitude) + "\n";
    output += "  Avg Magnitude: " + String(perf_metrics.avg_magnitude) + "\n";
    output += "  Active Bins: " + String(perf_metrics.active_bins) + "\n";
    output += "  Peak Freq: " + String(perf_metrics.peak_frequency) + " Hz\n";
    output += "  VU Level: " + String(perf_metrics.vu_level) + "\n";
    output += "  DC Offset: " + String(perf_metrics.dc_offset_current) + "\n";
    
    output += "\nSTATISTICS:\n";
    output += "  Frames: " + String(perf_metrics.frame_count) + "\n";
    output += "  Dropped: " + String(perf_metrics.dropped_frames) + "\n";
    output += "  I2S Underruns: " + String(perf_metrics.i2s_underruns) + "\n";
    
    return output;
}

// Test routines
void run_frequency_sweep_test() {
    USBSerial.println("\n=== FREQUENCY SWEEP TEST ===");
    USBSerial.println("Play a sine wave sweep from 55Hz to 13kHz");
    USBSerial.println("Monitor peak bin tracking...");
    
    // This would be monitored externally while playing test tone
    for (int i = 0; i < 10; i++) {
        delay(1000);
        USBSerial.printf("Peak: Bin %d (%.1f Hz), Magnitude: %.2f\n",
            perf_metrics.peak_bin,
            perf_metrics.peak_frequency,
            perf_metrics.max_magnitude
        );
    }
}

void run_latency_test() {
    USBSerial.println("\n=== LATENCY TEST ===");
    USBSerial.println("Measuring audio-to-LED latency...");
    
    // Mark when audio spike detected
    static bool spike_detected = false;
    static uint32_t spike_time = 0;
    
    if (!spike_detected && perf_metrics.max_magnitude > 10.0) {
        spike_detected = true;
        spike_time = micros();
        USBSerial.println("Audio spike detected!");
    }
    
    // This would need external measurement of LED response time
}

// Stress test state
static bool stress_test_running = false;
static uint32_t stress_test_start = 0;
static float stress_test_min_fps = 1000;
static uint32_t stress_test_max_gdft = 0;
static uint32_t stress_test_initial_heap = 0;

void run_stress_test() {
    if (!stress_test_running) {
        USBSerial.println("\n=== STRESS TEST STARTED ===");
        USBSerial.println("Running for 60 seconds (non-blocking)...");
        USBSerial.println("Continue using 'PERF' command to check progress");
        
        stress_test_running = true;
        stress_test_start = millis();
        stress_test_min_fps = 1000;
        stress_test_max_gdft = 0;
        stress_test_initial_heap = perf_metrics.free_heap;
    } else {
        USBSerial.println("Stress test already running!");
    }
}

// Call this from update_performance_metrics()
void update_stress_test() {
    if (!stress_test_running) return;
    
    // Update min/max values
    if (perf_metrics.fps_avg < stress_test_min_fps) {
        stress_test_min_fps = perf_metrics.fps_avg;
    }
    if (perf_metrics.gdft_compute_time > stress_test_max_gdft) {
        stress_test_max_gdft = perf_metrics.gdft_compute_time;
    }
    
    // Check if test complete
    if (millis() - stress_test_start >= 60000) {
        USBSerial.println("\n=== STRESS TEST COMPLETE ===");
        USBSerial.printf("Results over 60 seconds:\n");
        USBSerial.printf("  Min FPS: %.1f\n", stress_test_min_fps);
        USBSerial.printf("  Max GDFT time: %lu us\n", stress_test_max_gdft);
        USBSerial.printf("  Memory leaked: %lu bytes\n", 
            stress_test_initial_heap - perf_metrics.free_heap);
        USBSerial.printf("  Final heap: %lu bytes\n", perf_metrics.free_heap);
        
        stress_test_running = false;
    }
}

void handle_perf_command(const char* cmd) {
    if (strcmp(cmd, "PERF") == 0) {
        USBSerial.println(format_perf_detailed());
    }
    else if (strcmp(cmd, "PERF LOG ON") == 0) {
        perf_summary_logging = true;
        USBSerial.println("Performance summary logging ENABLED (every 2s).");
    }
    else if (strcmp(cmd, "PERF LOG OFF") == 0) {
        perf_summary_logging = false;
        USBSerial.println("Performance summary logging DISABLED.");
    }
    else if (strcmp(cmd, "PERF FREQ ON") == 0) {
        perf_frequency_logging = true;
        USBSerial.println("Frequency distribution logging ENABLED.");
    }
    else if (strcmp(cmd, "PERF FREQ OFF") == 0) {
        perf_frequency_logging = false;
        USBSerial.println("Frequency distribution logging DISABLED.");
    }
    else if (strcmp(cmd, "PERF SWEEP") == 0) {
        run_frequency_sweep_test();
    }
    else if (strcmp(cmd, "PERF STRESS") == 0) {
        run_stress_test();
    }
    else if (strcmp(cmd, "PERF RESET") == 0) {
        init_performance_monitor();
        USBSerial.println("Performance metrics reset");
    }
    else {
        USBSerial.println("Performance commands:");
        USBSerial.println("  PERF           - Show detailed performance report");
        USBSerial.println("  PERF LOG ON    - Enable periodic PERF|FPS summary output");
        USBSerial.println("  PERF LOG OFF   - Disable periodic PERF|FPS summary output");
        USBSerial.println("  PERF FREQ ON   - Enable FREQ_SPECTRUM/FREQ_DIST logging");
        USBSerial.println("  PERF FREQ OFF  - Disable FREQ_SPECTRUM/FREQ_DIST logging");
        USBSerial.println("  PERF SWEEP     - Run frequency sweep test");
        USBSerial.println("  PERF STRESS    - Run 60-second stress test");
        USBSerial.println("  PERF RESET     - Reset performance metrics");
    }
}
