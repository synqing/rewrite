/*----------------------------------------
  Sensory Bridge I2S FUNCTIONS
  ----------------------------------------*/

#include <driver/i2s.h>
#include "audio_raw_state.h"
#include "audio_processed_state.h"
#include "globals.h" // for AGC_GAIN

// Phase 2A: Access to AudioRawState instance for migration
extern SensoryBridge::Audio::AudioRawState audio_raw_state;

// Phase 2B: Access to AudioProcessedState instance for migration
extern SensoryBridge::Audio::AudioProcessedState audio_processed_state;

extern float raw_rms_global;
extern bool audio_debug_logging_enabled;

const i2s_config_t i2s_config = {
  .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
  .sample_rate = CONFIG.SAMPLE_RATE,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
  .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
  .communication_format = I2S_COMM_FORMAT_STAND_I2S,
  .dma_buf_count = 8,  // Increased from 2 for better double-buffering
  .dma_buf_len = CONFIG.SAMPLES_PER_CHUNK * 2,  // Larger buffers reduce interrupt overhead
};

const i2s_pin_config_t pin_config = {
  .bck_io_num = I2S_BCLK_PIN,
  .ws_io_num = I2S_LRCLK_PIN,
  .data_out_num = -1,  // not used (only for outputs)
  .data_in_num = I2S_DIN_PIN
};

void init_i2s() {
  esp_err_t result = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  USBSerial.print("INIT I2S: ");
  USBSerial.println(result == ESP_OK ? SB_PASS : SB_FAIL);

#ifndef ARDUINO_ESP32S3_DEV
  // S2-specific register hacks
  REG_SET_BIT(I2S_TIMING_REG(I2S_PORT), BIT(9));
  REG_SET_BIT(I2S_CONF_REG(I2S_PORT), I2S_RX_MSB_SHIFT);
#endif

  result = i2s_set_pin(I2S_PORT, &pin_config);
  USBSerial.print("I2S SET PINS: ");
  USBSerial.println(result == ESP_OK ? SB_PASS : SB_FAIL);
}

void acquire_sample_chunk(uint32_t t_now) {
  static int8_t sweet_spot_state_last = 0;
  static bool silence_temp = false;
  static uint32_t silence_switched = 0;
  static float silent_scale_last = 1.0;
  static uint32_t last_state_change_time = 0;
  static const uint32_t MIN_STATE_DURATION_MS = 1500; // 1 second minimum in each state
  static float max_waveform_val_raw_smooth = 0.0; // Added for smoothing

  size_t bytes_read = 0;
  // Phase 2A: Replace i2s_samples_raw with AudioRawState buffer
  // Use finite timeout to prevent indefinite blocking (10ms should be more than enough for 8ms of audio)
  i2s_read(I2S_PORT, audio_raw_state.getRawSamples(), CONFIG.SAMPLES_PER_CHUNK * sizeof(int32_t), &bytes_read, pdMS_TO_TICKS(10));

  if (audio_debug_logging_enabled && (t_now % 5000 == 0)) {
    USBSerial.print("DEBUG: Bytes read from I2S: ");
    USBSerial.print(bytes_read);
    USBSerial.print(" Max raw value: ");
    USBSerial.println(max_waveform_val_raw);
  }

  max_waveform_val = 0.0;
  max_waveform_val_raw = 0.0;
  // Phase 2A: Replace waveform_history_index with AudioRawState method
  audio_raw_state.advanceHistoryIndex();

  float raw_sum_sq = 0.0f;
  for (uint16_t i = 0; i < CONFIG.SAMPLES_PER_CHUNK; i++) {
    #ifdef ARDUINO_ESP32S3_DEV
    // S3: I2S data comes in as 32-bit signed, use it directly
    // Phase 2A: Use AudioRawState buffer instead of global array
    int32_t sample = audio_raw_state.getRawSamples()[i] >> 14;  // Scale down from 32-bit to ~18-bit range
    #else
    // S2: Original calculation
    // Phase 2A: Use AudioRawState buffer instead of global array
    int32_t sample = (audio_raw_state.getRawSamples()[i] * 0.000512) + 56000 - 5120;
    sample = sample >> 2;  // Helps prevent overflow in fixed-point math coming up
    #endif
    
    // Remove DC offset BEFORE applying sensitivity
    sample -= CONFIG.DC_OFFSET;
    
    // Apply configured sensitivity (raw for silence gate)
    sample *= CONFIG.SENSITIVITY;

    // Accumulate raw squared for silence gate BEFORE AGC
    raw_sum_sq += float(sample) * float(sample);

    // Now apply AGC gain for normal processing
    if(AGC_ENABLED){ sample = sample * AGC_GAIN; }

    if (sample > 32767) {
      sample = 32767;
    } else if (sample < -32767) {
      sample = -32767;
    }

    waveform[i] = sample;
    // Phase 2A: Replace waveform_history with AudioRawState buffer
    audio_raw_state.getCurrentHistoryFrame()[i] = waveform[i];

    uint32_t sample_abs = abs(sample);
    if (sample_abs > max_waveform_val_raw) {
      max_waveform_val_raw = sample_abs;
    }
  }

  // Apply smoothing to the raw max value
  const float smoothing_factor = 0.2; // Adjust as needed (lower = smoother)
  max_waveform_val_raw_smooth = (max_waveform_val_raw * smoothing_factor) + (max_waveform_val_raw_smooth * (1.0 - smoothing_factor));

  // Compute raw RMS for silence gating
  float raw_rms_frame = sqrtf(raw_sum_sq / CONFIG.SAMPLES_PER_CHUNK);
  raw_rms_global = raw_rms_frame;

  if (stream_audio) {
    USBSerial.print("sbs((audio=");
    for (uint16_t i = 0; i < CONFIG.SAMPLES_PER_CHUNK; i++) {
      USBSerial.print(waveform[i]);
      if (i < CONFIG.SAMPLES_PER_CHUNK - 1) {
        USBSerial.print(',');
      }
    }
    USBSerial.println("))");
  }

  if (!noise_complete) {
    // Calculate DC offset from raw sample, not processed waveform
    #ifdef ARDUINO_ESP32S3_DEV
    // For S3, use the raw sample before any processing
    audio_raw_state.getDCOffsetSum() += (audio_raw_state.getRawSamples()[0] >> 14);
    #else
    // For S2, apply the initial conversion but not sensitivity/DC removal
    int32_t raw_for_dc = (audio_raw_state.getRawSamples()[0] * 0.000512) + 56000 - 5120;
    audio_raw_state.getDCOffsetSum() += (raw_for_dc >> 2);
    #endif
    silent_scale = 1.0;  // Force LEDs on during calibration

    if (noise_iterations >= 64 && noise_iterations <= 192) {            // sample in the middle of noise cal
      if (max_waveform_val_raw * 1.10 > CONFIG.SWEET_SPOT_MIN_LEVEL) {  // Sweet Spot Min threshold should be the silence level + 15%
        CONFIG.SWEET_SPOT_MIN_LEVEL = max_waveform_val_raw * 1.10;
      }
    }
  } else {
    // Pre-calculate thresholds used multiple times
    float threshold_loud_break = CONFIG.SWEET_SPOT_MIN_LEVEL * 1.20;
    float dynamic_agc_floor_raw = float(min_silent_level_tracker);
    if (dynamic_agc_floor_raw < AGC_FLOOR_MIN_CLAMP_RAW) dynamic_agc_floor_raw = AGC_FLOOR_MIN_CLAMP_RAW;
    if (dynamic_agc_floor_raw > AGC_FLOOR_MAX_CLAMP_RAW) dynamic_agc_floor_raw = AGC_FLOOR_MAX_CLAMP_RAW;
    float dynamic_agc_floor_scaled = dynamic_agc_floor_raw * AGC_FLOOR_SCALING_FACTOR;
    if (dynamic_agc_floor_scaled < AGC_FLOOR_MIN_CLAMP_SCALED) dynamic_agc_floor_scaled = AGC_FLOOR_MIN_CLAMP_SCALED;
    if (dynamic_agc_floor_scaled > AGC_FLOOR_MAX_CLAMP_SCALED) dynamic_agc_floor_scaled = AGC_FLOOR_MAX_CLAMP_SCALED;
    float threshold_silence = dynamic_agc_floor_scaled;

    max_waveform_val = (max_waveform_val_raw - (CONFIG.SWEET_SPOT_MIN_LEVEL));

    if (max_waveform_val > max_waveform_val_follower) {
      float delta = max_waveform_val - max_waveform_val_follower;
      max_waveform_val_follower += delta * 0.4;  // Increased from 0.25 for faster response
    } else if (max_waveform_val < max_waveform_val_follower) {
      float delta = max_waveform_val_follower - max_waveform_val;
      max_waveform_val_follower -= delta * 0.02;  // Increased from 0.005 for faster decay

      if (max_waveform_val_follower < CONFIG.SWEET_SPOT_MIN_LEVEL) {
        max_waveform_val_follower = CONFIG.SWEET_SPOT_MIN_LEVEL;
      }
    }
    float waveform_peak_scaled_raw = max_waveform_val / max_waveform_val_follower;

    if (waveform_peak_scaled_raw > waveform_peak_scaled) {
      float delta = waveform_peak_scaled_raw - waveform_peak_scaled;
      waveform_peak_scaled += delta * 0.5;  // Increased from 0.25 for faster attack
    } else if (waveform_peak_scaled_raw < waveform_peak_scaled) {
      float delta = waveform_peak_scaled - waveform_peak_scaled_raw;
      waveform_peak_scaled -= delta * 0.5;  // Increased from 0.25 for faster decay
    }

    // Use the maximum amplitude of the captured frame to set
    // the Sweet Spot state. Think of this like a coordinate
    // space where 0 is the center LED, -1 is the left, and
    // +1 is the right. See run_sweet_spot() in led_utilities.h
    // for how this value translates to the final LED brightnesses

    int8_t potential_next_state = sweet_spot_state; // Assume current state initially

    // *** Use the SMOOTHED value for state decision ***
    if (max_waveform_val_raw_smooth <= threshold_silence) { // Use pre-calculated threshold
        potential_next_state = -1;
    } else if (max_waveform_val_raw_smooth >= CONFIG.SWEET_SPOT_MAX_LEVEL) {
        potential_next_state = 1;
    } else {
        potential_next_state = 0;
    }

    if (potential_next_state != sweet_spot_state) {
        if ((t_now - last_state_change_time) > MIN_STATE_DURATION_MS) {
            int8_t previous_state = sweet_spot_state;
            sweet_spot_state = potential_next_state;
            last_state_change_time = t_now;

            if (sweet_spot_state == -1) {
                silence_temp = true;
                silence_switched = t_now;

                if (previous_state != -1) {
                     // *** Use RAW value for deadband check ***
                     float agc_delta = threshold_silence - max_waveform_val_raw; // Use pre-calculated threshold
                     if (agc_delta > 50.0) {
                         min_silent_level_tracker = SQ15x16(AGC_FLOOR_INITIAL_RESET);
                        if (audio_debug_logging_enabled) {
                             USBSerial.print("DEBUG: AGC Floor Tracker Reset (deadband met): raw_val=");
                             USBSerial.print(max_waveform_val_raw);
                             USBSerial.print(" threshold=");
                             USBSerial.println(threshold_silence); // Use pre-calculated threshold
                         }
                     } else {
                        if (audio_debug_logging_enabled) {
                             USBSerial.print("DEBUG: AGC Floor Tracker not reset due to deadband, delta=");
                             USBSerial.println(agc_delta);
                         }
                     }
                }

               if (audio_debug_logging_enabled) {
                    USBSerial.println("DEBUG: Entered silent state (Hysteresis Passed)");
                    USBSerial.print("  max_waveform_val_raw: "); USBSerial.print(max_waveform_val_raw);
                    USBSerial.print("  MIN_LEVEL threshold: "); USBSerial.println(threshold_silence); // Use pre-calculated threshold
                }
            } else {
               if (audio_debug_logging_enabled) {
                   USBSerial.print("DEBUG: Entered ");
                   USBSerial.print(sweet_spot_state == 1 ? "loud" : "normal");
                   USBSerial.print(" state (Hysteresis Passed), delta=");
                   USBSerial.println(max_waveform_val_raw - threshold_silence); // Use pre-calculated threshold
                }
            }
        }
    }

    if (sweet_spot_state == -1) {
        SQ15x16 current_raw_level = SQ15x16(max_waveform_val_raw);
        if (current_raw_level < min_silent_level_tracker) {
            min_silent_level_tracker = current_raw_level;
        } else {
            min_silent_level_tracker += SQ15x16(AGC_FLOOR_RECOVERY_RATE);
            min_silent_level_tracker = fmin_fixed(min_silent_level_tracker, SQ15x16(AGC_FLOOR_INITIAL_RESET));
        }
        if (audio_debug_logging_enabled && (t_now % 1000 == 0)) {
             USBSerial.print("DEBUG (Silence): AGC Floor Tracker Value: "); USBSerial.println(float(min_silent_level_tracker));
         }
    }

    // *** Use RAW value for loud sound detection ***
    bool loud_sound_detected = (max_waveform_val_raw > threshold_loud_break); // Use pre-calculated threshold

    if (loud_sound_detected) {
        if (audio_debug_logging_enabled && silence) {
             USBSerial.println("DEBUG: Silence broken by loud sound");
        }
        silence = false;
        silence_temp = false;
        silence_switched = t_now;
    } else if (sweet_spot_state == -1) {
         silence_temp = true;
         if (t_now - silence_switched >= 10000) {
            if (audio_debug_logging_enabled && !silence) {
                USBSerial.println("DEBUG: Extended silence detected (10s)");
            }
            silence = true;
         }
    } else {
        silence = false;
        silence_temp = false;
    }

    if (audio_debug_logging_enabled && (t_now % 10000 == 0)) {
      USBSerial.print("DEBUG: silent_scale=");
      USBSerial.print(float(silent_scale));
      USBSerial.print(" silence=");
      USBSerial.print(silence ? "true" : "false");
      USBSerial.print(" sweet_spot_state=");
      USBSerial.println(sweet_spot_state);
    }

    if (CONFIG.STANDBY_DIMMING) {
      float silent_scale_raw = silence ? 0.0 : 1.0;
      silent_scale = silent_scale_raw * 0.1 + silent_scale_last * 0.9;
      silent_scale_last = silent_scale;
    } else {
      silent_scale = 1.0;
    }

    for (int i = 0; i < SAMPLE_HISTORY_LENGTH - CONFIG.SAMPLES_PER_CHUNK; i++) {
      sample_window[i] = sample_window[i + CONFIG.SAMPLES_PER_CHUNK];
    }
    for (int i = SAMPLE_HISTORY_LENGTH - CONFIG.SAMPLES_PER_CHUNK; i < SAMPLE_HISTORY_LENGTH; i++) {
      sample_window[i] = waveform[i - (SAMPLE_HISTORY_LENGTH - CONFIG.SAMPLES_PER_CHUNK)];
    }

    // Pre-calculate reciprocal for fixed-point conversion
    const SQ15x16 RECIP_32768 = SQ15x16(1.0 / 32768.0);
    for (uint16_t i = 0; i < CONFIG.SAMPLES_PER_CHUNK; i++) {
      // Convert using multiplication instead of division
      waveform_fixed_point[i] = SQ15x16(waveform[i]) * RECIP_32768;
    }

    sweet_spot_state_last = sweet_spot_state;

    if (audio_debug_logging_enabled && (t_now % 2000 == 0)) {
        USBSerial.print("DEBUG (State): sweet_spot_state="); USBSerial.print(sweet_spot_state);
        USBSerial.print(" | max_waveform_val_raw="); USBSerial.print(max_waveform_val_raw);
        USBSerial.print(" | silence_threshold="); USBSerial.println(threshold_silence); // Use pre-calculated threshold
    }
  }
}

void calculate_vu() {
  /*
    Calculates perceived audio loudness or Volume Unit (VU). Uses root mean square (RMS) method 
    for accurate representation of perceived loudness and incorporates a noise floor calibration.
    If calibration is active, updates noise floor level. If not, subtracts the noise floor from
    the calculated volume and normalizes the volume level.

    Parameters:
    - audio_samples[]: Audio samples to process.
    - sample_count: Number of samples in audio_samples array.

    Global variables:
    - audio_vu_level: Current VU level.
    - audio_vu_level_last: Last calculated VU level.
    - CONFIG.VU_LEVEL_FLOOR: Quietest level considered as audio signal.
    - audio_vu_level_average: Average of the current and the last VU level.
    - noise_cal_active: Indicator of active noise floor calibration.
    */

  // Silence gate using raw RMS before AGC
  static uint8_t silence_counter = 0;
  const float SILENCE_THRESHOLD = 0.01f; // ~ -40 dBFS after sensitivity
  if (raw_rms_global < SILENCE_THRESHOLD) {
      if (silence_counter < 20) silence_counter++; // ~160 ms at 125 FPS
  } else {
      silence_counter = 0;
  }
  if(!AGC_ENABLED){
      SILENCE_GATE_ACTIVE = false;
  } else {
      SILENCE_GATE_ACTIVE = (silence_counter >= 10); // 80 ms of silence
  }

  // Store last volume level
  audio_vu_level_last = audio_vu_level;

  float sum = 0.0;

  for (uint16_t i = 0; i < CONFIG.SAMPLES_PER_CHUNK; i++) {
    sum += float(waveform_fixed_point[i] * waveform_fixed_point[i]);
  }

  SQ15x16 rms = sqrt(float(sum / CONFIG.SAMPLES_PER_CHUNK));
  audio_vu_level = SILENCE_GATE_ACTIVE ? SQ15x16(0) : rms;

  // ---------------------- AGC COMPUTATION ---------------------------
  if(!AGC_ENABLED){
      AGC_GAIN = 1.0f; // bypass
  } else {
  static float rolling_rms = 0.05f;          // initial guess
  const float ROLL_ALPHA = 0.01f;            // ~1 s time-constant at 100 fps
  float rms_f = float(rms);
  rolling_rms = (rolling_rms * (1.0f - ROLL_ALPHA)) + (rms_f * ROLL_ALPHA);

  // Target RMS that maps to visual "0.2" brightness
  const float TARGET_RMS = 0.20f;
  float desired_gain = TARGET_RMS / rolling_rms;
  if (desired_gain < 0.5f) desired_gain = 0.5f;   // –6 dB floor
  if (desired_gain > 8.0f) desired_gain = 8.0f;   // +18 dB ceiling

  // Slew-limit gain changes to avoid pumping (0.05 per frame ~ 5 dB/s)
  if (desired_gain > AGC_GAIN)
      AGC_GAIN += fminf(desired_gain - AGC_GAIN, 0.05f);
  else
      AGC_GAIN -= fminf(AGC_GAIN - desired_gain, 0.05f);
  }
  // ------------------------------------------------------------------

  if (!noise_complete) {
    if (float(audio_vu_level * 1.5) > CONFIG.VU_LEVEL_FLOOR) {
      CONFIG.VU_LEVEL_FLOOR = float(audio_vu_level * 1.5);
    }
  } else {
    audio_vu_level -= CONFIG.VU_LEVEL_FLOOR;

    if (audio_vu_level < 0.0) {
      audio_vu_level = 0.0;
    }

    CONFIG.VU_LEVEL_FLOOR = min(0.99f, CONFIG.VU_LEVEL_FLOOR);
    audio_vu_level /= (1.0 - CONFIG.VU_LEVEL_FLOOR);
  }

  audio_vu_level_average = (audio_vu_level + audio_vu_level_last) / (2.0);
}
