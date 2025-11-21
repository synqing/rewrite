#ifndef ENCODERS_H
#define ENCODERS_H

#include <Arduino.h>
#include <Wire.h>
#include <stdint.h>
#include <FixedPoints.h>
#include <FixedPointsCommon.h>
#include <m5rotate8.h>
#include <USB.h>
#include <math.h> // For fabs
#include "constants.h" // For NUM_MODES
#include "globals.h"   // For conf, KNOB, other externs


// Forward declarations if needed (assuming defined elsewhere)
// enum lightshow_modes : uint8_t; // Assuming defined in lightshow_modes.h or similar
// struct conf; // Already defined in globals.h
struct KNOB;
void attempt_rotate8_init(bool verbose);


extern M5ROTATE8 rotate8;
extern uint32_t g_last_encoder_activity_time;
extern uint8_t g_last_active_encoder;
// extern conf CONFIG; // Already defined in globals.h
extern bool settings_updated;
extern bool debug_mode;
// extern bool mode_transition_queued; // No longer directly set here
extern uint32_t next_save_time;
// extern enum lightshow_modes; // Declaration moved or assumed included
extern KNOB knob_photons;
extern KNOB knob_chroma;
extern KNOB knob_mood;

// New global variables for encoder state management
extern bool g_rotate8_available; // Track whether the encoder is available
extern uint32_t g_next_recovery_attempt; // When to attempt reconnection
extern bool encoder3_in_contrast_mode; // Track when encoder 3 is in contrast mode
extern uint32_t encoder3_button_hold_start; // For detecting long-press

// Move these to file scope if needed by multiple functions:
static bool encoder_error_state = false;
static uint32_t error_recovery_time = 0;

// --- Fixed-Point Type Alias ---
using ConfigFixed = SQ15x16; // Alias for the fixed-point type used in config

void init_encoders();
void check_encoders(uint32_t t_now);
void update_encoder_leds();

void init_encoders() {
    Wire.begin(ENCODER_SDA_PIN, ENCODER_SCL_PIN);
    delay(100);
    attempt_rotate8_init(true);
}

void check_encoders(uint32_t t_now) {
    static uint32_t last_unavailable_warning = 0;
    static uint32_t last_heartbeat = 0;

    if (!g_rotate8_available) {
        // Log warning every 5 seconds if encoders are unavailable
        if (t_now - last_unavailable_warning > 5000) {
            USBSerial.println("WARNING: Encoders unavailable - g_rotate8_available is false");
            last_unavailable_warning = t_now;
        }
        return;
    }

    // Heartbeat every 10 seconds to confirm encoder task is running
    if (t_now - last_heartbeat > 10000) {
        USBSerial.println("[ENCODER] Heartbeat - encoder task running and available");
        last_heartbeat = t_now;
    }

    // Removed timing debug

    uint8_t sw = rotate8.inputSwitch();
    bool secondaryMode = (sw == 1);

    // --- Fixed-Point Sensitivity Divisors ---
    // Adjust these values as needed for sensitivity tuning with fixed-point
    const ConfigFixed sensitivity_divisor = ConfigFixed(120.0); // Use constructor for float conversion
    const ConfigFixed sensitivity_divisor_prism = ConfigFixed(8.0);
    const ConfigFixed sensitivity_divisor_contrast = ConfigFixed(25.0);
    const ConfigFixed prism_threshold = ConfigFixed(0.05);

    // --- Fixed-Point Limits ---
    const ConfigFixed limit_0_0 = ConfigFixed(0.0);
    const ConfigFixed limit_1_0 = ConfigFixed(1.0);
    const ConfigFixed limit_5_0 = ConfigFixed(5.0);
    const ConfigFixed limit_8_0 = ConfigFixed(8.0);


    static uint32_t last_encoder_check = 0;
    static bool encoder3_button_last_state = false;
    static uint32_t encoder_error_count = 0;
    static int32_t last_encoder_values[8] = {0};
    static int32_t accumulated_values[8] = {0};

    static uint32_t last_encoder_change_time[8] = {0};
    static uint8_t last_active_encoder_id = 255;
    static const uint32_t encoder_lockout_time = 50;

    bool activity_detected = false;

    if (t_now - last_encoder_check < 20) {
        return;
    }
    last_encoder_check = t_now;

    if (encoder_error_state) {
        if (t_now - error_recovery_time < 1000) {
            return;
        }

        g_rotate8_available = false;
        attempt_rotate8_init(false);

        if (!g_rotate8_available) {
            error_recovery_time = t_now;
            g_next_recovery_attempt = t_now + 10000;
            return;
        }

        encoder_error_state = false;
        encoder_error_count = 0;
        for (int i = 0; i < 8; i++) {
            accumulated_values[i] = 0;
            last_encoder_values[i] = 0;
            last_encoder_change_time[i] = 0;
        }
        last_active_encoder_id = 255;
        USBSerial.println("M5Rotate8 recovered via check_encoders.");
    }

    auto safeGetRelCounter = [&](uint8_t channel) -> int32_t {
        int32_t value = 0;
        bool read_successful = false;

        if (last_active_encoder_id != 255 &&
            last_active_encoder_id != channel &&
            (t_now - last_encoder_change_time[last_active_encoder_id] < encoder_lockout_time)) {
            return 0;
        }

        value = rotate8.getRelCounter(channel);
        read_successful = true;

        if (read_successful) {
            if (value > 40 || value < -40) {
                encoder_error_count++;
                value = 0;
            } else if (value != 0) {
                accumulated_values[channel] += value;

                if (accumulated_values[channel] > 100) accumulated_values[channel] = 100;
                if (accumulated_values[channel] < -100) accumulated_values[channel] = -100;

                last_encoder_change_time[channel] = t_now;
                last_active_encoder_id = channel;

                value = accumulated_values[channel];
                accumulated_values[channel] = 0;

                encoder_error_count = 0;
            } else {
                // Value is 0, potentially reset accumulator if needed, but currently reset on non-zero read.
                // Reset error counter if value is 0? Maybe not, could mask intermittent issues.
            }
        } else {
            // Treat read failure as an error (if we had a way to detect it explicitly)
            // encoder_error_count++;
            // value = 0;
        }

        if (encoder_error_count > 5) {
            encoder_error_state = true;
            error_recovery_time = t_now;
            g_rotate8_available = false;
            g_next_recovery_attempt = t_now + 5000;
            USBSerial.println("WARNING: Encoder communication errors detected. Entering recovery mode.");
            for (int i = 0; i < 8; i++) {
                accumulated_values[i] = 0;
            }
            return 0;
        }

        return value;
    };

    static uint32_t last_debug_time = 0;
    auto debugEncoder = [&](uint8_t channel, int32_t value, const char* name, ConfigFixed new_value) {
        if (value != 0 && (t_now - last_debug_time) > 100) {
            USBSerial.print("[ENCODER E");
            USBSerial.print(channel);
            USBSerial.print("] Raw: ");
            USBSerial.print(value);
            USBSerial.print(" | New ");
            USBSerial.print(name);
            USBSerial.print(": ");
            USBSerial.println(float(new_value)); // Cast fixed-point to float for printing
            last_debug_time = t_now;
        }
    };

    // Channel 0: Photons
    int32_t photons_rel = safeGetRelCounter(0);
    if (photons_rel != 0) {
        ConfigFixed change = ConfigFixed(photons_rel) / sensitivity_divisor; // Fixed-point division
        ConfigFixed current_val = secondaryMode ? SECONDARY_PHOTONS : CONFIG.PHOTONS; // Assumes these are ConfigFixed
        ConfigFixed new_value = current_val + change; // Fixed-point addition

        // Constrain using fixed-point values
        if (new_value > limit_1_0) new_value = limit_1_0;
        if (new_value < limit_0_0) new_value = limit_0_0;

        // Update only if the constrained value is different (prevents unnecessary saves)
        if (new_value != current_val) {
             activity_detected = true;
             g_last_active_encoder = 0;
             if (!secondaryMode) CONFIG.PHOTONS = float(new_value);
             else SECONDARY_PHOTONS = float(new_value);
             settings_updated = true;
             debugEncoder(0, photons_rel, "PHOTONS", new_value);
        }
    }

    // Channel 1: Chroma
    int32_t chroma_rel = safeGetRelCounter(1);
    if (chroma_rel != 0) {
        ConfigFixed change = ConfigFixed(chroma_rel) / sensitivity_divisor;
        ConfigFixed current_val = secondaryMode ? SECONDARY_CHROMA : CONFIG.CHROMA;
        ConfigFixed new_value = current_val + change;

        if (new_value > limit_1_0) new_value = limit_1_0;
        if (new_value < limit_0_0) new_value = limit_0_0;

        if (new_value != current_val) {
            activity_detected = true;
            g_last_active_encoder = 1;
            if (!secondaryMode) CONFIG.CHROMA = float(new_value);
            else SECONDARY_CHROMA = float(new_value);
            settings_updated = true;
            debugEncoder(1, chroma_rel, "CHROMA", new_value);
        }
    }

    // Channel 2: Mood
    int32_t mood_rel = safeGetRelCounter(2);
    if (mood_rel != 0) {
        ConfigFixed change = ConfigFixed(mood_rel) / sensitivity_divisor;
        ConfigFixed current_val = secondaryMode ? SECONDARY_MOOD : CONFIG.MOOD;
        ConfigFixed new_value = current_val + change;

        if (new_value > limit_1_0) new_value = limit_1_0;
        if (new_value < limit_0_0) new_value = limit_0_0;

        if (new_value != current_val) {
            activity_detected = true;
            g_last_active_encoder = 2;
            if (!secondaryMode) CONFIG.MOOD = float(new_value);
            else SECONDARY_MOOD = float(new_value);
            settings_updated = true;
            debugEncoder(2, mood_rel, "MOOD", new_value);
        }
    }

    bool encoder3_button_state = false;

    static uint32_t last_button_press_time = 0;
    static const uint32_t button_debounce_time = 400;
    static const uint32_t long_press_threshold = 800;

    if (t_now - last_button_press_time > button_debounce_time) {
        encoder3_button_state = rotate8.getKeyPressed(3);

        if (encoder3_button_state && !encoder3_button_last_state) {
            encoder3_button_hold_start = t_now;
            encoder3_button_last_state = true;
        }
        else if (!encoder3_button_state && encoder3_button_last_state) {
            encoder3_button_last_state = false;

            if (t_now - encoder3_button_hold_start >= long_press_threshold) {
                encoder3_in_contrast_mode = !encoder3_in_contrast_mode;
                activity_detected = true;
                g_last_active_encoder = 3;
                last_button_press_time = t_now;

                if(debug_mode){
                    USBSerial.print("[DBG E3] Long Press | Contrast Mode: ");
                    USBSerial.println(encoder3_in_contrast_mode ? "ON" : "OFF");
                }
            }
            else {
                activity_detected = true;
                g_last_active_encoder = 3;

                if (encoder3_in_contrast_mode) {
                    // Reset contrast using fixed-point literal, cast back to uint8_t
                    // (Workaround: Ideally change CONFIG type)
                    CONFIG.SQUARE_ITER = uint8_t(int(limit_1_0)); // Reset to 1.0 (assuming target is uint8_t)
                    settings_updated = true;
                    if(debug_mode){
                        USBSerial.print("[DBG E3] Short Press | Reset Contrast to: ");
                        USBSerial.println(float(CONFIG.SQUARE_ITER)); // Print float representation
                    }
                } else {
                    // Mode change logic remains integer based
                    if (!secondaryMode) CONFIG.LIGHTSHOW_MODE = (CONFIG.LIGHTSHOW_MODE + 1) % NUM_MODES;
                    else SECONDARY_LIGHTSHOW_MODE = (SECONDARY_LIGHTSHOW_MODE + 1) % NUM_MODES;
                    settings_updated = true;
                    if(debug_mode){
                        USBSerial.print("[DBG E3] Short Press | New Light Mode: ");
                        USBSerial.println(secondaryMode ? SECONDARY_LIGHTSHOW_MODE : CONFIG.LIGHTSHOW_MODE);
                    }
                    // ALWAYS log mode changes for debugging
                    USBSerial.printf("MODE CHANGE: New mode index=%d (Expected: SNAPWAVE=%d, SNAPWAVE_DEBUG=%d)\n", 
                                    CONFIG.LIGHTSHOW_MODE, LIGHT_MODE_SNAPWAVE, LIGHT_MODE_SNAPWAVE_DEBUG);
                }
                last_button_press_time = t_now;
            }
        }
    }
    else if (encoder3_button_state && encoder3_button_last_state) {
        // Button is still held, do nothing until release or long press threshold met
    }
    else if (!encoder3_button_state && encoder3_button_last_state) {
        // Button was released *during* the debounce lockout - handle it now as a short press potentially missed?
        // Or ignore it, assuming the user intended to release quickly. Let's ignore for simplicity now.
        // Could add logic here if short presses feel unresponsive.
    }

    if (encoder3_in_contrast_mode) {
        int32_t contrast_rel = safeGetRelCounter(3);
        if (contrast_rel != 0) {
            ConfigFixed contrast_change = ConfigFixed(contrast_rel) / sensitivity_divisor_contrast;
            ConfigFixed current_val = CONFIG.SQUARE_ITER; // Assumes this is ConfigFixed
            ConfigFixed new_value = current_val + contrast_change;

            // Constrain using fixed-point values
            if (new_value > limit_5_0) new_value = limit_5_0;
            if (new_value < limit_0_0) new_value = limit_0_0;

            if (new_value != current_val) {
                activity_detected = true;
                g_last_active_encoder = 3;
                CONFIG.SQUARE_ITER = uint8_t(int(new_value)); // Assuming target is uint8_t
                settings_updated = true;
                if(debug_mode) {
                    USBSerial.print("[DBG E3 ROT] Raw: "); USBSerial.print(contrast_rel);
                    USBSerial.print(" | New Contrast: "); USBSerial.println(float(CONFIG.SQUARE_ITER)); // Print float
                }
            }
        }
    }

    // Channel 4: Saturation
    int32_t sat_rel = safeGetRelCounter(4);
    if (sat_rel != 0) {
        ConfigFixed change = ConfigFixed(sat_rel) / sensitivity_divisor;
        ConfigFixed current_val = secondaryMode ? SECONDARY_SATURATION : CONFIG.SATURATION;
        ConfigFixed new_val = current_val + change;

        if (new_val > limit_1_0) new_val = limit_1_0;
        if (new_val < limit_0_0) new_val = limit_0_0;

        if (new_val != current_val) {
            activity_detected = true;
            g_last_active_encoder = 4;
            if (!secondaryMode) CONFIG.SATURATION = float(new_val);
            else SECONDARY_SATURATION = float(new_val);
            settings_updated = true;
            debugEncoder(4, sat_rel, "SATURATION", new_val);
        }
    }

    // Channel 5: Prism count
    int32_t prism_rel = safeGetRelCounter(5);
    if (prism_rel != 0) {
        ConfigFixed change = ConfigFixed(prism_rel) / sensitivity_divisor_prism;
        // Manual absolute value for fixed-point
        if ((change < limit_0_0 ? -change : change) >= prism_threshold) {
            ConfigFixed current_val = secondaryMode ? SECONDARY_PRISM_COUNT : CONFIG.PRISM_COUNT;
            ConfigFixed new_val = current_val + change;

            if (new_val > limit_8_0) new_val = limit_8_0;
            if (new_val < limit_0_0) new_val = limit_0_0;

            if (new_val != current_val) {
                activity_detected = true;
                g_last_active_encoder = 5;
                 if (!secondaryMode) CONFIG.PRISM_COUNT = float(new_val);
                 else SECONDARY_PRISM_COUNT = uint8_t(int(new_val));
                 settings_updated = true;
                 debugEncoder(5, prism_rel, "PRISM_COUNT", new_val);
            }
        }
    }

    // Channel 6: Incandescent filter
    int32_t inc_rel = safeGetRelCounter(6);
    if (inc_rel != 0) {
        ConfigFixed change = ConfigFixed(inc_rel) / sensitivity_divisor;
        ConfigFixed current_val = secondaryMode ? SECONDARY_INCANDESCENT_FILTER : CONFIG.INCANDESCENT_FILTER;
        ConfigFixed new_val = current_val + change;

        if (new_val > limit_1_0) new_val = limit_1_0;
        if (new_val < limit_0_0) new_val = limit_0_0;

        if (new_val != current_val) {
            activity_detected = true;
            g_last_active_encoder = 6;
            if (!secondaryMode) CONFIG.INCANDESCENT_FILTER = float(new_val);
            else SECONDARY_INCANDESCENT_FILTER = float(new_val);
            settings_updated = true;
            debugEncoder(6, inc_rel, "INCANDESCENT", new_val);
        }
    }

    // Channel 7: Bulb opacity (Does not have a secondary equivalent)
    int32_t bulb_rel = safeGetRelCounter(7);
    if (bulb_rel != 0) {
        ConfigFixed change = ConfigFixed(bulb_rel) / sensitivity_divisor;
        ConfigFixed current_val = CONFIG.BULB_OPACITY; // Assumes this is ConfigFixed
        ConfigFixed new_value = current_val + change;

        if (new_value > limit_1_0) new_value = limit_1_0;
        if (new_value < limit_0_0) new_value = limit_0_0;

        if (new_value != current_val) {
            activity_detected = true;
            g_last_active_encoder = 7;
            CONFIG.BULB_OPACITY = float(new_value);
            settings_updated = true;
            debugEncoder(7, bulb_rel, "BULB_OPACITY", new_value);
        }
    }

    if (activity_detected) {
        g_last_encoder_activity_time = t_now;
        next_save_time = t_now + 3000;
    }

    if (activity_detected) {
        if (g_last_active_encoder == 0) knob_photons.last_change = g_last_encoder_activity_time;
        if (g_last_active_encoder == 1) knob_chroma.last_change = g_last_encoder_activity_time;
        if (g_last_active_encoder == 2) knob_mood.last_change = g_last_encoder_activity_time;
    }
    // Removed timing debug
}

void update_encoder_leds() {
    if (!g_rotate8_available) return;

    static const uint8_t active_colors[8][3] = {
        {64, 64, 64},
        {0, 128, 128},
        {128, 128, 0},
        {0, 128, 0},
        {128, 0, 128},
        {192, 192, 0},
        {0, 128, 128},
        {192, 96, 0}
    };

    static uint8_t contrast_indicator = 0;
    static uint32_t last_pulse_time = 0;
    static bool pulse_direction = true;
    static const uint32_t pulse_interval = 20;

    if (encoder3_in_contrast_mode && (millis() - last_pulse_time > pulse_interval)) {
        if (pulse_direction) {
            contrast_indicator = min(contrast_indicator + 8, 128);
            if (contrast_indicator == 128) pulse_direction = false;
        } else {
            contrast_indicator = max(contrast_indicator - 8, 10);
            if (contrast_indicator == 10) pulse_direction = true;
        }
        last_pulse_time = millis();
    }

    static uint8_t inactive_r = 0;
    static uint8_t inactive_g = 0;
    static uint8_t inactive_b = 4;

    static uint32_t last_led_update_check_time = 0;
    static uint8_t current_active_encoder = 255;
    static uint8_t last_written_states[9] = {255};
    static const uint32_t led_update_interval = 100;

    uint32_t current_millis = millis();
    if (current_millis - last_led_update_check_time > led_update_interval) {
        last_led_update_check_time = current_millis;

        const uint32_t active_timeout = 2000;
        if (current_millis - g_last_encoder_activity_time < active_timeout) {
            current_active_encoder = g_last_active_encoder;
        } else {
            current_active_encoder = 255;
            encoder3_in_contrast_mode = false;
        }

        for (uint8_t i = 0; i < 8; i++) {
            uint8_t desired_state;

            if (i == 3 && encoder3_in_contrast_mode) {
                desired_state = 2;
            } else if (i == current_active_encoder) {
                desired_state = 1;
            } else {
                desired_state = 0;
            }

            if (desired_state != last_written_states[i]) {
                bool write_success = false;
                if (desired_state == 2) {
                    write_success = rotate8.writeRGB(i, 0, contrast_indicator, 0);
                } else if (desired_state == 1) {
                    write_success = rotate8.writeRGB(i, active_colors[i][0], active_colors[i][1], active_colors[i][2]);
                } else {
                    write_success = rotate8.writeRGB(i, inactive_r, inactive_g, inactive_b);
                }

                if (write_success) {
                    last_written_states[i] = desired_state;
                } else {
                    g_rotate8_available = false;
                    g_next_recovery_attempt = current_millis + 5000;
                    encoder_error_state = true;
                    error_recovery_time = current_millis;
                    USBSerial.println("WARNING: Encoder LED write error. Entering recovery mode.");
                    return;
                }
            }
        }

        if (last_written_states[8] != 0) {
            bool write_success = rotate8.writeRGB(8, 0, 0, 0);
            if (write_success) {
                last_written_states[8] = 0;
            } else {
                g_rotate8_available = false;
                g_next_recovery_attempt = current_millis + 5000;
                encoder_error_state = true;
                error_recovery_time = current_millis;
                USBSerial.println("WARNING: Encoder LED 8 write error. Entering recovery mode.");
            }
        }
    }
}

#endif
