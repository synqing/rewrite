#ifndef AUDIO_PROCESSED_STATE_H
#define AUDIO_PROCESSED_STATE_H

/*----------------------------------------
  AudioProcessedState - Phase 2B Implementation
  
  MISSION: Encapsulate shared audio processing results with race condition safety
  RISK LEVEL: MEDIUM - Audio writes, LED reads (single-core provides atomicity)
  TARGET: waveform[], max_waveform_val*, silence state, current_punch
  
  CRITICAL SUCCESS FACTORS:
  - Direct array access preserved for 86.6 FPS performance
  - Single-core scheduling provides atomic context switching
  - Zero synchronization overhead
  - Shared data clearly identified and documented
  ----------------------------------------*/

#include <stdint.h>
#include <cstring>
#include "constants.h"

namespace SensoryBridge {
namespace Audio {

/**
 * AudioProcessedState - Encapsulates shared audio processing results
 * 
 * THREAD SAFETY: Audio writes, LED reads - single-core scheduling provides atomicity
 * PERFORMANCE: Direct array access preserved for hot paths
 * SHARED DATA: Race condition potential mitigated by Core 0 scheduling
 */
class AudioProcessedState {
private:
    // SHARED: Processed waveform - written by audio thread, read by LED thread
    // ORIGINAL: short waveform[1024] (globals.h:189)
    // RACE CONDITION: Audio writes in i2s_audio.h, LED reads in lightshow_modes.h
    short   waveform_[1024];
    
    // Fixed-point version for mathematical operations
    // ORIGINAL: SQ15x16 waveform_fixed_point[1024] (globals.h:190)
    SQ15x16 waveform_fixed_point_[1024];
    
    // SHARED: Volume analysis - updated per-sample by audio thread, read by LED thread
    // ORIGINAL: float max_waveform_val_raw (globals.h:193)
    // PERFORMANCE CRITICAL: Updated 96 times per frame (8,313 times/second)
    float   max_waveform_val_raw_;
    
    // SHARED: float max_waveform_val (globals.h:194)
    float   max_waveform_val_;
    
    // SHARED: float max_waveform_val_follower (globals.h:195)
    float   max_waveform_val_follower_;
    
    // SHARED: float waveform_peak_scaled (globals.h:196)
    float   waveform_peak_scaled_;
    
    // SHARED: Audio state flags
    // ORIGINAL: bool silence (globals.h:199)
    bool    silence_;
    
    // ORIGINAL: float silent_scale (globals.h:201)
    float   silent_scale_;
    
    // ORIGINAL: float current_punch (globals.h:202)
    float   current_punch_;
    
    // Performance and debugging counters
    uint32_t frame_count_;
    uint32_t corruption_count_;
    
    // Memory corruption detection
    static constexpr uint32_t GUARD_MAGIC = 0xABCD5678;
    uint32_t guard_prefix_;
    uint32_t guard_suffix_;

public:
    /**
     * Constructor - Ensures safe initialization
     * Called once at system startup, zero-cost thereafter
     */
    AudioProcessedState() :
        max_waveform_val_raw_(0.0f),
        max_waveform_val_(0.0f),
        max_waveform_val_follower_(0.0f),
        waveform_peak_scaled_(0.0f),
        silence_(false),
        silent_scale_(1.0f),
        current_punch_(0.0f),
        frame_count_(0),
        corruption_count_(0),
        guard_prefix_(GUARD_MAGIC),
        guard_suffix_(GUARD_MAGIC)
    {
        // Zero all audio buffers for clean startup
        memset(waveform_, 0, sizeof(waveform_));
        memset(waveform_fixed_point_, 0, sizeof(waveform_fixed_point_));
    }
    
    /**
     * PERFORMANCE CRITICAL: Direct waveform access
     * 
     * USAGE: audio_processed.getWaveform()[i] = processed_sample;
     * REPLACES: waveform[i] = processed_sample;
     * PERFORMANCE: Zero overhead - compiles to direct memory access
     * RACE CONDITION: Audio writes, LED reads - single-core provides safety
     */
    short* getWaveform() { return waveform_; }
    const short* getWaveform() const { return waveform_; }
    
    /**
     * Fixed-point waveform access for mathematical operations
     * USAGE: audio_processed.getWaveformFixedPoint()[i] = fixed_sample;
     * REPLACES: waveform_fixed_point[i] = fixed_sample;
     */
    SQ15x16* getWaveformFixedPoint() { return waveform_fixed_point_; }
    const SQ15x16* getWaveformFixedPoint() const { return waveform_fixed_point_; }
    
    /**
     * PERFORMANCE CRITICAL: Volume analysis access
     * 
     * THREAD SAFETY: Single-core scheduler ensures atomic reads
     * Used by LED thread for volume-reactive effects
     */
    float getMaxRaw() const { return max_waveform_val_raw_; }
    float getMax() const { return max_waveform_val_; }
    float getMaxFollower() const { return max_waveform_val_follower_; }
    float getPeakScaled() const { return waveform_peak_scaled_; }
    
    /**
     * PERFORMANCE CRITICAL: Volume updates - called by audio thread only
     * 
     * USAGE: audio_processed.updatePeak(sample_abs);
     * REPLACES: if (sample_abs > max_waveform_val_raw) max_waveform_val_raw = sample_abs;
     * FREQUENCY: Called 96 times per frame (8,313 operations/second)
     */
    void updatePeak(float raw_peak) {
        if (raw_peak > max_waveform_val_raw_) {
            max_waveform_val_raw_ = raw_peak;
        }
    }
    
    /**
     * Volume analysis batch update
     * Called once per frame after all samples processed
     */
    void updateVolumeAnalysis(float max_val, float follower, float scaled) {
        max_waveform_val_ = max_val;
        max_waveform_val_follower_ = follower;
        waveform_peak_scaled_ = scaled;
    }
    
    /**
     * Audio state management
     * REPLACES: silence, silent_scale, current_punch globals
     */
    bool isSilent() const { return silence_; }
    void setSilent(bool silent) { silence_ = silent; }
    
    float getSilentScale() const { return silent_scale_; }
    void setSilentScale(float scale) { silent_scale_ = scale; }
    
    float getCurrentPunch() const { return current_punch_; }
    void setCurrentPunch(float punch) { current_punch_ = punch; }
    
    /**
     * Frame processing lifecycle
     * Called at start of each audio frame
     */
    void beginFrame() {
        // Reset per-frame peak tracking
        max_waveform_val_raw_ = 0.0f;
        frame_count_++;
    }
    
    /**
     * Direct access to raw values for legacy compatibility
     * TEMPORARY: Use during migration phase, remove after Phase 2C
     */
    float& getMaxRawRef() { return max_waveform_val_raw_; }
    float& getMaxRef() { return max_waveform_val_; }
    
    /**
     * Memory corruption detection
     * Called by AudioGuard::checkIntegrity()
     */
    bool validateState() const {
        if (guard_prefix_ != GUARD_MAGIC || guard_suffix_ != GUARD_MAGIC) {
            return false;  // Memory corruption detected
        }
        
        // Validate reasonable ranges for audio values
        if (max_waveform_val_raw_ < 0.0f || max_waveform_val_raw_ > 100000.0f) {
            return false;  // Unreasonable peak value
        }
        
        if (silent_scale_ < 0.0f || silent_scale_ > 10.0f) {
            return false;  // Invalid scale factor
        }
        
        return true;
    }
    
    /**
     * Performance monitoring
     */
    uint32_t getFrameCount() const { return frame_count_; }
    uint32_t getCorruptionCount() const { return corruption_count_; }
    
    void reportCorruption() { corruption_count_++; }
    
    /**
     * Debug information for troubleshooting
     * Only compiled in debug builds to avoid performance impact
     */
    #ifdef DEBUG
    void printDebugInfo() const {
        USBSerial.printf("AudioProcessedState Debug:\n");
        USBSerial.printf("  Frame Count: %d\n", frame_count_);
        USBSerial.printf("  Max Raw: %.2f\n", max_waveform_val_raw_);
        USBSerial.printf("  Max: %.2f\n", max_waveform_val_);
        USBSerial.printf("  Silent: %s\n", silence_ ? "YES" : "NO");
        USBSerial.printf("  Silent Scale: %.3f\n", silent_scale_);
        USBSerial.printf("  Memory Guards: %s\n", validateState() ? "OK" : "CORRUPTED");
        USBSerial.printf("  Size: %d bytes\n", sizeof(*this));
    }
    #endif
    
    /**
     * Memory usage information
     */
    static constexpr size_t getMemoryFootprint() {
        return sizeof(AudioProcessedState);
    }
};

} // namespace Audio
} // namespace SensoryBridge

#endif // AUDIO_PROCESSED_STATE_H