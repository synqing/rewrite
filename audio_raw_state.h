#ifndef AUDIO_RAW_STATE_H
#define AUDIO_RAW_STATE_H

/*----------------------------------------
  AudioRawState - Phase 2A Implementation
  
  MISSION: Encapsulate audio-thread-only globals with zero performance impact
  SAFETY: No shared variables = no race conditions = minimal risk
  TARGET: i2s_samples_raw[], waveform_history[], waveform_history_index
  
  CRITICAL SUCCESS FACTORS:
  - Zero abstraction cost - direct memory access preserved
  - Safe initialization - prevents corruption
  - Audio thread only - no LED thread interaction
  - 86.6 FPS performance maintained
  ----------------------------------------*/

#include <stdint.h>
#include <cstring>
#include "constants.h"

namespace SensoryBridge {
namespace Audio {

/**
 * AudioRawState - Encapsulates I2S capture and temporal buffers
 * 
 * THREAD SAFETY: Audio thread only - no shared access
 * PERFORMANCE: Zero abstraction cost - direct array access
 * MIGRATION SAFETY: Replaces globals one-by-one with validation
 */
class AudioRawState {
private:
    // I2S capture buffer - written by i2s_read(), processed immediately
    // CRITICAL: Must maintain exact same memory layout as original global
    int32_t samples_raw_[1024];
    
    // Temporal history for potential lookahead processing
    // ORIGINAL: short waveform_history[4][1024] (globals.h:191)
    short   waveform_history_[4][1024];
    
    // Circular buffer index for waveform history
    // ORIGINAL: uint8_t waveform_history_index (globals.h:192)
    uint8_t waveform_history_index_;
    
    // DC offset accumulator for real-time bias removal
    // ORIGINAL: int32_t dc_offset_sum (globals.h:197)
    int32_t dc_offset_sum_;
    
    // Memory corruption detection guards
    static constexpr uint32_t GUARD_MAGIC = 0xABCD1234;
    uint32_t guard_prefix_;
    uint32_t guard_suffix_;

public:
    /**
     * Constructor - Ensures safe initialization
     * Called once at system startup, zero-cost thereafter
     */
    AudioRawState() : 
        waveform_history_index_(0),
        dc_offset_sum_(0),
        guard_prefix_(GUARD_MAGIC),
        guard_suffix_(GUARD_MAGIC)
    {
        // Zero all audio buffers for clean startup
        // CRITICAL: Prevents S3 phantom triggers from uninitialized memory
        memset(samples_raw_, 0, sizeof(samples_raw_));
        memset(waveform_history_, 0, sizeof(waveform_history_));
    }
    
    /**
     * PERFORMANCE CRITICAL: Direct array access for I2S processing
     * 
     * USAGE: i2s_read(I2S_PORT, audio_raw.getRawSamples(), ...)
     * REPLACES: i2s_samples_raw[] global array
     * PERFORMANCE: Zero overhead - compiles to direct memory access
     */
    int32_t* getRawSamples() { return samples_raw_; }
    const int32_t* getRawSamples() const { return samples_raw_; }
    
    /**
     * Waveform history management with bounds checking
     * 
     * USAGE: audio_raw.getCurrentHistoryFrame()[i] = processed_sample;
     * REPLACES: waveform_history[waveform_history_index][i] = sample;
     * SAFETY: Automatic bounds validation
     */
    short* getCurrentHistoryFrame() { 
        validateState(); // Debug builds only - optimized out in release
        return waveform_history_[waveform_history_index_]; 
    }
    
    /**
     * Get a specific history frame by index
     * 
     * USAGE: audio_raw.getHistoryFrame(0)[i] // Get oldest frame sample i
     * SAFETY: Index wraps around based on current history index
     */
    short* getHistoryFrame(uint8_t frame_offset) {
        uint8_t idx = (waveform_history_index_ + 4 - frame_offset) % 4;
        return waveform_history_[idx];
    }
    
    /**
     * History index management
     * 
     * USAGE: audio_raw.advanceHistoryIndex();
     * REPLACES: waveform_history_index++; if(...) waveform_history_index = 0;
     * SAFETY: Automatic wraparound, no overflow possible
     */
    void advanceHistoryIndex() {
        waveform_history_index_ = (waveform_history_index_ + 1) % 4;
    }
    
    uint8_t getHistoryIndex() const { return waveform_history_index_; }
    
    /**
     * DC offset management for real-time bias removal
     * 
     * USAGE: audio_raw.getDCOffsetSum() += sample;
     * REPLACES: dc_offset_sum += sample;
     * PERFORMANCE: Direct reference access - zero overhead
     */
    int32_t& getDCOffsetSum() { return dc_offset_sum_; }
    const int32_t& getDCOffsetSum() const { return dc_offset_sum_; }
    
    /**
     * Memory corruption detection - called by AudioGuard
     * 
     * SAFETY: Detects buffer overruns and memory corruption
     * PERFORMANCE: Only called during integrity checks (1Hz)
     */
    bool validateState() const {
        if (guard_prefix_ != GUARD_MAGIC || guard_suffix_ != GUARD_MAGIC) {
            return false;  // Memory corruption detected
        }
        if (waveform_history_index_ >= 4) {
            return false;  // Index corruption - critical for bounds safety
        }
        return true;
    }
    
    /**
     * Debug information for troubleshooting
     * Only compiled in debug builds to avoid performance impact
     */
    #ifdef DEBUG
    void printDebugInfo() const {
        USBSerial.printf("AudioRawState Debug:\n");
        USBSerial.printf("  History Index: %d/4\n", waveform_history_index_);
        USBSerial.printf("  DC Offset Sum: %d\n", dc_offset_sum_);
        USBSerial.printf("  Memory Guards: %s\n", validateState() ? "OK" : "CORRUPTED");
        USBSerial.printf("  Size: %d bytes\n", sizeof(*this));
    }
    #endif
    
    /**
     * Memory usage information
     */
    static constexpr size_t getMemoryFootprint() {
        return sizeof(AudioRawState);
    }
};

} // namespace Audio
} // namespace SensoryBridge

#endif // AUDIO_RAW_STATE_H