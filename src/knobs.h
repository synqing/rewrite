/*----------------------------------------
  Sensory Bridge KNOB FUNCTIONS - PHYSICAL KNOBS DISABLED
----------------------------------------*/

uint16_t avg_read(uint8_t pin) {
  // Return fixed middle value instead of reading physical pins
  return 4096; // Middle value (8192/2)
}

void check_knobs(uint32_t t_now) {
  // PHYSICAL KNOBS COMPLETELY DISABLED
  // Function structure maintained for system stability
  
  static uint32_t iter = 0;
  static float PHOTONS_TARGET = 1.0;
  static float CHROMA_TARGET = 1.0;
  static float MOOD_TARGET = 1.0;

  static float PHOTONS_OUTPUT = 1.0;
  static float CHROMA_OUTPUT = 1.0;
  static float MOOD_OUTPUT = 1.0;

  static float PHOTONS_TARGET_LAST = 1.0;
  static float CHROMA_TARGET_LAST = 1.0;
  static float MOOD_TARGET_LAST = 1.0;

  iter++;

  // Process knob speed and update knob structs
  knob_photons.value = CONFIG.PHOTONS;
  knob_chroma.value = CONFIG.CHROMA;
  knob_mood.value = CONFIG.MOOD;

  SQ15x16 speed_threshold = 0.005;

  knob_photons.change_rate = fabs_fixed(knob_photons.value - knob_photons.last_value);
  knob_chroma.change_rate = fabs_fixed(knob_chroma.value - knob_chroma.last_value);
  knob_mood.change_rate = fabs_fixed(knob_mood.value - knob_mood.last_value);

  if (knob_photons.change_rate > speed_threshold) { knob_photons.last_change = t_now; }
  if (knob_chroma.change_rate > speed_threshold) { knob_chroma.last_change = t_now; }
  if (knob_mood.change_rate > speed_threshold) { knob_mood.last_change = t_now; }

  uint16_t knob_timeout_ms = 1500;

  // DISABLE UI VISUAL FEEDBACK: Always set most_recent_knob to K_NONE
  // This effectively disables the LED visual feedback while keeping knob functionality
  uint32_t most_recent_time = 0;
  uint8_t most_recent_knob = K_NONE;
  
  // Always set current_knob to K_NONE to disable UI feedback
  current_knob = K_NONE;

  knob_photons.last_value = knob_photons.value;
  knob_chroma.last_value = knob_chroma.value;
  knob_mood.last_value = knob_mood.value;

  // Required for system - continue to update the values needed by other components
  chroma_val = 1.0;
  if (CONFIG.CHROMA < 0.95) {
    chroma_val = CONFIG.CHROMA * 1.05263157;  // Reciprocal of 0.95 above
    chromatic_mode = false;
  } else {
    chromatic_mode = true;
  }

  // Calculate smoothing values from MOOD (needed by other parts of the system)
  float smoothing_top_half = (CONFIG.MOOD - 0.5);
  if (smoothing_top_half < 0.0) {
    smoothing_top_half = 0.0;
  }
  smoothing_top_half *= 2.0;

  float smoothing_bottom_half = (CONFIG.MOOD - 0.5);
  smoothing_bottom_half *= 2.0;
  if (smoothing_bottom_half > 0.0) {
    smoothing_bottom_half = 0.0;
  }
  smoothing_bottom_half *= -1.0;
  smoothing_bottom_half = 1.0 - smoothing_bottom_half;
  smoothing_bottom_half = (smoothing_bottom_half * 0.9) + 0.1;

  // Update the final smoothing algorithm values
  smoothing_follower = 0.100 + (smoothing_top_half * 0.300);
  smoothing_exp_average = 1.0 - smoothing_bottom_half;
}