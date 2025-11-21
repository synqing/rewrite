#include <Arduino.h>
#include <FastLED.h>
#include <FixedPoints.h>
#include "constants.h"
#include "globals.h"
// #include "led_utilities.h" // Removed to prevent multiple definition errors

// Cached CONFIG values for the current frame
struct cached_config {
  float PHOTONS;
  float CHROMA;
  float MOOD;
  uint8_t LIGHTSHOW_MODE;
  float SQUARE_ITER;
  float SATURATION;
} frame_config;

extern bool snapwave_debug_logging_enabled;
extern bool snapwave_color_debug_logging_enabled;
extern bool color_shift_debug_logging_enabled;

// Cache CONFIG values at start of frame (single-core = no mutex needed)
void cache_frame_config() {
  // SINGLE-CORE OPTIMIZATION: Direct access (both threads on Core 0)
  frame_config.PHOTONS = CONFIG.PHOTONS;
  frame_config.CHROMA = CONFIG.CHROMA;
  frame_config.MOOD = CONFIG.MOOD;
  frame_config.LIGHTSHOW_MODE = CONFIG.LIGHTSHOW_MODE;
  frame_config.SQUARE_ITER = CONFIG.SQUARE_ITER;
  frame_config.SATURATION = CONFIG.SATURATION;
}

void get_smooth_spectrogram() {
  static SQ15x16 spectrogram_smooth_last[NUM_FREQS];
  
  static uint32_t last_timing_print = 0;
  if (millis() - last_timing_print > 1000) {  // Only print once per second
    // USBSerial.printf("TIMING|%lu|SPECTRUM_READ|||\n", micros()); // Disabled to prevent memory issues
    last_timing_print = millis();
  }

  // SINGLE-CORE OPTIMIZATION: No mutex needed (both threads on Core 0)
  for (uint8_t bin = 0; bin < NUM_FREQS; bin++) {
    SQ15x16 note_brightness = spectrogram[bin];

    if (spectrogram_smooth[bin] < note_brightness) {
      SQ15x16 distance = note_brightness - spectrogram_smooth[bin];
      spectrogram_smooth[bin] += distance * SQ15x16(0.5);  // Faster attack for rising values

    } else if (spectrogram_smooth[bin] > note_brightness) {
      SQ15x16 distance = spectrogram_smooth[bin] - note_brightness;
      spectrogram_smooth[bin] -= distance * SQ15x16(0.25);  // Slower decay for falling values
    }
  }
}

CRGB calc_chromagram_color() {
  CRGB sum_color = CRGB(0, 0, 0);
  for (uint8_t i = 0; i < 12; i++) {
    float prog = i / 12.0;

    float bright = note_chromagram[i];
    for (uint8_t s = 0; s < frame_config.SQUARE_ITER + 1; s++) {
      bright *= bright;
    }
    bright *= 0.5;

    if (bright > 1.0) {
      bright = 1.0;
    }

    if (chromatic_mode == true) {
      CRGB out_col = CHSV(uint8_t(prog * 255), uint8_t(frame_config.SATURATION * 255), uint8_t(bright * 255));
      sum_color += out_col;
    }
  }

  if (chromatic_mode == false) {
    sum_color = force_saturation(sum_color, uint8_t(frame_config.SATURATION * 255));
  }

  return sum_color;
}

void avg_bins(uint8_t low_bin, uint8_t high_bin) {
  // TBD
}

void test_mode() {
  static float radians = 0.00;
  radians += frame_config.MOOD;
  float position = sin(radians) * 0.5 + 0.5;
  set_dot_position(RESERVED_DOTS + 0, position);
  clear_leds();
  draw_dot(leds_16, RESERVED_DOTS + 0, hsv(chroma_val, frame_config.SATURATION, frame_config.PHOTONS * frame_config.PHOTONS));
}

// Default mode!
void light_mode_gdft() {
  // Calculate frequency data for the first half of the strip
  for (uint16_t i = 0; i < (NATIVE_RESOLUTION / 2); i++) {
    // Map the 64 frequency bins across the first half (NATIVE_RESOLUTION / 2 LEDs)
    SQ15x16 freq_prog = (SQ15x16)i / (SQ15x16)(NATIVE_RESOLUTION / 2);
    SQ15x16 freq_index_f = freq_prog * (NUM_FREQS - 1);
    uint16_t freq_index_i = freq_index_f.getInteger();
    SQ15x16 freq_fract = freq_index_f - freq_index_i;

    // Interpolate between adjacent frequency bins
    SQ15x16 bin1 = spectrogram_smooth[freq_index_i];
    SQ15x16 bin2 = spectrogram_smooth[freq_index_i + 1 < NUM_FREQS ? freq_index_i + 1 : freq_index_i]; // Handle edge case
    SQ15x16 bin = bin1 * (1.0 - freq_fract) + bin2 * freq_fract;

    if (bin > 1.0) { bin = 1.0; }

    uint8_t base_iters = (uint8_t)frame_config.SQUARE_ITER;
    float fract_iter = frame_config.SQUARE_ITER - base_iters;

    uint8_t extra_iters = 0;
    if (chromatic_mode == true) {
      extra_iters = 1;
    }

    // Apply full iterations first
    for (uint8_t s = 0; s < base_iters + extra_iters; s++) {
      bin = (bin * bin) * SQ15x16(0.65) + (bin * SQ15x16(0.35));
    }

    // Apply fractional iteration if needed
    if (fract_iter > 0.01) {
      SQ15x16 squared = (bin * bin) * SQ15x16(0.65) + (bin * SQ15x16(0.35));
      bin = bin * (1.0 - fract_iter) + squared * fract_iter;
    }

    SQ15x16 led_hue;
    SQ15x16 prog = (SQ15x16)i / (SQ15x16)(NATIVE_RESOLUTION / 2); // Use LED position for hue progression
    if (chromatic_mode == true) {
      // Interpolate note colors across the half-strip based on frequency index
       SQ15x16 color_prog = (SQ15x16)(freq_index_i % 12) / 12.0;
       SQ15x16 next_color_prog = (SQ15x16)((freq_index_i + 1) % 12) / 12.0;
       SQ15x16 hue1 = note_colors[freq_index_i % 12];
       SQ15x16 hue2 = note_colors[(freq_index_i + 1) % 12];
       // Handle hue wrap-around during interpolation
       if (fabs_fixed(hue1 - hue2) > 0.5) { // Detect wrap around 0.0/1.0
          if (hue1 < hue2) hue1 += 1.0; else hue2 += 1.0;
       }
       led_hue = hue1 * (1.0 - freq_fract) + hue2 * freq_fract;
       if (led_hue >= 1.0) led_hue -= 1.0; // Normalize back to 0-1 range

    } else {
      // Use frame_config.CHROMA directly
      led_hue = frame_config.CHROMA + hue_position + ((sqrt(float(bin)) * SQ15x16(0.05)) + (prog * SQ15x16(0.10)) * hue_shifting_mix);
    }

    // Place calculated color in the second half of the buffer initially
    leds_16[i + (NATIVE_RESOLUTION / 2)] = hsv(led_hue + bin * SQ15x16(0.050), frame_config.SATURATION, bin);
  }

  // Clear the first half before mirroring
  memset(leds_16, 0, sizeof(CRGB16) * (NATIVE_RESOLUTION / 2));

  // No shift needed, just mirror the calculated second half to the first half
  mirror_image_downwards(leds_16);  // (led_utilities.h) Mirror downwards
}

/*
void light_mode_gdft_chromagram() {
  for (uint16_t i = 0; i < NATIVE_RESOLUTION; i++) {
    float prog = i / float(NATIVE_RESOLUTION);

    float bin = interpolate(prog, note_chromagram, 12) * 1.25;
    if (bin > 1.0) { bin = 1.0; };

    for (uint8_t s = 0; s < CONFIG.SQUARE_ITER + 1; s++) {
      bin = bin * bin;
    }

    bin *= 1.0 - CONFIG.BACKDROP_BRIGHTNESS;
    bin += CONFIG.BACKDROP_BRIGHTNESS;

    float led_brightness_raw = 254 * bin;  // -1 for temporal dithering below
    uint16_t led_brightness = led_brightness_raw;
    float fract = led_brightness_raw - led_brightness;

    if (CONFIG.TEMPORAL_DITHERING == true) {
      if (fract >= dither_table[dither_step]) {
        led_brightness += 1;
      }
    }

    float led_hue;
    if (chromatic_mode == true) {
      //led_hue = 255 * prog;
    } else {
      //led_hue = 255 * chroma_val + (i >> 1) + hue_shift;
    }

    //leds[i] = CHSV(led_hue + hue_shift, 255 * CONFIG.SATURATION, led_brightness);
  }
}
*/

/*
void light_mode_bloom(bool fast_scroll) {
  static uint32_t iter = 0;
  const float led_share = 1.0 / 12.0;
  iter++;

  if (bitRead(iter, 0) == 0) {
    CRGB sum_color = calc_chromagram_color();

    //sum_color = force_saturation(sum_color, 255 * CONFIG.SATURATION);

    if (fast_scroll == true) {  // Fast mode scrolls two LEDs at a time
      for (uint8_t i = 0; i < NATIVE_RESOLUTION - 2; i++) {
        leds_fx[(NATIVE_RESOLUTION - 1) - i] = leds_last[(NATIVE_RESOLUTION - 1) - i - 2];
      }

      leds_fx[0] = sum_color;  // New information goes here
      leds_fx[1] = sum_color;  // New information goes here

    } else {  // Slow mode only scrolls one LED at a time
      for (uint8_t i = 0; i < NATIVE_RESOLUTION - 1; i++) {
        leds_fx[(NATIVE_RESOLUTION - 1) - i] = leds_last[(NATIVE_RESOLUTION - 1) - i - 1];
      }

      leds_fx[0] = sum_color;  // New information goes here
    }

    load_leds_from_fx();
    save_leds_to_last();

    //fadeToBlackBy(leds, 128, 255-255*waveform_peak_scaled);

    distort_logarithmic();
    //distort_exponential();

    fade_top_half(CONFIG.MIRROR_ENABLED);  // fade at different location depending if mirroring is enabled
    //increase_saturation(32);

    save_leds_to_aux();
  } else {
    load_leds_from_aux();
  }
}
*/

void light_mode_vu_dot() {
  static SQ15x16 dot_pos_last = 0.0;
  static SQ15x16 audio_vu_level_smooth = 0.0;
  static SQ15x16 max_level = 0.01;

  SQ15x16 mix_amount = mood_scale(0.10, 0.05);

  audio_vu_level_smooth = (audio_vu_level_average * mix_amount) + (audio_vu_level_smooth * (1.0 - mix_amount));

  if (audio_vu_level_smooth * 1.1 > max_level) {
    SQ15x16 distance = (audio_vu_level_smooth * 1.1) - max_level;
    max_level += distance *= 0.1;
  } else {
    max_level *= 0.9999;
    if (max_level < 0.0025) {
      max_level = 0.0025;
    }
  }
  SQ15x16 multiplier = 1.0 / max_level;

  SQ15x16 dot_pos = (audio_vu_level_smooth * multiplier);

  if (dot_pos > 1.0) {
    dot_pos = 1.0;
  }

  SQ15x16 mix = mood_scale(0.25, 0.24);
  SQ15x16 dot_pos_smooth = (dot_pos * mix) + (dot_pos_last * (1.0-mix));
  dot_pos_last = dot_pos_smooth;

  SQ15x16 brightness = sqrt(float(dot_pos_smooth));

  set_dot_position(RESERVED_DOTS + 0, dot_pos_smooth * 0.5 + 0.5);
  set_dot_position(RESERVED_DOTS + 1, 0.5 - dot_pos_smooth * 0.5);

  clear_leds();
  //fade_grayscale(0.15);

  SQ15x16 hue = chroma_val + hue_position;
  CRGB16 color = hsv(hue, CONFIG.SATURATION, brightness);
  draw_dot(leds_16, RESERVED_DOTS + 0, color);
  draw_dot(leds_16, RESERVED_DOTS + 1, color);
}

void light_mode_kaleidoscope() {
  static float pos_r = 0.0;
  static float pos_g = 0.0;
  static float pos_b = 0.0;

  static SQ15x16 brightness_low = 0.0;
  static SQ15x16 brightness_mid = 0.0;
  static SQ15x16 brightness_high = 0.0;

  SQ15x16 sum_low = 0.0;
  SQ15x16 sum_mid = 0.0;
  SQ15x16 sum_high = 0.0;

  // Consolidate brightness calculation
  for (uint8_t i = 0; i < 20; i++) { // Assuming NUM_FREQS is 64, split into 3 bands
    SQ15x16 bin_low = spectrogram_smooth[i]; // 0-19
    SQ15x16 bin_mid = spectrogram_smooth[20 + i]; // 20-39
    SQ15x16 bin_high = spectrogram_smooth[40 + i]; // 40-59 (adjust range if NUM_FREQS changes)

    bin_low = bin_low * 0.5 + (bin_low * bin_low) * 0.5;
    bin_mid = bin_mid * 0.5 + (bin_mid * bin_mid) * 0.5;
    bin_high = bin_high * 0.5 + (bin_high * bin_high) * 0.5;

    sum_low += bin_low;
    sum_mid += bin_mid;
    sum_high += bin_high;

    if (bin_low > brightness_low) brightness_low += fabs_fixed(bin_low - brightness_low) * 0.1;
    if (bin_mid > brightness_mid) brightness_mid += fabs_fixed(bin_mid - brightness_mid) * 0.1;
    if (bin_high > brightness_high) brightness_high += fabs_fixed(bin_high - brightness_high) * 0.1;
  }

  brightness_low *= 0.99;
  brightness_mid *= 0.99;
  brightness_high *= 0.99;

  SQ15x16 shift_speed = (SQ15x16)100 + ((SQ15x16)500 * (SQ15x16)CONFIG.MOOD);

  SQ15x16 shift_r = (shift_speed * sum_low);
  SQ15x16 shift_g = (shift_speed * sum_mid);
  SQ15x16 shift_b = (shift_speed * sum_high);

  // Removed speed limit check as it seemed arbitrary

  pos_r += (float)shift_r;
  pos_g += (float)shift_g;
  pos_b += (float)shift_b;

  // Loop through the first half of the strip
  for (uint16_t i = 0; i < (NATIVE_RESOLUTION / 2); i++) {
    uint32_t y_pos_r = pos_r;
    uint32_t y_pos_g = pos_g;
    uint32_t y_pos_b = pos_b;

    // Scale noise coordinate space based on position in the half-strip
    uint32_t i_mapped = i + 18; // Keep offset? Maybe adjust
    SQ15x16 noise_coord_scale = 2.0; // Affects density of noise pattern
    uint32_t i_scaled = uint32_t(((SQ15x16)i_mapped * (SQ15x16)i_mapped * (SQ15x16)i_mapped) * noise_coord_scale);


    SQ15x16 r_val = inoise16(i_scaled * 0.5 + y_pos_r) / 65536.0;
    SQ15x16 g_val = inoise16(i_scaled * 1.0 + y_pos_g) / 65536.0;
    SQ15x16 b_val = inoise16(i_scaled * 1.5 + y_pos_b) / 65536.0;

    if (r_val > 1.0) { r_val = 1.0; };
    if (g_val > 1.0) { g_val = 1.0; };
    if (b_val > 1.0) { b_val = 1.0; };

    // Handle fractional contrast values
    uint8_t base_iters = (uint8_t)CONFIG.SQUARE_ITER;
    float fract_iter = CONFIG.SQUARE_ITER - base_iters;

    // Apply full iterations
    for (uint8_t s = 0; s < base_iters; s++) {
      r_val *= r_val;
      g_val *= g_val;
      b_val *= b_val;
    }

    // Apply fractional iteration if needed
    if (fract_iter > 0.01) {
      SQ15x16 r_squared = r_val * r_val;
      SQ15x16 g_squared = g_val * g_val;
      SQ15x16 b_squared = b_val * b_val;

      r_val = r_val * (1.0 - fract_iter) + r_squared * fract_iter;
      g_val = g_val * (1.0 - fract_iter) + g_squared * fract_iter;
      b_val = b_val * (1.0 - fract_iter) + b_squared * fract_iter;
    }

    r_val = apply_contrast_fixed(r_val, 0.1);
    g_val = apply_contrast_fixed(g_val, 0.1);
    b_val = apply_contrast_fixed(b_val, 0.1);

    SQ15x16 prog = 1.0;
    // Fade brightness towards the start of the half-strip
    if (i < (NATIVE_RESOLUTION / 4)) { // Fade over first quarter
      prog = (SQ15x16)i / (SQ15x16)(NATIVE_RESOLUTION / 4 -1);
      prog *= prog; // Quadratic fade
    }


    r_val *= prog * brightness_low;
    g_val *= prog * brightness_mid;
    b_val *= prog * brightness_high;

    CRGB16 col = { r_val, g_val, b_val };
    col = desaturate(col, 0.1 + (0.9 - 0.9*CONFIG.SATURATION));

    if (chromatic_mode == false) {
      SQ15x16 brightness = 0.0;
      if(r_val > brightness){ brightness = r_val; }
      if(g_val > brightness){ brightness = g_val; }
      if(b_val > brightness){ brightness = b_val; }

      // Hue progression based on position in the half-strip
      SQ15x16 hue_prog = (SQ15x16)i / (SQ15x16)(NATIVE_RESOLUTION / 2 -1);
      // Use CONFIG.CHROMA directly
      SQ15x16 led_hue = CONFIG.CHROMA + hue_position + ((sqrt(float(brightness)) * SQ15x16(0.05)) + (hue_prog * SQ15x16(0.10)) * hue_shifting_mix);
      col = hsv(led_hue, CONFIG.SATURATION, brightness);
    }

    // Write to the first half and mirror to the second half
    leds_16[i] = { col.r, col.g, col.b };
    leds_16[NATIVE_RESOLUTION - 1 - i] = leds_16[i];
  }
}

void light_mode_chromagram_gradient() {
  // Loop through the second half of the strip
  for (uint16_t i = 0; i < (NATIVE_RESOLUTION / 2); i++) {
    SQ15x16 prog = (SQ15x16)i / (SQ15x16)(NATIVE_RESOLUTION / 2 -1); // Progress across the half strip
    SQ15x16 note_magnitude = interpolate(prog, chromagram_smooth, 12) * 0.9 + 0.1;

    // Handle fractional contrast values
    uint8_t base_iters = (uint8_t)CONFIG.SQUARE_ITER;
    float fract_iter = CONFIG.SQUARE_ITER - base_iters;

    // Apply full iterations
    for (uint8_t s = 0; s < base_iters; s++) {
      note_magnitude = (note_magnitude * note_magnitude) * SQ15x16(0.65) + (note_magnitude * SQ15x16(0.35));
    }

    // Apply fractional iteration if needed
    if (fract_iter > 0.01) {
      SQ15x16 squared = (note_magnitude * note_magnitude) * SQ15x16(0.65) + (note_magnitude * SQ15x16(0.35));
      note_magnitude = note_magnitude * (1.0 - fract_iter) + squared * fract_iter;
    }

    SQ15x16 led_hue;
    if (chromatic_mode == true) {
      // Interpolate note colors based on progress across the half-strip
      SQ15x16 color_prog = prog * 11.0; // Map 0-1 progress to 0-11 index range
      uint8_t idx1 = color_prog.getInteger();
      uint8_t idx2 = idx1 + 1;
      if (idx2 >= 12) idx2 = 11; // Clamp index
      SQ15x16 fract = color_prog - idx1;

      SQ15x16 hue1 = note_colors[idx1];
      SQ15x16 hue2 = note_colors[idx2];

      // Handle hue wrap-around during interpolation
      if (fabs_fixed(hue1 - hue2) > 0.5) {
          if (hue1 < hue2) hue1 += 1.0; else hue2 += 1.0;
      }
      led_hue = hue1 * (1.0 - fract) + hue2 * fract;
      if (led_hue >= 1.0) led_hue -= 1.0; // Normalize back to 0-1 range

    } else {
      // Use CONFIG.CHROMA directly instead of the potentially stale global chroma_val
      led_hue = CONFIG.CHROMA + hue_position + ((sqrt(float(note_magnitude)) * SQ15x16(0.05)) + (prog * SQ15x16(0.10)) * hue_shifting_mix);
    }

    CRGB16 col = hsv(led_hue, CONFIG.SATURATION, note_magnitude * note_magnitude);

    // Write to the second half of the strip
    leds_16[(NATIVE_RESOLUTION / 2) + i] = col;
    // Mirror to the first half of the strip
    leds_16[(NATIVE_RESOLUTION / 2) - 1 - i] = col;
  }
}

void light_mode_chromagram_dots() {
  // static SQ15x16 chromagram_last[12]; // Removed static buffer

  memset(leds_16, 0, sizeof(CRGB16) * NATIVE_RESOLUTION);
  //dim_display(0.9);

  // low_pass_array_fixed(chromagram_smooth, chromagram_last, 12, LED_FPS, float(mood_scale(3.5, 1.5))); // Removed low-pass call
  // memcpy(chromagram_last, chromagram_smooth, sizeof(float) * 12); // Removed memcpy

  for (uint8_t i = 0; i < 12; i++) {
    SQ15x16 led_hue;
    if (chromatic_mode == true) {
      led_hue = note_colors[i];
    } else {
      // Use CONFIG.CHROMA directly
      led_hue = CONFIG.CHROMA + hue_position + (sqrt(float(1.0)) * SQ15x16(0.05));
    }

    SQ15x16 magnitude = chromagram_smooth[i] * 1.0;
    if (magnitude > 1.0) { magnitude = 1.0; }

    magnitude = magnitude * magnitude;

    CRGB16 col = hsv(led_hue, CONFIG.SATURATION, magnitude);

    set_dot_position(RESERVED_DOTS + i * 2 + 0, magnitude * 0.45 + 0.5);
    set_dot_position(RESERVED_DOTS + i * 2 + 1, 0.5 - magnitude * 0.45);

    draw_dot(leds_16, RESERVED_DOTS + i * 2 + 0, col);
    draw_dot(leds_16, RESERVED_DOTS + i * 2 + 1, col);
  }
}

void light_mode_bloom(CRGB16* leds_prev_buffer) { // Accept previous buffer as argument
  // Clear output
  memset(leds_16, 0, sizeof(CRGB16) * NATIVE_RESOLUTION);

  // Draw previous frame shifted with mood scaling
  // Use the provided buffer instead of the global one
  draw_sprite(leds_16, leds_prev_buffer, NATIVE_RESOLUTION, NATIVE_RESOLUTION, 0.250 + 1.750 * CONFIG.MOOD, 0.99);
  
  // DEBUG: Check chromagram values - DISABLED to reduce serial flooding
  static uint32_t bloom_debug_counter = 0;
  bloom_debug_counter++; // Keep counter for other uses
  /*
  if (debug_mode && (bloom_debug_counter++ % 100 == 0)) {
    float total_chromagram = 0;
    for (int i = 0; i < 12; i++) {
      total_chromagram += float(chromagram_smooth[i]);
    }
    USBSerial.print("BLOOM DEBUG: total_chromagram=");
    USBSerial.print(total_chromagram);
    USBSerial.print(" hue_position=");
    USBSerial.println(float(hue_position));
  }
  */

  //-------------------------------------------------------
  // Calculate new color input based on chromagram
  CRGB16 sum_color;
  memset(&sum_color, 0, sizeof(CRGB16)); // Initialize sum_color

  // Mix colors from strongest chromagram bins
  SQ15x16 total_magnitude = 0.0;
  
  for (uint8_t i = 0; i < 12; i++) {
    SQ15x16 bin = chromagram_smooth[i];
    // Apply contrast iterations (integer part)
    for(uint8_t iter = 0; iter < (uint8_t)CONFIG.SQUARE_ITER; ++iter) {
        bin *= bin;
    }
    // Apply fractional contrast iteration
    float fract_iter = CONFIG.SQUARE_ITER - floor(CONFIG.SQUARE_ITER);
    if (fract_iter > 0.01) {
        SQ15x16 squared = bin * bin;
        bin = bin * (1.0 - fract_iter) + squared * fract_iter;
    }
    
    // Only add colors from bins above threshold
    if (bin > 0.05) {
      float prog = i / 12.0;
      // Offset hue to avoid yellow dominance - start at cyan/blue instead of red
      // This distributes the chromagram across the full color spectrum more evenly
      SQ15x16 hue_offset = SQ15x16(0.5); // Start at cyan instead of red
      SQ15x16 note_hue = SQ15x16(prog) + hue_offset;
      if (note_hue > 1.0) note_hue -= 1.0;
      
      // Apply auto color shift if enabled
      if (chromatic_mode == true) {
        note_hue += hue_position;
        if (note_hue > 1.0) note_hue -= 1.0;
      }
      
      CRGB16 add_color = get_mode_color(note_hue, CONFIG.SATURATION, bin);
      
      sum_color.r += add_color.r;
      sum_color.g += add_color.g;
      sum_color.b += add_color.b;
      total_magnitude += bin;
    }
  }
  
  // Normalize by total magnitude to preserve brightness
  if (total_magnitude > 0.01) {
    sum_color.r /= total_magnitude;
    sum_color.g /= total_magnitude;
    sum_color.b /= total_magnitude;
  }

  // Clamp color values
  if (sum_color.r > 1.0) { sum_color.r = 1.0; };
  if (sum_color.g > 1.0) { sum_color.g = 1.0; };
  if (sum_color.b > 1.0) { sum_color.b = 1.0; };

  // Apply saturation and hue adjustments (similar to original logic but using fixed point)
  CRGB temp_col_rgb = { uint8_t(sum_color.r * 255), uint8_t(sum_color.g * 255), uint8_t(sum_color.b * 255) };
  temp_col_rgb = force_saturation(temp_col_rgb, 255*float(CONFIG.SATURATION));
  
  // When chromatic mode is off, use the CHROMA knob to set a fixed hue
  if (chromatic_mode == false) {
    SQ15x16 led_hue = CONFIG.CHROMA + hue_position;
    if (led_hue > 1.0) led_hue -= 1.0;
    temp_col_rgb = force_hue(temp_col_rgb, 255*float(led_hue));
  }
  
  // DEBUG: Check what color we're inserting - DISABLED to reduce serial flooding
  /*
  if (debug_mode && (bloom_debug_counter % 100 == 1)) {
    USBSerial.print("BLOOM COLOR: chromatic_mode=");
    USBSerial.print(chromatic_mode);
    USBSerial.print(" RGB=(");
    USBSerial.print(temp_col_rgb.r);
    USBSerial.print(",");
    USBSerial.print(temp_col_rgb.g);
    USBSerial.print(",");
    USBSerial.print(temp_col_rgb.b);
    USBSerial.println(")");
  }
  */

  CRGB16 final_insert_color = { temp_col_rgb.r / 255.0, temp_col_rgb.g / 255.0, temp_col_rgb.b / 255.0 };
  
  // Apply PHOTONS brightness scaling
  final_insert_color.r *= frame_config.PHOTONS;
  final_insert_color.g *= frame_config.PHOTONS;
  final_insert_color.b *= frame_config.PHOTONS;

  // Insert the new color at the center of the strip
  uint16_t center_idx1 = (NATIVE_RESOLUTION / 2) - 1;
  uint16_t center_idx2 = NATIVE_RESOLUTION / 2;
  leds_16[center_idx1] = final_insert_color;
  leds_16[center_idx2] = final_insert_color; // Insert in two center pixels for symmetry

  //-------------------------------------------------------

  // Copy current frame to the provided previous frame buffer
  memcpy(leds_prev_buffer, leds_16, sizeof(CRGB16) * NATIVE_RESOLUTION);

  // Apply fade towards the ends of the strip (adjust fade range if needed)
  uint16_t fade_width = NATIVE_RESOLUTION / 4; // Fade over the outer quarters
  for(uint16_t i = 0; i < fade_width; i++) {
    float prog = (float)i / (fade_width - 1);
    SQ15x16 fade_amount = SQ15x16(prog * prog); // Quadratic fade, ensure SQ15x16

    // Fade right end
    leds_16[NATIVE_RESOLUTION - 1 - i].r *= fade_amount;
    leds_16[NATIVE_RESOLUTION - 1 - i].g *= fade_amount;
    leds_16[NATIVE_RESOLUTION - 1 - i].b *= fade_amount;

    // Fade left end
    leds_16[i].r *= fade_amount;
    leds_16[i].g *= fade_amount;
    leds_16[i].b *= fade_amount;
  }

  // Mirroring is implicitly handled by the structure? Or apply explicitly if needed.
  // If the sprite shift + center insert doesn't create symmetry, uncomment:
   mirror_image_downwards(leds_16); // Re-enabled mirroring
}

// Add at the end of the file, after the last light mode function but before any closing braces
void light_mode_quantum_collapse() {
  static SQ15x16 wave_probabilities[NATIVE_RESOLUTION];
  static bool initialized = false;
  static uint32_t last_collapse_time = 0;
  static uint16_t particle_positions[12] = {0}; 
  static SQ15x16 particle_velocities[12] = {0};
  static SQ15x16 particle_energies[12] = {0}; 
  static SQ15x16 particle_hues[12] = {0};
  static float animation_phase = 0.0;
  static float field_flow = 0.0;
  static SQ15x16 field_energy = SQ15x16(0.5);
  static SQ15x16 triad_hues[3];
  static SQ15x16 field_energy_f = SQ15x16(0.5);
  static SQ15x16 speed_mult_fixed = SQ15x16(1.0);
  static SQ15x16 wave_phase[NATIVE_RESOLUTION] = {0};  // For organic wave variation
  static SQ15x16 fluid_velocity[NATIVE_RESOLUTION] = {0}; // For fluid-like motion
  static SQ15x16 audio_impact = SQ15x16(0); // Audio impact tracker
  static SQ15x16 audio_pulse = SQ15x16(0); // Audio pulse effect
  static SQ15x16 prev_energy_level = SQ15x16(0); // For detecting energy changes
  static SQ15x16 beat_strength = SQ15x16(0); // For beat response
  
  // Initialize on first run
  if (!initialized) {
    // Set up triadic colour scheme based on current chroma value
    // Use CONFIG.CHROMA directly for initialization
    triad_hues[0] = CONFIG.CHROMA;
    triad_hues[1] = CONFIG.CHROMA + SQ15x16(0.333);
    triad_hues[2] = CONFIG.CHROMA + SQ15x16(0.667);
    
    // Initialize with variable patterns
    for (uint16_t i = 0; i < NATIVE_RESOLUTION; i++) {
      float position = (float)i / NATIVE_RESOLUTION;
      // Multiple overlapping waves with varied phases for organic look
      wave_probabilities[i] = SQ15x16(0.2) + 
                              SQ15x16(0.15) * sin(position * 6.28 * 2 + random_float() * 0.5) + 
                              SQ15x16(0.15) * sin(position * 6.28 * 3.5 + random_float() * 0.7);
      
      // Initialize wave phase with random offsets for natural variation
      wave_phase[i] = SQ15x16(random_float() * 6.28);
      
      // Initialize fluid velocities with slight variations
      fluid_velocity[i] = SQ15x16(random_float() * 0.01 - 0.005);
      
      if (wave_probabilities[i] > SQ15x16(1.0)) wave_probabilities[i] = SQ15x16(1.0);
      if (wave_probabilities[i] < SQ15x16(0.0)) wave_probabilities[i] = SQ15x16(0.0);
    }
    
    // Initialize particles with more varied properties
    for (uint8_t i = 0; i < 12; i++) {
      // More varied spacing and positioning
      float spacing_variety = NATIVE_RESOLUTION / (12.0 + random_float() * 4.0 - 2.0);
      particle_positions[i] = spacing_variety * i + random(15) - 7;
      
      // Ensure valid range
      if (particle_positions[i] >= NATIVE_RESOLUTION) 
        particle_positions[i] = NATIVE_RESOLUTION - 1;
      if (particle_positions[i] < 0) 
        particle_positions[i] = 0;
      
      // More natural velocity distribution - some fast, some slow, some nearly still
      float speed_factor = pow(random_float(), 2) * 3.0 + 0.5; // Non-linear distribution
      particle_velocities[i] = SQ15x16((random_float() - 0.45) * 1.2 * speed_factor);
      
      // Varied energy levels
      particle_energies[i] = SQ15x16(0.3 + pow(random_float(), 1.5) * 0.7);
      
      // Color variation with triadic scheme
      float hue_variety = random_float() * 0.12 - 0.06; // Greater color variation
      particle_hues[i] = triad_hues[i % 3] + SQ15x16(hue_variety);
    }
    
    initialized = true;
  }
  
  // Update triadic colours to follow auto color shift if enabled
  // This ensures colors evolve with the global color system
  // Use CONFIG.CHROMA directly
  triad_hues[0] = CONFIG.CHROMA + hue_position;
  triad_hues[1] = triad_hues[0] + SQ15x16(0.333);
  triad_hues[2] = triad_hues[0] + SQ15x16(0.667);
  
  // Normalize hues
  for (int i = 0; i < 3; i++) {
    while (triad_hues[i] > SQ15x16(1.0)) triad_hues[i] -= SQ15x16(1.0);
    while (triad_hues[i] < SQ15x16(0.0)) triad_hues[i] += SQ15x16(1.0);
  }
  
  // Audio reactivity - detect beats and energy changes
  SQ15x16 audio_energy = audio_vu_level_average > SQ15x16(0.01) ? 
                       (audio_vu_level / audio_vu_level_average) : SQ15x16(1.0);
  audio_energy = constrain(audio_energy, SQ15x16(0.5), SQ15x16(3.0));
  
  // Detect sudden audio level increases (beats)
  SQ15x16 energy_delta = audio_vu_level - prev_energy_level;
  prev_energy_level = audio_vu_level;
  
  // Create a beat pulse that decays naturally
  if (energy_delta > SQ15x16(0.08) && audio_vu_level > SQ15x16(0.15)) {
    // Strong beat detected - create impulse
    beat_strength = energy_delta * SQ15x16(5.0);
    if (beat_strength > SQ15x16(1.0)) beat_strength = SQ15x16(1.0);
    
    // Audio pulse gets boosted on beats for visible audio reaction
    audio_pulse = beat_strength * SQ15x16(1.5);
  }
  
  // Natural decay of beat strength with spring-like oscillation
  if (beat_strength > SQ15x16(0.01)) {
    beat_strength *= SQ15x16(0.95); // Decay
  } else {
    beat_strength = SQ15x16(0);
  }
  
  // Audio pulse decays with oscillation (more organic)
  if (audio_pulse > SQ15x16(0.01)) {
    audio_pulse = audio_pulse * SQ15x16(0.9) + SQ15x16(sin(float(audio_pulse) * 3.14)) * SQ15x16(0.1);
  } else {
    audio_pulse = SQ15x16(0);
  }
  
  // Audio impact rises quickly but decays smoothly (fluid mechanics)
  SQ15x16 target_impact = audio_vu_level * SQ15x16(2.0);
  if (target_impact > audio_impact) {
    // Fast rise
    audio_impact += (target_impact - audio_impact) * SQ15x16(0.3);
  } else {
    // Slow natural decay
    audio_impact -= (audio_impact - target_impact) * SQ15x16(0.05);
  }
  
  // System energy evolves with audio and MOOD 
  // More dynamic scaling of speed based on MOOD
  speed_mult_fixed = SQ15x16(0.7) + (CONFIG.MOOD * SQ15x16(4.0)); // More extreme speed range
  
  // Energy target influenced by audio beat detection
  field_energy_f = SQ15x16(0.4) + audio_energy * SQ15x16(0.3) + CONFIG.MOOD * SQ15x16(0.7) + beat_strength * SQ15x16(0.5);
  
  // Organic energy transition - faster rise, slower fall (natural feeling)
  if (field_energy_f > field_energy) {
    field_energy += (field_energy_f - field_energy) * SQ15x16(0.15); // Faster rise
  } else {
    field_energy -= (field_energy - field_energy_f) * SQ15x16(0.03); // Slower decay
  }
  
  // Animation phase advances with organic variation based on energy
  float energy_variation = float(field_energy) * 0.06 * (0.8 + sin(animation_phase * 0.7) * 0.2);
  animation_phase += (0.01 + energy_variation) * float(speed_mult_fixed); 
  field_flow += (0.005 + float(field_energy) * 0.015 * (0.9 + cos(animation_phase * 0.3) * 0.1)) * float(speed_mult_fixed);
  
  // Clear LED buffer
  memset(leds_16, 0, sizeof(CRGB16) * NATIVE_RESOLUTION);
  
  // Detect major beats for collapse events
  bool collapse_triggered = audio_vu_level > audio_vu_level_average * SQ15x16(1.3) && 
                         audio_vu_level > SQ15x16(0.15) && 
                         (millis() - last_collapse_time > 250 - 100 * float(CONFIG.MOOD)); // Quicker collapse at high MOOD
  
  // Secondary collapse detection based on audio dynamics
  bool small_collapse = energy_delta > SQ15x16(0.08) && audio_vu_level > SQ15x16(0.1);
  
  // Wave function collapse on beats
  if (collapse_triggered) {
    // Choose collapse center with weighted probability
    uint16_t collapse_center;
    
    // Base probability on existing wave state and audio
    float total_prob = 0;
    for (uint16_t i = 0; i < NATIVE_RESOLUTION; i++) {
      total_prob += float(wave_probabilities[i]);
    }
    
    // Weighted probability selection for more natural collapses
    float random_prob = random_float() * total_prob;
    float prob_sum = 0;
    collapse_center = NATIVE_RESOLUTION / 2; // Default center
    
    for (uint16_t i = 0; i < NATIVE_RESOLUTION; i++) {
      prob_sum += float(wave_probabilities[i]);
      if (prob_sum >= random_prob) {
        collapse_center = i;
        break;
      }
    }
    
    // Audio-reactive collapse with more organic distribution
    float audio_intensity = 0.5 + float(audio_vu_level) * 0.5;
    // Width varies with SQUARE_ITER for visible control
    float collapse_width = 0.3 - float(CONFIG.SQUARE_ITER) * 0.05;
    if (collapse_width < 0.1) collapse_width = 0.1;
    
    // Non-uniform collapse pattern for more organic feel
    for (uint16_t i = 0; i < NATIVE_RESOLUTION; i++) {
      // Non-linear distance calculation for more natural falloff
      float raw_distance = fabs(float(i) - float(collapse_center));
      float scaled_distance = pow(raw_distance / (NATIVE_RESOLUTION * collapse_width), 1.2);
      
      // Varied collapse probability with audio influence
      float collapse_probability = exp(-scaled_distance * scaled_distance * 8.0 * audio_intensity);
      
      // Add randomness for natural look
      if (random_float() < collapse_probability) {
        // Collapsed regions with natural variance
        wave_probabilities[i] = SQ15x16(0.7 + random_float() * 0.3);
        
        // Reset wave phase for dynamic restart
        wave_phase[i] = SQ15x16(random_float() * 6.28);
        
        // Reset fluid velocity with burst in collapse direction
        float direction = (i < collapse_center) ? -1.0 : 1.0;
        fluid_velocity[i] = SQ15x16(direction * (0.01 + random_float() * 0.02) * float(audio_energy));
      } else {
        // Reduce probability with natural falloff
        float reduction = 0.8 - 0.6 * pow(scaled_distance, 0.8);
        if (reduction < 0.2) reduction = 0.2;
        
        // Apply with slight randomness
        wave_probabilities[i] *= SQ15x16(reduction * (0.95 + random_float() * 0.1));
        
        // Small fluid velocity impact
        fluid_velocity[i] += SQ15x16((i < collapse_center ? -0.005 : 0.005) * float(audio_energy));
      }
    }
    
    // Energize particles with more physical behavior
    for (uint8_t i = 0; i < 6; i++) {
      uint8_t particle_idx = random(12);
      
      // Position with natural spread from center
      int spread = NATIVE_RESOLUTION / (20 - random(8)); // Variable spread
      int16_t new_pos = collapse_center + random(spread*2) - spread;
      
      // Ensure bounds
      if (new_pos < 0) new_pos = 0;
      if (new_pos >= NATIVE_RESOLUTION) new_pos = NATIVE_RESOLUTION - 1;
      
      particle_positions[particle_idx] = new_pos;
      
      // More organic velocity based on position from center
      float dir = (new_pos < collapse_center) ? -1.0 : 1.0;
      // Log scale for more natural distribution
      float speed_variety = pow(0.5 + random_float() * 0.5, 0.7) * 3.0;
      particle_velocities[particle_idx] = SQ15x16(dir * speed_variety) * audio_energy;
      
      // Energy varies with audio level
      particle_energies[particle_idx] = SQ15x16(0.6 + float(audio_vu_level) * 0.4 + random_float() * 0.2);
      
      // Color with slight shift and audio influence
      particle_hues[particle_idx] = triad_hues[particle_idx % 3] + 
                                   SQ15x16(random_float() * 0.1 - 0.05) + 
                                   audio_vu_level * SQ15x16(0.05);
    }
    
    // Boost energy with physical dynamics
    field_energy += audio_energy * SQ15x16(0.5) * (SQ15x16(1.0) + beat_strength);
    if (field_energy > SQ15x16(2.5)) field_energy = SQ15x16(2.5);
    
    last_collapse_time = millis();
  }
  // Small collapses for added organics
  else if (small_collapse) {
    // Choose mini-collapse center near particles for natural focal points
    uint16_t small_collapse_center;
    if (random_float() < 0.7 && float(audio_vu_level) > 0.2) {
      // Bias toward existing particles
      small_collapse_center = particle_positions[random(12)];
    } else {
      small_collapse_center = random(NATIVE_RESOLUTION);
    }
    
    // Audio-reactive radius with organic variation
    int radius = 5 + int(float(audio_vu_level) * (8.0 + random_float() * 4.0));
    if (radius > 25) radius = 25;
    
    // Non-uniform collapse for natural look
    for (int16_t i = -radius; i <= radius; i++) {
      int16_t pos = small_collapse_center + i;
      if (pos >= 0 && pos < NATIVE_RESOLUTION) {
        // Non-linear distance for more organic falloff
        float distance = pow(fabs(float(i)) / radius, 1.2);
        float collapse_strength = exp(-distance * distance * 4) * 0.3 * float(audio_energy);
        
        // Add randomness for natural look
        collapse_strength *= 0.9 + random_float() * 0.2;
        
        // Apply to wave with fluid mechanics
        wave_probabilities[pos] += SQ15x16(collapse_strength);
        if (wave_probabilities[pos] > SQ15x16(1.0)) wave_probabilities[pos] = SQ15x16(1.0);
        
        // Update fluid velocity with impact
        fluid_velocity[pos] += SQ15x16((random_float() - 0.5) * collapse_strength * 0.02);
      }
    }
    
    // Physical particle behavior
    for (uint8_t i = 0; i < 2; i++) {
      uint8_t particle_idx = random(12);
      
      // Realistic physics - momentum conservation with energy loss
      particle_velocities[particle_idx] *= SQ15x16(-0.85 - random_float() * 0.1); 
      
      // Small energy boost with randomness
      particle_energies[particle_idx] += SQ15x16(0.15 + random_float() * 0.1);
      if (particle_energies[particle_idx] > SQ15x16(1.0)) particle_energies[particle_idx] = SQ15x16(1.0);
    }
    
    // Small energy boost with audio reaction
    field_energy += SQ15x16(0.05 + float(audio_vu_level) * 0.08);
    if (field_energy > SQ15x16(2.0)) field_energy = SQ15x16(2.0);
  }
  
  // Continuous fluid wave motion in the probability field
  // Audio-reactive amplitude with organic variation
  float wave_amplitude = 0.02 + float(audio_vu_level) * 0.08 + float(audio_pulse) * 0.05;
  
  // Update fluid simulation
  SQ15x16 fluid_diffusion = SQ15x16(0.03 + float(CONFIG.MOOD) * 0.02); // Diffusion rate
  SQ15x16 temp_fluid[NATIVE_RESOLUTION];
  
  // Copy fluid velocities for update
  memcpy(temp_fluid, fluid_velocity, sizeof(SQ15x16) * NATIVE_RESOLUTION);
  
  // Update fluid velocities with diffusion for organic flow
  for (uint16_t i = 1; i < NATIVE_RESOLUTION-1; i++) {
    fluid_velocity[i] = temp_fluid[i] * SQ15x16(1.0 - 2.0 * float(fluid_diffusion)) + 
                       (temp_fluid[i-1] + temp_fluid[i+1]) * fluid_diffusion;
                       
    // Natural velocity decay (fluid friction)
    fluid_velocity[i] *= SQ15x16(0.99);
  }
  
  // Apply wave updates with fluid transport
  for (uint16_t i = 0; i < NATIVE_RESOLUTION; i++) {
    float position = (float)i / NATIVE_RESOLUTION;
    
    // Advance wave phase with fluid velocity
    wave_phase[i] += SQ15x16(0.1 * float(speed_mult_fixed) * (0.5 + float(field_energy) * 0.5)) + fluid_velocity[i];
    
    // Create complex wave patterns with multiple harmonics
    SQ15x16 wave_add = SQ15x16(
      sin(position * 8.0 + animation_phase * 1.5 + float(wave_phase[i])) * wave_amplitude * 0.6 +
      sin(position * 15.0 + animation_phase * 3.0 - float(wave_phase[i]) * 0.5) * wave_amplitude * 0.3 +
      sin(position * 5.0 + animation_phase * 0.7 + float(wave_phase[i]) * 0.3) * wave_amplitude * 0.4
    );
    
    // Audio pulse adds bloom on beats
    if (audio_pulse > SQ15x16(0.01)) {
      wave_add += audio_pulse * SQ15x16(sin(position * 30.0 + animation_phase * 5.0) * 0.03);
    }
    
    // Apply to wave field with transport
    int idx = i;
    float transport = float(fluid_velocity[i]) * 10.0;
    if (transport > 0) {
      idx = (i + 1 < NATIVE_RESOLUTION) ? i + 1 : i;
    } else if (transport < 0) {
      idx = (i > 0) ? i - 1 : i;
    }
    
    wave_probabilities[idx] += wave_add;
    
    // Keep within bounds with soft clipping for more natural look
    if (wave_probabilities[i] > SQ15x16(1.0)) {
      wave_probabilities[i] = SQ15x16(1.0) - ((wave_probabilities[i] - SQ15x16(1.0)) * SQ15x16(0.5));
    }
    if (wave_probabilities[i] < SQ15x16(0.0)) {
      wave_probabilities[i] = wave_probabilities[i] * SQ15x16(0.5);
    }
  }
  
  // Apply fluid-based diffusion for more organic movement
  SQ15x16 base_diffusion = SQ15x16(0.08) + (CONFIG.MOOD * SQ15x16(0.3)) + (field_energy * SQ15x16(0.1)); 
  SQ15x16 max_diffusion = SQ15x16(0.4);
  if (base_diffusion > max_diffusion) base_diffusion = max_diffusion;
  
  // Fluid flow changes with organic variation
  float flow_angle = field_flow + sin(animation_phase * 0.3) * 0.5;
  SQ15x16 flow_direction = SQ15x16(sin(flow_angle)) * SQ15x16(0.3);
  
  // Safe copy for diffusion
  SQ15x16 temp_field[NATIVE_RESOLUTION];
  memcpy(temp_field, wave_probabilities, sizeof(SQ15x16) * NATIVE_RESOLUTION);
  
  // Apply diffusion with organic asymmetry
  for (uint16_t i = 1; i < NATIVE_RESOLUTION-1; i++) {
    // Dynamic diffusion rate for non-uniform flow
    float pos_factor = 1.0 + sin(float(i) / NATIVE_RESOLUTION * 6.28 + animation_phase) * 0.2;
    SQ15x16 local_diffusion = base_diffusion * SQ15x16(pos_factor);
    
    // Asymmetric diffusion with fluid direction
    SQ15x16 left_mix = local_diffusion + flow_direction + fluid_velocity[i] * SQ15x16(2.0);
    SQ15x16 right_mix = local_diffusion - flow_direction - fluid_velocity[i] * SQ15x16(2.0);
    
    // Safety bounds
    if (left_mix < SQ15x16(0.01)) left_mix = SQ15x16(0.01);
    if (right_mix < SQ15x16(0.01)) right_mix = SQ15x16(0.01);
    if (left_mix > SQ15x16(0.4)) left_mix = SQ15x16(0.4);
    if (right_mix > SQ15x16(0.4)) right_mix = SQ15x16(0.4);
    
    // Calculate center mix with proper conservation
    SQ15x16 center_mix = SQ15x16(1.0) - (left_mix + right_mix);
    
    // Apply weighted mixing
    wave_probabilities[i] = temp_field[i] * center_mix + 
                           temp_field[i-1] * left_mix + 
                           temp_field[i+1] * right_mix;
    
    // Energy-based decay with audio influence
    SQ15x16 decay_rate = SQ15x16(0.995) + (field_energy * SQ15x16(0.003)) + (audio_impact * SQ15x16(0.001));
    if (decay_rate > SQ15x16(0.999)) decay_rate = SQ15x16(0.999);
    
    wave_probabilities[i] *= decay_rate;
  }
  
  // Update quantum particles with springy physics
  for (uint8_t i = 0; i < 12; i++) {
    // Dynamic energy target based on field and audio
    SQ15x16 energy_target = SQ15x16(0.3) + field_energy * SQ15x16(0.3) + audio_impact * SQ15x16(0.4);
    
    // Natural energy approach with diminishing returns
    SQ15x16 energy_delta = energy_target - particle_energies[i];
    if (energy_delta > SQ15x16(0)) {
      // Faster recovery when far from target (organic)
      particle_energies[i] += energy_delta * SQ15x16(0.05 + float(energy_delta) * 0.2);
    } else {
      // Slow decay when above target
      particle_energies[i] += energy_delta * SQ15x16(0.02);
    }
    
    // Audio boost to energy on beats
    if (beat_strength > SQ15x16(0.1)) {
      particle_energies[i] += beat_strength * SQ15x16(0.2);
    }
    
    // Clamp energy to reasonable range
    if (particle_energies[i] > SQ15x16(1.5)) particle_energies[i] = SQ15x16(1.5);
    if (particle_energies[i] < SQ15x16(0.1)) particle_energies[i] = SQ15x16(0.1);
    
    // Dynamic speed adjustment with physics
    SQ15x16 speed_mult_sq = speed_mult_fixed * speed_mult_fixed;
    // Base speed with energy influence
    SQ15x16 speed_mod = particle_energies[i] * (SQ15x16(0.6) + audio_vu_level * SQ15x16(0.8)) * speed_mult_sq;
    
    // Apply audio beat boost to speed
    if (beat_strength > SQ15x16(0.1)) {
      speed_mod += beat_strength * SQ15x16(0.8) * speed_mult_fixed;
    }
    
    // Calculate movement with inertia effects
    int delta_pos = (particle_velocities[i] * speed_mod).getInteger();
    
    // Limit maximum speed for stability with organic cap
    float speed_limit = 15.0 * (0.8 + 0.4 * random_float());
    if (delta_pos > speed_limit) delta_pos = speed_limit;
    if (delta_pos < -speed_limit) delta_pos = -speed_limit;
    
    // Update position with physics
    int32_t new_pos = particle_positions[i] + delta_pos;
    
    // Realistic boundary physics with energy preservation
    if (new_pos >= NATIVE_RESOLUTION) {
      // Bounce with energy loss and slight randomization
      particle_positions[i] = NATIVE_RESOLUTION - 1;
      
      // Vary bounce coefficient for more natural feel
      float bounce_factor = -0.8 - random_float() * 0.15;
      particle_velocities[i] *= SQ15x16(bounce_factor);
      
      // Energy loss on collision with slight randomization
      particle_energies[i] *= SQ15x16(0.85 + random_float() * 0.1);
    } else if (new_pos < 0) {
      // Bounce off other wall with similar physics
      particle_positions[i] = 0;
      
      float bounce_factor = -0.8 - random_float() * 0.15;
      particle_velocities[i] *= SQ15x16(bounce_factor);
      particle_energies[i] *= SQ15x16(0.85 + random_float() * 0.1);
    } else {
      // Normal movement
      particle_positions[i] = new_pos;
    }
    
    // Get current position
    uint16_t pos = particle_positions[i];
    
    // Force influence from probability field with natural physics
    SQ15x16 field_gradient = SQ15x16(0);
    if (pos > 2 && pos < NATIVE_RESOLUTION-3) {
      // Wider gradient sampling for smoother motion
      field_gradient = (wave_probabilities[pos+3] - wave_probabilities[pos-3]) * SQ15x16(0.3);
    }
    
    // Apply acceleration with mass-like properties
    SQ15x16 mass_factor = SQ15x16(1.5) - particle_energies[i] * SQ15x16(0.5); // Larger masses accelerate slower
    if (mass_factor < SQ15x16(0.5)) mass_factor = SQ15x16(0.5);
    
    SQ15x16 acceleration = field_gradient * SQ15x16(0.25) * speed_mult_fixed / mass_factor;
    particle_velocities[i] += acceleration;
    
    // Add oscillation with natural harmonics and phase variance
    float phase_offset = i * 0.7 + sin(i * 0.3) * 2.0;
    particle_velocities[i] += SQ15x16(
      sin(animation_phase * (0.3 + (i % 4) * 0.2) + phase_offset) * 0.03 * 
      (0.8 + float(particle_energies[i]) * 0.4)
    ) * speed_mult_fixed;
    
    // Natural velocity bounds with energy consideration
    SQ15x16 max_velocity = (SQ15x16(0.4) + particle_energies[i] * SQ15x16(1.1)) * speed_mult_fixed;
    if (particle_velocities[i] > max_velocity) {
      // Soft limiting for natural feel
      particle_velocities[i] = max_velocity - ((particle_velocities[i] - max_velocity) * SQ15x16(0.5));
    }
    if (particle_velocities[i] < -max_velocity) {
      particle_velocities[i] = -max_velocity + ((particle_velocities[i] + max_velocity) * SQ15x16(0.5));
    }
    
    // Energy and audio influence trail strength
    SQ15x16 trail_strength = SQ15x16(0.1) + 
                            particle_energies[i] * SQ15x16(0.2) + 
                            audio_vu_level * SQ15x16(0.2) +
                            audio_pulse * SQ15x16(0.4); // Beat responsiveness
    
    // Add to probability field with fluid dynamics
    wave_probabilities[pos] += trail_strength;
    if (wave_probabilities[pos] > SQ15x16(1.0)) wave_probabilities[pos] = SQ15x16(1.0);
    
    // Audio-reactive trail width
    float trail_intensity = float(particle_energies[i]) * (1.0 + float(audio_vu_level) * 0.5);
    uint8_t trail_width = 1 + (SQ15x16(trail_intensity) * SQ15x16(4)).getInteger();
    if (trail_width > 6) trail_width = 6;
    
    // Create organic trail with Gaussian-like distribution
    for (int8_t j = -trail_width; j <= trail_width; j++) {
      if (j == 0) continue; // Skip center
      
      int16_t trail_pos = pos + j;
      if (trail_pos >= 0 && trail_pos < NATIVE_RESOLUTION) {
        // Non-linear falloff for more natural look
        float falloff_factor = 2.0 + float(audio_vu_level) * 2.0; // Audio affects trail shape
        float falloff = exp(-(j*j) / (float)(trail_width*trail_width) * falloff_factor);
        
        // Add trail with audio influence
        SQ15x16 trail_value = trail_strength * SQ15x16(falloff) * SQ15x16(0.5);
        wave_probabilities[trail_pos] += trail_value;
        
        // Add fluid velocity influence
        fluid_velocity[trail_pos] += SQ15x16(j > 0 ? 0.0005 : -0.0005) * falloff * particle_energies[i];
        
        if (wave_probabilities[trail_pos] > SQ15x16(1.0)) 
          wave_probabilities[trail_pos] = SQ15x16(1.0);
      }
    }
  }
  
  // Render final visualization with triadic color scheme
  for (uint16_t i = 0; i < NATIVE_RESOLUTION; i++) {
    // Position-based color zones with flow
    float zone_progress = (float)i / NATIVE_RESOLUTION;
    zone_progress = fmod(zone_progress + animation_phase * 0.02, 1.0); // Slowly shifting zones
    
    // Dynamic color zones based on thirds
    uint8_t hue_idx = (int)(zone_progress * 3) % 3;
    
    // Base hue from triadic scheme with position blending
    SQ15x16 field_hue = triad_hues[hue_idx];
    
    // Organic color gradients between zones
    float zone_pos = fmod(zone_progress * 3.0, 1.0);
    if (zone_pos > 0.85 || zone_pos < 0.15) {
      // Blend between zones for smoother transition
      uint8_t next_idx = (hue_idx + 1) % 3;
      float blend = zone_pos > 0.5 ? (zone_pos - 0.85) * 6.67 : (0.15 - zone_pos) * 6.67;
      field_hue = triad_hues[hue_idx] * SQ15x16(1.0 - blend) + triad_hues[next_idx] * SQ15x16(blend);
    }
    
    // Add small organic variance
    SQ15x16 position_variance = SQ15x16(sin(zone_progress * 6.28 + animation_phase) * 0.03);
    field_hue += position_variance;
    
    // Audio-reactive color shift (subtle)
    field_hue += audio_vu_level * SQ15x16(0.02) * SQ15x16(sin(animation_phase * 0.5 + i * 0.03));
    
    // Keep hue in valid range
    if (field_hue > SQ15x16(1.0)) field_hue -= SQ15x16(1.0);
    if (field_hue < SQ15x16(0.0)) field_hue += SQ15x16(1.0);
    
    // Dynamic brightness with organic curves
    SQ15x16 brightness = wave_probabilities[i] * (SQ15x16(0.4) + CONFIG.PHOTONS * SQ15x16(0.6));
    
    // Audio-reactive brightness boost
    brightness += audio_vu_level * SQ15x16(0.2) * brightness;
    
    // Beat pulse brightening
    if (audio_pulse > SQ15x16(0.01)) {
      brightness += audio_pulse * SQ15x16(0.3) * brightness;
    }
    
    // Apply contrast with organic feel
    for (uint8_t s = 0; s < (uint8_t)CONFIG.SQUARE_ITER; s++) {
      brightness = brightness * brightness;
    }
    
    // Apply fractional contrast for smoother control
    float fract_iter = CONFIG.SQUARE_ITER - floor(CONFIG.SQUARE_ITER);
    if (fract_iter > 0.01) {
      SQ15x16 squared = brightness * brightness;
      brightness = brightness * SQ15x16(1.0 - fract_iter) + squared * fract_iter;
    }
    
    // Organic wave modulation for added dimensionality
    float wave_factor = 0.15 + 0.1 * float(audio_vu_level);
    brightness *= SQ15x16(1.0 - wave_factor) + 
                 SQ15x16(wave_factor) * sin(i * 0.15 + animation_phase * 2.5 + float(wave_phase[i]));
    
    // Dynamic saturation
    SQ15x16 saturation = CONFIG.SATURATION;
    
    // Desaturate very bright and dark regions for natural look
    if (wave_probabilities[i] > SQ15x16(0.85)) {
      saturation *= SQ15x16(1.0 - (float(wave_probabilities[i]) - 0.85) * 2.0 * 0.3);
    } else if (wave_probabilities[i] < SQ15x16(0.1)) {
      saturation *= SQ15x16(0.7 + float(wave_probabilities[i]) * 3.0);
    }
    
    // Audio affects saturation slightly
    saturation *= SQ15x16(0.9 + float(audio_vu_level) * 0.2);
    
    // Create final LED color
    leds_16[i] = get_mode_color(field_hue, saturation, brightness);
  }
  
  // Render particles with bloom physics
  for (uint8_t i = 0; i < 12; i++) {
    uint16_t pos = particle_positions[i];
    
    // Only render if in valid range
    if (pos < NATIVE_RESOLUTION) {
      // Audio-reactive pulse with unique frequency
      float pulse_freq = 2.0 + i * 0.4 + sin(i * 0.7) * 0.5;
      SQ15x16 pulse = SQ15x16(0.7) + SQ15x16(0.3) * sin(animation_phase * pulse_freq + i * 0.7);
      
      // Audio boosts pulse
      pulse += audio_pulse * SQ15x16(0.4);
      if (pulse > SQ15x16(1.5)) pulse = SQ15x16(1.5);
      
      // Energy and audio affect appearance
      SQ15x16 energy_factor = particle_energies[i] * (SQ15x16(1.0) + audio_vu_level * SQ15x16(0.5));
      
      // Get particle hue with slight audio variation
      uint8_t hue_idx = i % 3;
      SQ15x16 particle_hue = triad_hues[hue_idx];
      
      // Audio and energy affect hue slightly
      float hue_shift = sin(animation_phase * 0.7 + i * 0.5) * 0.03 * float(audio_vu_level);
      particle_hue += SQ15x16(hue_shift);
      
      // Normalize hue
      if (particle_hue > SQ15x16(1.0)) particle_hue -= SQ15x16(1.0);
      if (particle_hue < SQ15x16(0.0)) particle_hue += SQ15x16(1.0);
      
      // Brightness with pulse and energy
      SQ15x16 particle_brightness = SQ15x16(0.7) + energy_factor * SQ15x16(0.3);
      particle_brightness *= pulse;
      
      // Create particle color
      CRGB16 particle_color = get_mode_color(particle_hue, 
                                 CONFIG.SATURATION * SQ15x16(0.95), 
                                 particle_brightness);
      
      // Dynamic intensity with audio response
      SQ15x16 intensity = SQ15x16(2.5) + particle_energies[i] * SQ15x16(2.5);
      
      // Audio boost to brightness
      intensity += audio_pulse * SQ15x16(3.0);
      
      // Set particle with additive blending for glow
      leds_16[pos].r = fmax_fixed(leds_16[pos].r, particle_color.r * intensity); 
      leds_16[pos].g = fmax_fixed(leds_16[pos].g, particle_color.g * intensity);
      leds_16[pos].b = fmax_fixed(leds_16[pos].b, particle_color.b * intensity);
      
      // Dynamic bloom radius with energy and audio
      float bloom_size = 2.0 + float(particle_energies[i]) * 4.0 + float(audio_pulse) * 3.0;
      int bloom_radius = bloom_size;
      if (bloom_radius > 8) bloom_radius = 8;
      
      // Create bloom with organic falloff
      for (int8_t j = -bloom_radius; j <= bloom_radius; j++) {
        if (j == 0) continue; // Skip center
        
        int16_t bloom_pos = pos + j;
        if (bloom_pos >= 0 && bloom_pos < NATIVE_RESOLUTION) {
          // Non-linear falloff for more natural glow
          float distance = abs(j) / bloom_size;
          float bloom_curve = 2.5 + float(audio_vu_level) * 2.0; // Audio affects bloom shape
          SQ15x16 falloff = SQ15x16(exp(-distance * distance * bloom_curve)) * pulse;
          
          // Energy affects bloom intensity
          SQ15x16 bloom_intensity = SQ15x16(0.8) + particle_energies[i] * SQ15x16(1.2);
          
          // Audio boosts bloom
          bloom_intensity += audio_pulse * SQ15x16(1.5);
          
          // Add bloom to existing color
          leds_16[bloom_pos].r += particle_color.r * falloff * bloom_intensity;
          leds_16[bloom_pos].g += particle_color.g * falloff * bloom_intensity;
          leds_16[bloom_pos].b += particle_color.b * falloff * bloom_intensity;
          
          // Create fluid velocity from bloom for organic flow
          fluid_velocity[bloom_pos] += SQ15x16(j > 0 ? 0.0005 : -0.0005) * falloff * particle_energies[i];
        }
      }
      
      // Occasional energy bursts for added interest
      if (random(100) < 3 + int(float(audio_vu_level) * 10)) {
        // Create burst with random spread
        int burst_count = 2 + random(3);
        for (int b = 0; b < burst_count; b++) {
          int burst_pos = pos + random(21) - 10;
          if (burst_pos >= 0 && burst_pos < NATIVE_RESOLUTION) {
            // Energy and audio affect burst intensity
            SQ15x16 burst_intensity = SQ15x16(0.3) + particle_energies[i] * SQ15x16(0.7) + audio_vu_level * SQ15x16(0.5);
            
            // Add burst glow
            leds_16[burst_pos].r += particle_color.r * burst_intensity * SQ15x16(0.4);
            leds_16[burst_pos].g += particle_color.g * burst_intensity * SQ15x16(0.4);
            leds_16[burst_pos].b += particle_color.b * burst_intensity * SQ15x16(0.4);
            
            // Add fluid impulse
            fluid_velocity[burst_pos] += SQ15x16((random_float() - 0.5) * 0.02) * audio_energy;
          }
        }
      }
    }
  }
  
  // Clip all LED values to prevent overflow
  clip_led_values(leds_16); // Pass the buffer
  
  // Handle mirroring
  if (CONFIG.MIRROR_ENABLED) {
    mirror_image_downwards(leds_16);
  }
}

void light_mode_waveform(CRGB16* leds_previous, CRGB16& last_color) { // New signature accepting previous buffer and last color reference
  static float waveform_peak_scaled_last;

  // Smooth the waveform peak with more aggressive smoothing
  SQ15x16 smoothed_peak_fixed = SQ15x16(waveform_peak_scaled) * 0.02 + SQ15x16(waveform_peak_scaled_last) * 0.98;
  waveform_peak_scaled_last = float(smoothed_peak_fixed);

  CRGB16 current_sum_color = {0,0,0};
  SQ15x16 total_magnitude = 0.0;
  
  for (uint8_t c = 0; c < 12; c++) {
    float prog = c / 12.0f;
    float bin = float(chromagram_smooth[c]);

    float bright = bin;
    for (uint8_t s = 0; s < int(CONFIG.SQUARE_ITER); s++) {
      bright *= bright;
    }
    float fract_iter = CONFIG.SQUARE_ITER - floor(CONFIG.SQUARE_ITER);
    if (fract_iter > 0.01) {
      float squared = bright * bright;
      bright = bright * (1.0f - fract_iter) + squared * fract_iter;
    }
    
    // Only add colors from bins above threshold for better color clarity
    if (bright > 0.05) {
      CRGB16 note_col = get_mode_color(SQ15x16(prog), CONFIG.SATURATION, SQ15x16(bright));
      current_sum_color.r += note_col.r;
      current_sum_color.g += note_col.g;
      current_sum_color.b += note_col.b;
      total_magnitude += bright;
    }
  }
  
  if (chromatic_mode == true && total_magnitude > 0.01) {
    // Normalize by total magnitude to get pure color, then scale by brightness
    current_sum_color.r /= total_magnitude;
    current_sum_color.g /= total_magnitude;
    current_sum_color.b /= total_magnitude;
    
    // Scale back up by total magnitude for proper brightness
    current_sum_color.r *= total_magnitude;
    current_sum_color.g *= total_magnitude;
    current_sum_color.b *= total_magnitude;
  } else if (chromatic_mode == false) {
    // Use single hue with total_magnitude for brightness
    current_sum_color = get_mode_color(chroma_val + hue_position, CONFIG.SATURATION, total_magnitude);
  }

  // Apply PHOTONS brightness scaling
  current_sum_color.r *= frame_config.PHOTONS;
  current_sum_color.g *= frame_config.PHOTONS;
  current_sum_color.b *= frame_config.PHOTONS;
  
  // Use the chromagram color mix for the waveform
  last_color = current_sum_color;

  if (snapwave_color_debug_logging_enabled) {
    static uint32_t last_color_debug = 0;
    uint32_t now_ms = millis();
    if (now_ms - last_color_debug > 2000) {
      USBSerial.printf("SNAPWAVE COLOR DEBUG | chromatic=%d | saturation=%.2f | r=%.3f g=%.3f b=%.3f | total_mag=%.3f\n",
                       chromatic_mode ? 1 : 0,
                       float(frame_config.SATURATION),
                       float(last_color.r),
                       float(last_color.g),
                       float(last_color.b),
                       float(total_magnitude));
      last_color_debug = now_ms;
    }
  }
  // --- End Color Calculation ---

  // --- Dynamic Fading for Trails ---
  // Operate directly on the global leds_16 buffer, assuming it holds the *target* state from previous frame
  float abs_amp = abs(waveform_peak_scaled); 
  if (abs_amp > 1.0f) abs_amp = 1.0f; 
  
  float max_fade_reduction = 0.10; 
  SQ15x16 dynamic_fade_amount = 1.0 - (max_fade_reduction * abs_amp);

  // Apply the dynamic fade TO THE GLOBAL leds_16 buffer
  for (uint16_t i = 0; i < NATIVE_RESOLUTION; i++) {
      leds_16[i].r *= dynamic_fade_amount;
      leds_16[i].g *= dynamic_fade_amount;
      leds_16[i].b *= dynamic_fade_amount;
  }

  // --- Waveform Display --- 
  shift_leds_up(leds_16, 1); // Shift the global leds_16 buffer
  
  // Use smoothed peak instead of raw peak
  float amp = waveform_peak_scaled_last;
  
  // Apply threshold to ignore tiny movements
  float threshold = 0.05f;  // Ignore movements smaller than 5%
  if (fabs(amp) < threshold) {
    amp = 0.0f;
  }
  
  // Scale down the amplitude for less dramatic movement
  // Use CONFIG.SENSITIVITY to control waveform responsiveness (inverted - lower = less sensitive)
  float sensitivity_scale = 0.7f / CONFIG.SENSITIVITY;  // At SENSITIVITY=1.0, use 70% of strip
  amp *= sensitivity_scale;
  
  if (amp > 1.0f) amp = 1.0f;
  else if (amp < -1.0f) amp = -1.0f;
  int center = NATIVE_RESOLUTION / 2;
  float pos_f = center + amp * (NATIVE_RESOLUTION / 2.0f); 
  int pos = int(pos_f + (pos_f >= 0 ? 0.5 : -0.5));
  if (pos < 0) pos = 0;
  if (pos >= NATIVE_RESOLUTION) pos = NATIVE_RESOLUTION - 1;
  
  // Set the new dot with the calculated & smoothed 'last_color'
  leds_16[pos] = last_color; // Draw onto the global leds_16 buffer
  
  if (CONFIG.MIRROR_ENABLED) { // Check current config setting for mirroring
    mirror_image_downwards(leds_16);
  }
}

void light_mode_snapwave() {
  // DEBUG: Verify correct function is being called
  if (snapwave_debug_logging_enabled) {
    static uint32_t call_count = 0;
    if (call_count++ % 60 == 0) {  // Log every second at 60fps
      USBSerial.printf("SNAPWAVE DEBUG: Original executing! Mode index=%d, Expected=%d\n",
                       CONFIG.LIGHTSHOW_MODE, LIGHT_MODE_SNAPWAVE);
    }
  }

  static float waveform_peak_scaled_last = 0.0f;
  static CRGB16 last_color = {0, 0, 0};

  // Smooth the waveform peak with more aggressive smoothing
  SQ15x16 smoothed_peak_fixed = SQ15x16(waveform_peak_scaled) * 0.02 + SQ15x16(waveform_peak_scaled_last) * 0.98;
  waveform_peak_scaled_last = float(smoothed_peak_fixed);

  // --- Color Calculation from Chromagram ---
  CRGB16 current_sum_color = {0, 0, 0};
  SQ15x16 total_magnitude = 0.0;

  for (uint8_t c = 0; c < 12; c++) {
    float prog = c / 12.0f;
    float bin = float(chromagram_smooth[c]);

    float bright = bin;
    for (uint8_t s = 0; s < int(frame_config.SQUARE_ITER); s++) {
      bright *= bright;
    }
    float fract_iter = frame_config.SQUARE_ITER - floor(frame_config.SQUARE_ITER);
    if (fract_iter > 0.01) {
      float squared = bright * bright;
      bright = bright * (1.0f - fract_iter) + squared * fract_iter;
    }

    // Only add colors from bins above threshold for better color clarity
    if (bright > 0.05) {
      // ORIGINAL SNAPWAVE COLOR PATH: use pure HSV, not palette/get_mode_color
      CRGB16 note_col = hsv(SQ15x16(prog), frame_config.SATURATION, SQ15x16(bright));
      current_sum_color.r += note_col.r;
      current_sum_color.g += note_col.g;
      current_sum_color.b += note_col.b;
      total_magnitude += bright;
    }
  }

  if (chromatic_mode == true && total_magnitude > 0.01) {
    // Normalize by total magnitude to get pure color, then scale by brightness
    current_sum_color.r /= total_magnitude;
    current_sum_color.g /= total_magnitude;
    current_sum_color.b /= total_magnitude;

    // Scale back up by total magnitude for proper brightness
    current_sum_color.r *= total_magnitude;
    current_sum_color.g *= total_magnitude;
    current_sum_color.b *= total_magnitude;
  } else if (chromatic_mode == false) {
    // ORIGINAL SNAPWAVE COLOR PATH: single-hue HSV for non-chromatic mode
    current_sum_color = hsv(chroma_val + hue_position, frame_config.SATURATION, total_magnitude);
  }

  // Apply PHOTONS brightness scaling
  current_sum_color.r *= frame_config.PHOTONS;
  current_sum_color.g *= frame_config.PHOTONS;
  current_sum_color.b *= frame_config.PHOTONS;

  // Use the chromagram color mix for the waveform
  last_color = current_sum_color;

  // --- Dynamic Fading for Trails ---
  float abs_amp = fabs(waveform_peak_scaled);
  if (abs_amp > 1.0f) abs_amp = 1.0f;

  float max_fade_reduction = 0.10;
  SQ15x16 dynamic_fade_amount = 1.0 - (max_fade_reduction * abs_amp);

  // Apply the dynamic fade
  for (uint16_t i = 0; i < NATIVE_RESOLUTION; i++) {
    leds_16[i].r *= dynamic_fade_amount;
    leds_16[i].g *= dynamic_fade_amount;
    leds_16[i].b *= dynamic_fade_amount;
  }

  // --- Waveform Display ---
  shift_leds_up(leds_16, 1);

  // Use smoothed peak instead of raw peak
  float amp = waveform_peak_scaled_last;

  // Apply threshold to ignore tiny movements
  float threshold = 0.05f;
  if (fabs(amp) < threshold) {
    amp = 0.0f;
  }

  // CRITICAL FIX: waveform_peak_scaled is absolute value (0 to 1)
  // We need to use the actual waveform data to get signed values
  // For now, use a simple oscillation based on chromagram phase
  float oscillation = 0.0f;

  // Create oscillation from dominant chromagram notes
  int active_notes = 0;
  for (uint8_t i = 0; i < 12; i++) {
    if (chromagram_smooth[i] > 0.1) {  // Original threshold
      // Each note contributes to position with different phase
      float contribution = float(chromagram_smooth[i]) * sin(millis() * 0.001f * (1.0f + i * 0.5f));
      oscillation += contribution;
      active_notes++;
    }
  }

  // Normalize oscillation to -1 to +1 range
  oscillation = tanh(oscillation * 2.0f);

  // Mix oscillation with amplitude for more dynamic movement
  amp = oscillation * waveform_peak_scaled_last * 0.7f;

  if (amp > 1.0f) amp = 1.0f;
  else if (amp < -1.0f) amp = -1.0f;

  int center = NATIVE_RESOLUTION / 2;
  float pos_f = center + amp * (NATIVE_RESOLUTION / 2.0f);
  int pos = int(pos_f + (pos_f >= 0 ? 0.5 : -0.5));
  if (pos < 0) pos = 0;
  if (pos >= NATIVE_RESOLUTION) pos = NATIVE_RESOLUTION - 1;

  // Set the new dot with the calculated color
  leds_16[pos] = last_color;

  if (CONFIG.MIRROR_ENABLED) {
    mirror_image_downwards(leds_16);
  }
}

void light_mode_snapwave_debug() {
  static uint32_t debug_call_count = 0;
  if (snapwave_debug_logging_enabled && (debug_call_count++ % 60 == 0)) {
    USBSerial.printf("SNAPWAVE_DEBUG: Test variant executing! Mode index=%d\n",
                     CONFIG.LIGHTSHOW_MODE);
  }

  for (int i = 0; i < NATIVE_RESOLUTION; i++) {
    leds_16[i] = CRGB16{1.0, 0.0, 0.0};
  }
}
