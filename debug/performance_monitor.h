#ifndef PERFORMANCE_MONITOR_H
#define PERFORMANCE_MONITOR_H

#include <Arduino.h>

// Forward declarations to avoid circular includes
extern uint32_t g_race_condition_count;

// Constants needed
#ifndef NUM_FREQS
#define NUM_FREQS 96
#endif

// Performance tracking structure
struct PerformanceMetrics {
    // Timing metrics (microseconds)
    uint32_t frame_start_time;
    uint32_t i2s_read_time;
    uint32_t gdft_compute_time;
    uint32_t gdft_per_bin_time;
    uint32_t post_process_time;
    uint32_t led_update_time;
    uint32_t total_frame_time;
    
    // Memory metrics
    uint32_t free_heap;
    uint32_t min_free_heap;
    uint32_t largest_free_block;
    
    // Audio metrics
    float max_magnitude;
    float avg_magnitude;
    float noise_floor;
    uint8_t active_bins;
    float agc_multiplier;
    float vu_level;
    int32_t dc_offset_current;
    
    // Frequency response
    float bin_magnitudes[NUM_FREQS];
    uint8_t peak_bin;
    float peak_frequency;
    
    // Frame statistics
    uint32_t frame_count;
    uint32_t dropped_frames;
    uint32_t i2s_underruns;
    
    // Rolling averages
    float fps_avg;
    float cpu_usage;
    uint32_t gdft_time_avg;
};

extern PerformanceMetrics perf_metrics;

// Timing macros
#define PERF_MONITOR_START() uint32_t _perf_start = micros()
#define PERF_MONITOR_END(metric) perf_metrics.metric = micros() - _perf_start

// Initialize performance monitoring
void init_performance_monitor();

// Update metrics each frame
void update_performance_metrics();

// Log performance data
void log_performance_data();

// Specific metric trackers
void track_gdft_performance(uint32_t bin_count, uint32_t total_time);
void track_memory_usage();
void track_audio_metrics(float* magnitudes, uint8_t num_bins);

// Test routines
void run_frequency_sweep_test();
void run_latency_test();
void run_stress_test();
void update_stress_test();

// Format performance data for serial output
String format_perf_summary();
String format_perf_detailed();

// Serial command handlers
void handle_perf_command(const char* cmd);

#endif // PERFORMANCE_MONITOR_H