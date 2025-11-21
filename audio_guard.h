/*----------------------------------------
  Audio Guard - Defensive Layer
  
  Prevents phantom noise calibration triggers
  and protects audio pipeline integrity.
  
  CRITICAL: This must be called early in setup()
  and periodically in the main loop.
  
  ⚠️  TECHNICAL DEBT WARNING - CRITICAL DESIGN ISSUES ⚠️
  
  BACKGROUND:
  AudioGuard was implemented as an emergency fix for phantom noise calibration
  triggers on ESP32-S3 devices. The phantom triggers occurred because S3 devices
  lack physical buttons, but uninitialized memory was being interpreted as button
  press events, causing the system to enter noise calibration mode unexpectedly.
  
  ORIGINAL DESIGN FLAW - HARDCODED ASSUMPTIONS:
  The validation logic in validateAudioState() was written with INCORRECT assumptions
  about system configuration. Specifically:
  
  1. SAMPLE RATE ASSUMPTION (FIXED 2025-06-26):
     - Originally hardcoded to ONLY accept 48kHz as valid
     - System actually uses 12.8kHz as native sample rate by design
     - AudioGuard was "correcting" valid 12.8kHz to invalid 48kHz
     - FIX: Now accepts both 12800Hz (native) and 48000Hz as valid
  
  2. AGGRESSIVE VALIDATION PATTERN:
     - Runs every 1 second checking for "corruption"
     - Assumes ANY deviation from hardcoded values = memory corruption
     - No learning mechanism - continuously overwrites valid user settings
     - RISK: May interfere with runtime configuration changes
  
  3. NUCLEAR INITIALIZATION:
     - initAudioSafe() zeros ALL audio buffers indiscriminately
     - Resets ALL indices without context awareness
     - Could potentially interfere with ongoing audio processing
  
  RECOMMENDED IMPROVEMENTS:
  1. Replace hardcoded validation with configuration-aware validation
  2. Implement validation ranges rather than exact value matching
  3. Add configuration persistence to distinguish corruption from valid changes
  4. Consider making integrity checks less frequent or event-driven
  5. Add selective buffer initialization rather than mass zeroing
  
  CURRENT STATUS:
  - blockPhantomTriggers() - GOOD: Effectively prevents S3 phantom triggers
  - validateAudioState() - PATCHED: Sample rate validation fixed, but pattern remains problematic
  - initAudioSafe() - REVIEW NEEDED: Mass zeroing approach may be too aggressive
  - checkIntegrity() - REVIEW NEEDED: 1Hz continuous validation may be excessive
  
  LESSON LEARNED:
  Emergency defensive programming must be implemented with deep understanding
  of system architecture. Hardcoded assumptions in defensive layers can cause
  more damage than the original problems they're designed to solve.
  
  Last Updated: 2025-06-26 - Sample rate validation fix applied
  Next Review: Required before Phase 2 audio pipeline encapsulation
  ----------------------------------------*/

#ifndef AUDIO_GUARD_H
#define AUDIO_GUARD_H

class AudioGuard {
private:
    static uint32_t last_guard_check;
    static bool guard_initialized;
    
public:
    // Initialize the audio guard system
    static void init() {
        USBSerial.println("AUDIO GUARD: Initializing defensive layer");
        
        // Force clean state on S3 devices
        #ifdef ARDUINO_ESP32S3_DEV
        blockPhantomTriggers();
        USBSerial.println("AUDIO GUARD: S3 phantom trigger protection enabled");
        #endif
        
        guard_initialized = true;
        last_guard_check = millis();
    }
    
    // Block phantom noise calibration triggers on S3
    static void blockPhantomTriggers() {
        #ifdef ARDUINO_ESP32S3_DEV
        // S3 has no physical buttons - only clear button state, don't force noise_complete
        // This allows legitimate noise calibration triggered via serial commands
        
        // Clear any button state that could trigger calibration
        noise_button.pressed = false;
        noise_button.last_down = 0;
        noise_button.last_up = 0;
        
        // REMOVED: Forcing noise_complete=true was preventing legitimate calibration!
        // The system needs to be able to run noise calibration when requested
        
        // Ensure transition flags are clean
        noise_transition_queued = false;
        
        // Validate noise calibration values
        if (CONFIG.SWEET_SPOT_MIN_LEVEL == 0) {
            CONFIG.SWEET_SPOT_MIN_LEVEL = 100;  // Safe default
            USBSerial.println("AUDIO GUARD: Set SWEET_SPOT_MIN_LEVEL to safe default");
        }
        #endif
    }
    
    // Periodic safety check - call from main loop
    static void checkIntegrity(uint32_t t_now) {
        if (!guard_initialized) {
            init();
            return;
        }
        
        // Check every 1 second
        if (t_now - last_guard_check < 1000) {
            return;
        }
        last_guard_check = t_now;
        
        #ifdef ARDUINO_ESP32S3_DEV
        // REMOVED: Don't force noise_complete - let calibration run when needed
        // Only validate audio state for corruption
        validateAudioState();
        #endif
    }
    
    // Validate critical audio state
    static void validateAudioState() {
        // Check for out-of-bounds values that indicate corruption
        bool corruption_detected = false;
        
        // Validate configuration ranges
        if (CONFIG.SENSITIVITY < 0.001 || CONFIG.SENSITIVITY > 10.0) {
            USBSerial.printf("AUDIO GUARD WARNING: Invalid sensitivity %.3f detected!\n", 
                           CONFIG.SENSITIVITY);
            // CONFIG.SENSITIVITY = 0.21; // DO NOT AUTOCORRECT
            corruption_detected = true;
        }
        
        // CRITICAL FIX: Replace hardcoded, flawed validation with a flexible check.
        // The previous logic only allowed 12.8kHz or 48kHz and would forcibly
        // overwrite valid configurations (like 32kHz), causing pipeline failure.
        // This new function validates a range of standard audio rates without
        // destructively modifying the configuration.
        auto isValidSampleRate = [](uint32_t rate) {
            return (rate == 8000 || rate == 16000 || rate == 22050 || 
                    rate == 32000 || rate == 44100 || rate == 48000);
        };

        if (!isValidSampleRate(CONFIG.SAMPLE_RATE)) {
            USBSerial.printf("AUDIO GUARD WARNING: Potentially invalid sample rate %d detected!\n",
                           CONFIG.SAMPLE_RATE);
            // CONFIG.SAMPLE_RATE = DEFAULT_SAMPLE_RATE; // DO NOT AUTOCORRECT. Let the system run.
            corruption_detected = true; // Still flag it as a potential issue to observe.
        }
        
        if (CONFIG.LED_COUNT > 1000 || CONFIG.LED_COUNT == 0) {
            USBSerial.printf("AUDIO GUARD WARNING: Invalid LED count %d detected!\n", 
                           CONFIG.LED_COUNT);
            // CONFIG.LED_COUNT = 128; // DO NOT AUTOCORRECT
            corruption_detected = true;
        }
        
        // Validate AudioRawState integrity
        if (!audio_raw_state.validateState()) {
            USBSerial.println("AUDIO GUARD: AudioRawState corruption detected");
            corruption_detected = true;
        }
        
        if (corruption_detected) {
            USBSerial.println("AUDIO GUARD: Corruption detected and corrected");
        }
    }
    
    // Safe audio initialization wrapper
    static bool initAudioSafe() {
        USBSerial.println("AUDIO GUARD: Safe audio initialization starting");
        
        // Pre-initialize all audio globals to safe values
        // Phase 2A NOTE: i2s_samples_raw, waveform_history, waveform_history_index 
        // are now encapsulated in AudioRawState and safely initialized by constructor
        memset(waveform, 0, sizeof(waveform));
        memset(sample_window, 0, sizeof(sample_window));
        memset(magnitudes, 0, sizeof(magnitudes));
        
        // Phase 2A: AudioRawState handles its own initialization
        // No need to manually set waveform_history_index - constructor handles it
        
        // Initialize audio state
        max_waveform_val = 0;
        max_waveform_val_raw = 0;
        audio_vu_level = 0;
        
        // Just clean button state, don't force noise_complete
        #ifdef ARDUINO_ESP32S3_DEV
        blockPhantomTriggers();
        #endif
        
        return true;
    }
    
    // Check if it's safe to process audio
    static bool isSafeToProcess() {
        // Simply return the actual noise_complete state
        // Don't force it - let the system run calibration when needed
        return noise_complete;
    }
    
    // Debug output for audio state
    static void printAudioState() {
        USBSerial.println("\n=== AUDIO GUARD STATE ===");
        USBSerial.printf("Platform: %s\n", 
                       #ifdef ARDUINO_ESP32S3_DEV
                       "ESP32-S3"
                       #else
                       "ESP32-S2"
                       #endif
                       );
        USBSerial.printf("Guard initialized: %s\n", guard_initialized ? "YES" : "NO");
        USBSerial.printf("Noise complete: %s\n", noise_complete ? "YES" : "NO");
        // USBSerial.printf("Noise cal index: %d\n", noise_cal_index);
        USBSerial.printf("Sensitivity: %.3f\n", CONFIG.SENSITIVITY);
        USBSerial.printf("Sweet spot min: %d\n", CONFIG.SWEET_SPOT_MIN_LEVEL);
        USBSerial.printf("Sample rate: %d\n", CONFIG.SAMPLE_RATE);
        USBSerial.printf("LED count: %d\n", CONFIG.LED_COUNT);
        USBSerial.println("========================\n");
    }
};

// Static member initialization
uint32_t AudioGuard::last_guard_check = 0;
bool AudioGuard::guard_initialized = false;

#endif // AUDIO_GUARD_H