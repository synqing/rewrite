#ifndef LED_UTILITIES_H
#define LED_UTILITIES_H

#include <Arduino.h>
#include <FastLED.h>
#include <FixedPoints.h>
#include <FixedPointsCommon.h>
#include <math.h> // For standard math functions if needed
#include "globals.h" // Assuming globals contains necessary definitions
#include "constants.h" // Assuming constants contains necessary definitions
#include "Palettes.h"  // Added for gradient palettes

extern bool color_shift_debug_logging_enabled;

extern void propagate_noise_cal();
extern void start_noise_cal();

// Forward declarations for secondary LED functions
void scale_to_secondary_strip();
void apply_brightness_secondary();
void show_secondary_leds();
void init_secondary_leds();
void quantize_color_secondary(bool temporal_dither);

// Forward declarations for internal functions needed before their implementations
CRGB16 adjust_hue_and_saturation(CRGB16 color, SQ15x16 hue, SQ15x16 saturation);
void init_gamma_lut();
extern uint8_t gamma_lut[256];

enum blending_modes {
  BLEND_MIX,
  BLEND_ADD,
  BLEND_MULTIPLY,

  NUM_BLENDING_MODES  // used to know the length of this list if it changes in the future
};


CRGB16 interpolate_hue(SQ15x16 hue) {
  // Scale hue to [0, 63]
  SQ15x16 hue_scaled = hue * 63.0;

  // Calculate index1, avoiding expensive floor() call by using typecast to int
  int index1 = hue_scaled.getInteger();

  // If index1 < 0, make it 0. If index1 > 63, make it 63
  index1 = (index1 < 0 ? 0 : (index1 > 63 ? 63 : index1));

  // Calculate index2, minimizing the range to [index1, index1+1]
  int index2 = index1 + 1;

  // If index2 > 63, make it 63
  index2 = (index2 > 63 ? 63 : index2);

  // Compute interpolation factor
  SQ15x16 t = hue_scaled - SQ15x16(index1);
  SQ15x16 t_inv = SQ15x16(1.0) - t;

  CRGB16 output;
  output.r = t_inv * hue_lookup[index1][0] + t * hue_lookup[index2][0];
  output.g = t_inv * hue_lookup[index1][1] + t * hue_lookup[index2][1];
  output.b = t_inv * hue_lookup[index1][2] + t * hue_lookup[index2][2];

  return output;
}


CRGB16 desaturate(CRGB16 input_color, SQ15x16 amount) {
  SQ15x16 luminance = SQ15x16(0.2126) * input_color.r + SQ15x16(0.7152) * input_color.g + SQ15x16(0.0722) * input_color.b;
  SQ15x16 amount_inv = SQ15x16(1.0) - amount;

  CRGB16 output;
  output.r = input_color.r * amount_inv + luminance * amount;
  output.g = input_color.g * amount_inv + luminance * amount;
  output.b = input_color.b * amount_inv + luminance * amount;

  return output;
}

CRGB16 hsv(SQ15x16 h, SQ15x16 s, SQ15x16 v) {
  while (h > 1.0) { h -= 1.0; }
  while (h < 0.0) { h += 1.0; }

  CRGB base_color = CHSV(uint8_t(h * 255.0), uint8_t(s * 255.0), 255);

  CRGB16 col = { base_color.r / 255.0, base_color.g / 255.0, base_color.b / 255.0 };
  //col = desaturate(col, SQ15x16(1.0) - s);

  col.r *= v;
  col.g *= v;
  col.b *= v;

  return col;
}

// Soft-knee compression so temporary HDR (>1.0) values gently roll off
// rather than hard-clipping. «knee_softness» controls how aggressively
// we compress the excess. 0.5 = very gentle, 1.0 = medium (default).
static const SQ15x16 knee_softness = SQ15x16(1.0); // tweakable

void clip_led_values(CRGB16* buffer) { // accept buffer pointer
  for (uint16_t i = 0; i < NATIVE_RESOLUTION; i++) {
    // Floor at 0
    if (buffer[i].r < 0.0) buffer[i].r = 0.0;
    if (buffer[i].g < 0.0) buffer[i].g = 0.0;
    if (buffer[i].b < 0.0) buffer[i].b = 0.0;

    // Soft-knee above 1.0 (HDR). Preserve colour ratio.
    SQ15x16 max_chan = buffer[i].r;
    if (buffer[i].g > max_chan) max_chan = buffer[i].g;
    if (buffer[i].b > max_chan) max_chan = buffer[i].b;

    if (max_chan > SQ15x16(1.0)) {
      // Compression factor:  1 / (1 + (excess * knee_softness))
      SQ15x16 excess = max_chan - SQ15x16(1.0);
      SQ15x16 scale = SQ15x16(1.0) / (SQ15x16(1.0) + excess * knee_softness);

      buffer[i].r *= scale;
      buffer[i].g *= scale;
      buffer[i].b *= scale;
    }

    // Final hard limit to be safe
    if (buffer[i].r > 1.0) buffer[i].r = 1.0;
    if (buffer[i].g > 1.0) buffer[i].g = 1.0;
    if (buffer[i].b > 1.0) buffer[i].b = 1.0;
  }
}

void reverse_leds(CRGB arr[], uint16_t size) {
  uint16_t start = 0;
  uint16_t end = size - 1;
  while (start < end) {
    CRGB temp = arr[start];
    arr[start] = arr[end];
    arr[end] = temp;
    start++;
    end--;
  }
}

void run_sweet_spot() {
  #ifdef ARDUINO_ESP32S3_DEV
  // S3 has no sweet spot hardware, skip entirely
  return;
  #endif
  
  static float sweet_spot_brightness = 0.0;  // init to zero for first fade in

  if (sweet_spot_brightness < 1.0) {
    sweet_spot_brightness += 0.05;
  }
  if (sweet_spot_brightness > 1.0) {
    sweet_spot_brightness = 1.0;
  }

  sweet_spot_state_follower = (sweet_spot_state*0.05) + (sweet_spot_state_follower*0.95);

  uint16_t led_power[3] = { 0, 0, 0 };
  for (float i = -1; i <= 1; i++) {
    float position_delta = fabs(i - sweet_spot_state_follower);
    if (position_delta > 1.0) {
      position_delta = 1.0;
    }

    float led_level = 1.0 - position_delta;
    led_level *= led_level;
    //                                                Never fully dim
    led_power[uint8_t(i + 1)] = 256 * led_level * (0.1 + silent_scale * 0.9) * sweet_spot_brightness * (CONFIG.PHOTONS * CONFIG.PHOTONS);
  }

  #ifndef ARDUINO_ESP32S3_DEV
  // Only write to sweet spot LEDs on S2 (S3 has no sweet spot LEDs)
  ledcWrite(SWEET_SPOT_LEFT_CHANNEL, led_power[0]);
  ledcWrite(SWEET_SPOT_CENTER_CHANNEL, led_power[1]);
  ledcWrite(SWEET_SPOT_RIGHT_CHANNEL, led_power[2]);
  #endif
}


// Returns the linear interpolation of a floating point index in a CRGB array
// index is in the range of 0.0-1.0
CRGB lerp_led_NEW(float index, CRGB* led_array) {
  const uint16_t NUM_LEDS_NATIVE = NATIVE_RESOLUTION - 1;  // count from zero
  uint32_t index_fp = (uint32_t)(index * (float)NUM_LEDS_NATIVE * 256.0f);

  if (index_fp > (NUM_LEDS_NATIVE << 8)) {
    return CRGB::Black;
  }

  uint16_t index_i = index_fp >> 8;
  uint8_t index_f_frac = index_fp & 0xFF;

  // Use FastLED built-in lerp8by8 function for interpolation
  CRGB out_col;
  out_col.r = lerp8by8(led_array[index_i].r, led_array[index_i + 1].r, index_f_frac);
  out_col.g = lerp8by8(led_array[index_i].g, led_array[index_i + 1].g, index_f_frac);
  out_col.b = lerp8by8(led_array[index_i].b, led_array[index_i + 1].b, index_f_frac);

  return out_col;
}

// Returns the linear interpolation of a floating point index in a CRGB16 array
// index is in the range of 0.0 - float(NATIVE_RESOLUTION)
CRGB16 lerp_led_16(SQ15x16 index, CRGB16* led_array) {
  int32_t index_whole = index.getInteger();
  SQ15x16 index_fract = index - (SQ15x16)index_whole;

  int32_t index_left = index_whole + 0;
  int32_t index_right = index_whole + 1;

  SQ15x16 mix_left = SQ15x16(1.0) - index_fract;
  SQ15x16 mix_right = SQ15x16(1.0) - mix_left;

  CRGB16 out_col;
  out_col.r = led_array[index_left].r * mix_left + led_array[index_right].r * mix_right;
  out_col.g = led_array[index_left].g * mix_left + led_array[index_right].g * mix_right;
  out_col.b = led_array[index_left].b * mix_left + led_array[index_right].b * mix_right;

  return out_col;
}

void apply_brightness() {
  // This is only used to fade in when booting!
  if (millis() >= 1000 && noise_transition_queued == false && mode_transition_queued == false) {
    if (MASTER_BRIGHTNESS < 1.0) {
      MASTER_BRIGHTNESS += 0.005;
    }
    if (MASTER_BRIGHTNESS > 1.0) {
      MASTER_BRIGHTNESS = 1.00;
    }
  }

  // ------------------------------------------------------------------
  // HDR photon boost: add extra head-room during loud audio peaks.
  // audio_vu_level ∈ [0,1] (average-normalised). We allow up to +1.0x
  // extra gain (i.e. brightness factor up to 2×) at full scale audio.
  // ------------------------------------------------------------------
  SQ15x16 hdr_boost = SQ15x16(1.0) + (audio_vu_level * SQ15x16(1.0));

  SQ15x16 brightness = MASTER_BRIGHTNESS * (CONFIG.PHOTONS * CONFIG.PHOTONS) * silent_scale * hdr_boost;
  
  if (debug_mode && (millis() % 5000 == 0)) {
    USBSerial.print("DEBUG: Brightness components - MASTER_BRIGHTNESS: ");
    USBSerial.print(MASTER_BRIGHTNESS);
    USBSerial.print(" PHOTONS: ");
    USBSerial.print(CONFIG.PHOTONS);
    USBSerial.print(" PHOTONS²: ");
    USBSerial.print(CONFIG.PHOTONS * CONFIG.PHOTONS);
    USBSerial.print(" silent_scale: ");
    USBSerial.print(float(silent_scale));
    USBSerial.print(" Final brightness (SQ15x16): ");
    USBSerial.print(float(brightness));
    USBSerial.print(" Final brightness (raw): ");
    USBSerial.println(brightness.getInteger());
  }

  for (uint16_t i = 0; i < NATIVE_RESOLUTION; i++) {
    leds_16[i].r *= brightness;
    leds_16[i].g *= brightness;
    leds_16[i].b *= brightness;
  }

  clip_led_values(leds_16);
}

void quantize_color(bool temporal_dithering) {
  // Ensure gamma LUT ready
  init_gamma_lut();
  if (temporal_dithering) {
    dither_step++;
    if (dither_step >= 8) {  // Updated for 8-frame dithering
      dither_step = 0;
    }

    static uint8_t noise_origin_r = 0;  // 0
    static uint8_t noise_origin_g = 0;  // 2
    static uint8_t noise_origin_b = 0;  // 4

    noise_origin_r += 1;
    noise_origin_g += 1;
    noise_origin_b += 1;

    for (uint16_t i = 0; i < CONFIG.LED_COUNT; i += 1) {
      // Skip dithering on near-black pixels to avoid sparkle
      SQ15x16 max_chan = leds_scaled[i].r;
      if (leds_scaled[i].g > max_chan) max_chan = leds_scaled[i].g;
      if (leds_scaled[i].b > max_chan) max_chan = leds_scaled[i].b;
      if (max_chan < SQ15x16(0.003)) {
        leds_out[i].r = leds_out[i].g = leds_out[i].b = 0;
        continue;
      }
      // RED #####################################################
      SQ15x16 decimal_r = leds_scaled[i].r * SQ15x16(254);
      SQ15x16 whole_r = decimal_r.getInteger();
      SQ15x16 fract_r = decimal_r - whole_r;

      if (fract_r >= dither_table[(noise_origin_r + i) % 8]) {
        whole_r += SQ15x16(1);
      }

      leds_out[i].r = gamma_lut[whole_r.getInteger()];

      // GREEN ###################################################
      SQ15x16 decimal_g = leds_scaled[i].g * SQ15x16(254);
      SQ15x16 whole_g = decimal_g.getInteger();
      SQ15x16 fract_g = decimal_g - whole_g;

      if (fract_g >= dither_table[(noise_origin_g + i) % 8]) {
        whole_g += SQ15x16(1);
      }

      leds_out[i].g = gamma_lut[whole_g.getInteger()];

      // BLUE ####################################################
      SQ15x16 decimal_b = leds_scaled[i].b * SQ15x16(254);
      SQ15x16 whole_b = decimal_b.getInteger();
      SQ15x16 fract_b = decimal_b - whole_b;

      if (fract_b >= dither_table[(noise_origin_b + i) % 8]) {
        whole_b += SQ15x16(1);
      }

      leds_out[i].b = gamma_lut[whole_b.getInteger()];
    }
  } else {
    for (uint16_t i = 0; i < CONFIG.LED_COUNT; i += 1) {
      SQ15x16 max_chan = leds_scaled[i].r;
      if (leds_scaled[i].g > max_chan) max_chan = leds_scaled[i].g;
      if (leds_scaled[i].b > max_chan) max_chan = leds_scaled[i].b;
      if (max_chan < SQ15x16(0.003)) {
        leds_out[i].r = leds_out[i].g = leds_out[i].b = 0;
        continue;
      }
      leds_out[i].r = gamma_lut[ uint8_t(leds_scaled[i].r * 255) ];
      leds_out[i].g = gamma_lut[ uint8_t(leds_scaled[i].g * 255) ];
      leds_out[i].b = gamma_lut[ uint8_t(leds_scaled[i].b * 255) ];
    }
  }
}

void apply_incandescent_filter() {
  SQ15x16 mix = CONFIG.INCANDESCENT_FILTER;
  SQ15x16 inv_mix = 1.0 - mix;

  for (uint8_t i = 0; i < NATIVE_RESOLUTION; i++) {
    SQ15x16 filtered_r = leds_16[i].r * incandescent_lookup.r;
    SQ15x16 filtered_g = leds_16[i].g * incandescent_lookup.g;
    SQ15x16 filtered_b = leds_16[i].b * incandescent_lookup.b;

    leds_16[i].r = (leds_16[i].r * inv_mix) + (filtered_r * mix);
    leds_16[i].g = (leds_16[i].g * inv_mix) + (filtered_g * mix);
    leds_16[i].b = (leds_16[i].b * inv_mix) + (filtered_b * mix);

    /*
    SQ15x16 max_val = 0;
    if (leds_16[i].r > max_val) { max_val = leds_16[i].r; }
    if (leds_16[i].g > max_val) { max_val = leds_16[i].g; }
    if (leds_16[i].b > max_val) { max_val = leds_16[i].b; }

    SQ15x16 leakage = (max_val >> 2) * mix;

    CRGB base_col = CRGB(
      (uint16_t(leds[i].r * (255 - leakage)) >> 8),
      (uint16_t(leds[i].g * (255 - leakage)) >> 8),
      (uint16_t(leds[i].b * (255 - leakage)) >> 8));

    CRGB leak_col = CRGB(
      uint16_t((uint16_t(incandescent_lookup.r * (leakage)) >> 8) * max_val) >> 8,
      uint16_t((uint16_t(incandescent_lookup.g * (leakage)) >> 8) * max_val) >> 8,
      uint16_t((uint16_t(incandescent_lookup.b * (leakage)) >> 8) * max_val) >> 8);

    leds[i].r = base_col.r + leak_col.r;
    leds[i].g = base_col.g + leak_col.g;
    leds[i].b = base_col.b + leak_col.b;
    */
  }
}

void set_dot_position(uint16_t dot_index, SQ15x16 new_pos) {
  dots[dot_index].last_position = dots[dot_index].position;
  dots[dot_index].position = new_pos;
}

void draw_line(CRGB16* layer, SQ15x16 x1, SQ15x16 x2, CRGB16 color, SQ15x16 alpha) {
  bool lighten = true;
  if (color.r == 0 && color.g == 0 && color.b == 0) {
    lighten = false;
  }

  x1 *= (SQ15x16)(NATIVE_RESOLUTION - 1);
  x2 *= (SQ15x16)(NATIVE_RESOLUTION - 1);

  if (x1 > x2) {  // Ensure x1 <= x2
    SQ15x16 temp = x1;
    x1 = x2;
    x2 = temp;
  }

  SQ15x16 ix1 = floorFixed(x1);
  SQ15x16 ix2 = ceilFixed(x2);

  // start pixel
  if (ix1 >= 0 && ix1 < NATIVE_RESOLUTION) {
    SQ15x16 coverage = 1.0 - (x1 - ix1);
    SQ15x16 mix = alpha * coverage;

    if (lighten == true) {
      layer[ix1.getInteger()].r += color.r * mix;
      layer[ix1.getInteger()].g += color.g * mix;
      layer[ix1.getInteger()].b += color.b * mix;
    } else {
      layer[ix1.getInteger()].r = layer[ix1.getInteger()].r * (1.0 - mix) + color.r * mix;
      layer[ix1.getInteger()].g = layer[ix1.getInteger()].g * (1.0 - mix) + color.g * mix;
      layer[ix1.getInteger()].b = layer[ix1.getInteger()].b * (1.0 - mix) + color.b * mix;
    }
  }

  // end pixel
  if (ix2 >= 0 && ix2 < NATIVE_RESOLUTION) {
    SQ15x16 coverage = x2 - floorFixed(x2);
    SQ15x16 mix = alpha * coverage;

    if (lighten == true) {
      layer[ix2.getInteger()].r += color.r * mix;
      layer[ix2.getInteger()].g += color.g * mix;
      layer[ix2.getInteger()].b += color.b * mix;
    } else {
      layer[ix2.getInteger()].r = layer[ix2.getInteger()].r * (1.0 - mix) + color.r * mix;
      layer[ix2.getInteger()].g = layer[ix2.getInteger()].g * (1.0 - mix) + color.g * mix;
      layer[ix2.getInteger()].b = layer[ix2.getInteger()].b * (1.0 - mix) + color.b * mix;
    }
  }

  // pixels in between
  for (SQ15x16 i = ix1 + 1; i < ix2; i++) {
    if (i >= 0 && i < NATIVE_RESOLUTION) {
      layer[i.getInteger()].r += color.r * alpha;
      layer[i.getInteger()].g += color.g * alpha;
      layer[i.getInteger()].b += color.b * alpha;

      if (lighten == true) {
        layer[i.getInteger()].r += color.r * alpha;
        layer[i.getInteger()].g += color.g * alpha;
        layer[i.getInteger()].b += color.b * alpha;
      } else {
        layer[i.getInteger()].r = layer[i.getInteger()].r * (1.0 - alpha) + color.r * alpha;
        layer[i.getInteger()].g = layer[i.getInteger()].g * (1.0 - alpha) + color.g * alpha;
        layer[i.getInteger()].b = layer[i.getInteger()].b * (1.0 - alpha) + color.b * alpha;
      }
    }
  }
}

void draw_dot(CRGB16* layer, uint16_t dot_index, CRGB16 color) {
  SQ15x16 position = dots[dot_index].position;
  SQ15x16 last_position = dots[dot_index].last_position;

  SQ15x16 positional_distance = fabs_fixed(position - last_position);
  if (positional_distance < 1.0) {
    positional_distance = 1.0;
  }

  SQ15x16 net_brightness_per_pixel = 1.0 / positional_distance;
  if (net_brightness_per_pixel > 1.0) {
    net_brightness_per_pixel = 1.0;
  }

  draw_line(
    layer,
    position,
    last_position,
    color,
    net_brightness_per_pixel);
}

void render_photons_graph() {
  // Draw graph ticks
  uint8_t ticks = 5;
  SQ15x16 tick_distance = (0.425 / (ticks - 1));
  SQ15x16 tick_pos = 0.025;

  CRGB16 background = { 0.0, 0.0, 0.0 };
  CRGB16 needle_color = { incandescent_lookup.r * incandescent_lookup.r * 0.9, incandescent_lookup.g * incandescent_lookup.g * 0.9, incandescent_lookup.b * incandescent_lookup.b * 0.9 };

  //draw_line(leds_16_ui, 0.0, 0.5, background, 1.0);

  memset(leds_16_ui, 0, sizeof(CRGB16) * NATIVE_RESOLUTION);

  for (uint8_t i = 0; i < ticks; i++) {
    SQ15x16 prog = i / float(ticks);
    SQ15x16 tick_brightness = 0.2 + 0.4 * prog;
    tick_brightness *= tick_brightness;
    tick_brightness *= tick_brightness;
    CRGB16 tick_color = { 1.0 * tick_brightness, 0, 0 };

    set_dot_position(GRAPH_DOT_1 + i, tick_pos);
    draw_dot(leds_16_ui, GRAPH_DOT_1 + i, tick_color);
    tick_pos += tick_distance;
  }

  SQ15x16 needle_pos = 0.025 + (0.425 * CONFIG.PHOTONS);

  // Draw needle
  set_dot_position(GRAPH_NEEDLE, needle_pos);
  draw_dot(leds_16_ui, GRAPH_NEEDLE, needle_color);
}

void render_chroma_graph() {
  memset(leds_16_ui, 0, sizeof(CRGB16) * NATIVE_RESOLUTION);

  SQ15x16 half_height = NATIVE_RESOLUTION >> 1;
  SQ15x16 quarter_height = NATIVE_RESOLUTION >> 2;

  if (chromatic_mode == false) {
    for (SQ15x16 i = 5; i < half_height - 5; i++) {
      SQ15x16 prog = i / half_height;

      SQ15x16 distance_to_center = fabs_fixed(i - quarter_height);
      SQ15x16 brightness;  // = SQ15x16(1.0) - distance_to_center / quarter_height;

      if (distance_to_center < 3) {
        brightness = 1.0;
      } else if (distance_to_center < 5) {
        brightness = 0.0;
      } else {
        brightness = 0.20;
      }

      leds_16_ui[i.getInteger()] = hsv((SQ15x16(chroma_val + hue_position) - 0.48) + prog, CONFIG.SATURATION, brightness * brightness);
    }
  } else {
    SQ15x16 dot_pos = 0.025;
    SQ15x16 dot_distance = (0.425 / (12 - 1));

    static float radians = 0.0;
    radians -= 0.025;

    for (uint8_t i = 0; i < 12; i++) {
      SQ15x16 wave = sin(radians + (i * 0.5)) * 0.4 + 0.6;

      CRGB16 dot_color = hsv(SQ15x16(i / 12.0), CONFIG.SATURATION, wave * wave);
      set_dot_position(MAX_DOTS - 1 - i, dot_pos);
      draw_dot(leds_16_ui, MAX_DOTS - 1 - i, dot_color);

      dot_pos += dot_distance;
    }
  }
}

void render_mood_graph() {
  // Draw graph ticks
  uint8_t ticks = 5;
  SQ15x16 tick_distance = (0.425 / (ticks - 1));
  SQ15x16 tick_pos = 0.025;

  CRGB16 background = { 0.0, 0.0, 0.0 };
  CRGB16 needle_color = { incandescent_lookup.r * incandescent_lookup.r * 0.9, incandescent_lookup.g * incandescent_lookup.g * 0.9, incandescent_lookup.b * incandescent_lookup.b * 0.9 };

  //draw_line(leds_16_ui, 0.0, 0.5, background, 1.0);

  memset(leds_16_ui, 0, sizeof(CRGB16) * NATIVE_RESOLUTION);

  static float radians = 0.0;
  radians -= 0.02;

  for (uint8_t i = 0; i < ticks; i++) {
    SQ15x16 tick_brightness = 0.1;  // + (0.025 * sin(radians * (1 << i)));  // + (0.04 * sin(radians * ((i<<1)+1)));
    SQ15x16 mix = i / float(ticks - 1);

    CRGB16 tick_color = { tick_brightness * mix, 0.05 * tick_brightness, tick_brightness * (1.0 - mix) };

    set_dot_position(GRAPH_DOT_1 + i, tick_pos + (0.008 * sin(radians * (1 << i))));
    draw_dot(leds_16_ui, GRAPH_DOT_1 + i, tick_color);
    tick_pos += tick_distance;
  }

  SQ15x16 needle_pos = 0.025 + (0.425 * CONFIG.MOOD);

  // Draw needle
  set_dot_position(GRAPH_NEEDLE, needle_pos);
  draw_dot(leds_16_ui, GRAPH_NEEDLE, needle_color);
}

void transition_ui_mask_to_height(SQ15x16 target_height) {
  SQ15x16 distance = fabs_fixed(ui_mask_height - target_height);
  if (ui_mask_height > target_height) {
    ui_mask_height -= distance * 0.05;
  } else if (ui_mask_height < target_height) {
    ui_mask_height += distance * 0.05;
  }

  if (ui_mask_height < 0.0) {
    ui_mask_height = 0.0;
  } else if (ui_mask_height > 1.0) {
    ui_mask_height = 1.0;
  }

  memset(ui_mask, 0, sizeof(SQ15x16) * NATIVE_RESOLUTION);
  for (uint8_t i = 0; i < NATIVE_RESOLUTION * ui_mask_height; i++) {
    ui_mask[i] = SQ15x16(1.0);
  }
}

void render_noise_cal() {
  // Noise cal UI
  float noise_cal_progress = (float)noise_iterations / 256.0f; // Ensure float division

  uint16_t half_res = NATIVE_RESOLUTION >> 1;
  uint16_t prog_led_index = half_res * noise_cal_progress;
  float max_val = 0.0;
  for (uint16_t i = 0; i < NUM_FREQS; i++) {
    if (noise_samples[i] > max_val) {
      max_val = float(noise_samples[i]);
    }
  }
  for (uint16_t i = 0; i < half_res; i++) {
    if (i < prog_led_index) {
      float led_level = float(noise_samples[i]) / max_val;
      led_level = led_level * 0.9 + 0.1;
      leds_16_ui[half_res + i] = hsv(0.859, CONFIG.SATURATION, led_level * led_level);
      leds_16_ui[half_res - 1 - i] = leds_16_ui[half_res + i]; // Corrected mirror index
    } else if (i == prog_led_index) {
      leds_16_ui[half_res + i] = hsv(0.875, 1.0, 1.0);
      leds_16_ui[half_res - 1 - i] = leds_16_ui[half_res + i]; // Corrected mirror index

      ui_mask[half_res + i] = 1.0;
      ui_mask[half_res - 1 - i] = ui_mask[half_res + i]; // Corrected mirror index
    } else {
      leds_16_ui[half_res + i] = {0,0,0}; // Use CRGB16 zero initializer
      leds_16_ui[half_res - 1 - i] = leds_16_ui[half_res + i]; // Corrected mirror index
    }
  }

  if (noise_iterations > 192) {  // fade out towards end of calibration
    uint16_t iters_left = 256 - noise_iterations; // Fade over last 64 iterations
    float brightness_level = (float)iters_left / 64.0f; // Ensure float division
    brightness_level *= brightness_level;
  }
}

void render_ui() {
  if (noise_complete == true) {
    if (current_knob == K_NONE) {
      // Close UI if open
      if (ui_mask_height > 0.005) {
        transition_ui_mask_to_height(0.0);
      }
    } else {
      if (current_knob == K_PHOTONS) {
        render_photons_graph();
      } else if (current_knob == K_CHROMA) {
        render_chroma_graph();
      } else if (current_knob == K_MOOD) {
        render_mood_graph();
      }

      // Open UI if closed
      transition_ui_mask_to_height(0.5);
    }
  } else {
    render_noise_cal();
  }

  if (ui_mask_height > 0.005 || noise_complete == false) {
    for (uint8_t i = 0; i < NATIVE_RESOLUTION; i++) {
      SQ15x16 mix = ui_mask[i];
      SQ15x16 mix_inv = SQ15x16(1.0) - mix;

      if (mix > 0.0) {
        leds_16[i].r = leds_16[i].r * mix_inv + leds_16_ui[i].r * mix;
        leds_16[i].g = leds_16[i].g * mix_inv + leds_16_ui[i].g * mix;
        leds_16[i].b = leds_16[i].b * mix_inv + leds_16_ui[i].b * mix;
      }
    }
  }
}

struct LerpParams {
    int32_t index_left;
    int32_t index_right;
    SQ15x16 mix_left;
    SQ15x16 mix_right;
};
LerpParams* led_lerp_params = NULL;
bool lerp_params_initialized = false;

void init_lerp_params() {
    if (CONFIG.LED_COUNT != NATIVE_RESOLUTION && !lerp_params_initialized) {
        if (led_lerp_params) delete[] led_lerp_params;
        led_lerp_params = new LerpParams[CONFIG.LED_COUNT];
        
        for (uint16_t i = 0; i < CONFIG.LED_COUNT; i++) {
            SQ15x16 prog = SQ15x16(i) / SQ15x16(CONFIG.LED_COUNT);
            SQ15x16 index = prog * SQ15x16(NATIVE_RESOLUTION);
            
            led_lerp_params[i].index_left = index.getInteger();
            led_lerp_params[i].index_right = led_lerp_params[i].index_left + 1;
            SQ15x16 index_fract = index - SQ15x16(led_lerp_params[i].index_left);
            led_lerp_params[i].mix_left = SQ15x16(1.0) - index_fract;
            led_lerp_params[i].mix_right = index_fract;
        }
        lerp_params_initialized = true;
    }
}


void scale_to_strip() {
    if (!leds_scaled) {
        return;
    }
    
    if (CONFIG.LED_COUNT == NATIVE_RESOLUTION) {
        memcpy(leds_scaled, leds_16, sizeof(CRGB16)*NATIVE_RESOLUTION);
    } else {
        if (!lerp_params_initialized) {
            init_lerp_params();
        }
        
        for (uint16_t i = 0; i < CONFIG.LED_COUNT; i++) {
            int32_t index_left = led_lerp_params[i].index_left;
            int32_t index_right = led_lerp_params[i].index_right;
            SQ15x16 mix_left = led_lerp_params[i].mix_left;
            SQ15x16 mix_right = led_lerp_params[i].mix_right;
            
            leds_scaled[i].r = leds_16[index_left].r * mix_left + leds_16[index_right].r * mix_right;
            leds_scaled[i].g = leds_16[index_left].g * mix_left + leds_16[index_right].g * mix_right;
            leds_scaled[i].b = leds_16[index_left].b * mix_left + leds_16[index_right].b * mix_right;
        }
    }
}

void show_leds() {
  apply_brightness();

  // Tint the color image with an incandescent LUT to reduce harsh blues
  if (CONFIG.INCANDESCENT_FILTER > 0.0) {
    apply_incandescent_filter();
  }

  if (CONFIG.BASE_COAT == true) {
    if (CONFIG.PHOTONS <= 0.05) {
      base_coat_width_target = 0.0;
    } else {
      base_coat_width_target = 1.0;
    }

    SQ15x16 transition_speed = 0.05;
    if (base_coat_width < base_coat_width_target) {
      base_coat_width += (base_coat_width_target - base_coat_width) * transition_speed;
    } else if (base_coat_width > base_coat_width_target) {
      base_coat_width -= (base_coat_width - base_coat_width_target) * transition_speed;
    }

    SQ15x16 backdrop_divisor = 256.0;

    SQ15x16 bottom_value_r = 1 / backdrop_divisor;
    SQ15x16 bottom_value_g = 1 / backdrop_divisor;
    SQ15x16 bottom_value_b = 1 / backdrop_divisor;

    CRGB16 backdrop_color = { bottom_value_r, bottom_value_g, bottom_value_b };

    SQ15x16 base_coat_width_scaled = base_coat_width * silent_scale;

    if (base_coat_width_scaled > 0.01) {
      draw_line(leds_16, 0.5 - (base_coat_width_scaled * 0.5), 0.5 + (base_coat_width_scaled * 0.5), backdrop_color, 1.0);
    }

    /*
    for (uint8_t i = 0; i < 128; i++) {
      if (leds_16[i].r < bottom_value_r) { leds_16[i].r = bottom_value_r; }
      if (leds_16[i].g < bottom_value_g) { leds_16[i].g = bottom_value_g; }
      if (leds_16[i].b < bottom_value_b) { leds_16[i].b = bottom_value_b; }
    }
    */
  }

  render_ui();
  clip_led_values(leds_16);
  scale_to_strip();
  
  // Only attempt to use secondary LEDs if explicitly enabled
  if (ENABLE_SECONDARY_LEDS) {
    show_secondary_leds();
  }
  
  quantize_color(CONFIG.TEMPORAL_DITHERING);

  if (CONFIG.REVERSE_ORDER == true) {
    reverse_leds(leds_out, CONFIG.LED_COUNT);
  }

  if (debug_mode && (millis() % 10000 == 0)) {
    bool has_light = false;
    uint16_t first_nonzero = NATIVE_RESOLUTION;
    uint16_t last_nonzero = 0;
    
    for (uint16_t i = 0; i < CONFIG.LED_COUNT; i++) {
      if (leds_out[i].r > 0 || leds_out[i].g > 0 || leds_out[i].b > 0) {
        has_light = true;
        if (i < first_nonzero) first_nonzero = i;
        if (i > last_nonzero) last_nonzero = i;
      }
    }
    
    USBSerial.print("DEBUG: LED Output - HasLight: ");
    USBSerial.print(has_light ? "YES" : "NO");
    if (has_light) {
      USBSerial.print(" Range: ");
      USBSerial.print(first_nonzero);
      USBSerial.print("-");
      USBSerial.print(last_nonzero);
      USBSerial.print(" (");
      USBSerial.print(last_nonzero - first_nonzero + 1);
      USBSerial.print(" LEDs)");
    }
    USBSerial.println();
  }

  FastLED.setDither(false);
  FastLED.show(); // This will update both LED strips

  // Add inside show_leds() function, just before FastLED.show()
  if (debug_mode && (millis() % 5000 == 0)) {
    USBSerial.print("DEBUG: Using modes - Primary: ");
    USBSerial.print(CONFIG.LIGHTSHOW_MODE);
    USBSerial.print(" (");
    USBSerial.print(mode_names + (CONFIG.LIGHTSHOW_MODE * 32));
    USBSerial.print(")");
    
    if (ENABLE_SECONDARY_LEDS) {
      USBSerial.print(", Secondary: ");
      USBSerial.print(SECONDARY_LIGHTSHOW_MODE);
      USBSerial.print(" (");
      USBSerial.print(mode_names + (SECONDARY_LIGHTSHOW_MODE * 32));
      USBSerial.print(")");
    }
    
    USBSerial.println();
  }
}

void init_leds() {
  bool leds_started = false;

  // TACTICAL FIX: Validate LED_COUNT before allocation
  if (CONFIG.LED_COUNT == 0 || CONFIG.LED_COUNT > 1000) {
    USBSerial.println("ERROR: Invalid LED_COUNT in config! Using default 128");
    CONFIG.LED_COUNT = 128;
  }

  leds_scaled = new CRGB16[CONFIG.LED_COUNT];
  leds_out = new CRGB[CONFIG.LED_COUNT];
  
  // TACTICAL FIX: Check allocation success
  if (leds_scaled == nullptr || leds_out == nullptr) {
    USBSerial.println("ERROR: Failed to allocate LED buffers!");
    ESP.restart();
  }
  
  // CRITICAL FIX: Allocate secondary LED buffers if enabled
  if (ENABLE_SECONDARY_LEDS) {
    leds_scaled_secondary = new CRGB16[SECONDARY_LED_COUNT];
    leds_out_secondary = new CRGB[SECONDARY_LED_COUNT];
    
    if (leds_scaled_secondary == nullptr || leds_out_secondary == nullptr) {
      USBSerial.println("ERROR: Failed to allocate secondary LED buffers!");
      ESP.restart();
    }
    
    // Initialize secondary buffers to black
    for (uint16_t i = 0; i < SECONDARY_LED_COUNT; i++) {
      leds_scaled_secondary[i] = {0, 0, 0};
      leds_out_secondary[i] = CRGB(0, 0, 0);
    }
  }
  
  // Initialize the lerp parameters for scale_to_strip optimization
  init_lerp_params();

  if (CONFIG.LED_TYPE == LED_NEOPIXEL) {
    if (CONFIG.LED_COLOR_ORDER == RGB) {
      FastLED.addLeds<WS2812B, LED_DATA_PIN, RGB>(leds_out, CONFIG.LED_COUNT);
    } else if (CONFIG.LED_COLOR_ORDER == GRB) {
      FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(leds_out, CONFIG.LED_COUNT);
    } else if (CONFIG.LED_COLOR_ORDER == BGR) {
      FastLED.addLeds<WS2812B, LED_DATA_PIN, BGR>(leds_out, CONFIG.LED_COUNT);
    }
  }

  else if (CONFIG.LED_TYPE == LED_NEOPIXEL_X2) {
    if (CONFIG.LED_COLOR_ORDER == RGB) {
      FastLED.addLeds< WS2812B, LED_DATA_PIN_1,  RGB >(leds_out, 0, CONFIG.LED_COUNT / 2);
      FastLED.addLeds< WS2812B, LED_DATA_PIN_2, RGB >(leds_out, CONFIG.LED_COUNT / 2, CONFIG.LED_COUNT / 2);
    } else if (CONFIG.LED_COLOR_ORDER == GRB) {
      FastLED.addLeds< WS2812B, LED_DATA_PIN_1,  GRB >(leds_out, 0, CONFIG.LED_COUNT / 2);
      FastLED.addLeds< WS2812B, LED_DATA_PIN_2, GRB >(leds_out, CONFIG.LED_COUNT / 2, CONFIG.LED_COUNT / 2);
    } else if (CONFIG.LED_COLOR_ORDER == BGR) {
      FastLED.addLeds< WS2812B, LED_DATA_PIN_1,  BGR >(leds_out, 0, CONFIG.LED_COUNT / 2);
      FastLED.addLeds< WS2812B, LED_DATA_PIN_2, BGR >(leds_out, CONFIG.LED_COUNT / 2, CONFIG.LED_COUNT / 2);
    }
  }

  else if (CONFIG.LED_TYPE == LED_DOTSTAR) {
    if (CONFIG.LED_COLOR_ORDER == RGB) {
      FastLED.addLeds<DOTSTAR, LED_DATA_PIN, LED_CLOCK_PIN, RGB>(leds_out, CONFIG.LED_COUNT);
    } else if (CONFIG.LED_COLOR_ORDER == GRB) {
      FastLED.addLeds<DOTSTAR, LED_DATA_PIN, LED_CLOCK_PIN, GRB>(leds_out, CONFIG.LED_COUNT);
    } else if (CONFIG.LED_COLOR_ORDER == BGR) {
      FastLED.addLeds<DOTSTAR, LED_DATA_PIN, LED_CLOCK_PIN, BGR>(leds_out, CONFIG.LED_COUNT);
    }
  }

  FastLED.setMaxPowerInVoltsAndMilliamps(5.0, CONFIG.MAX_CURRENT_MA);

  for (uint16_t x = 0; x < CONFIG.LED_COUNT; x++) {
    leds_out[x] = CRGB(0, 0, 0);
  }
  FastLED.show();  // Just show the LEDs directly during init instead of calling show_leds()
  delay(100); // Give FastLED time to initialize on S3

  leds_started = true;

  USBSerial.print("INIT_LEDS: ");
  USBSerial.println(leds_started == true ? SB_PASS : SB_FAIL);
}

void blocking_flash(CRGB16 col) {
  led_thread_halt = true;
  for (uint8_t i = 0; i < NATIVE_RESOLUTION; i++) {
    leds_16[i] = { 0, 0, 0 };
  }

  const uint8_t flash_times = 2;
  for (uint8_t f = 0; f < flash_times; f++) {
    for (uint8_t i = 0 + 48; i < NATIVE_RESOLUTION - 48; i++) {
      leds_16[i] = col;
    }
    show_leds();
    FastLED.delay(150);

    for (uint8_t i = 0; i < NATIVE_RESOLUTION; i++) {
      leds_16[i] = { 0, 0, 0 };
    }
    show_leds();
    FastLED.delay(150);
  }
  led_thread_halt = false;
}

void clear_all_led_buffers() {
  for (uint8_t i = 0; i < NATIVE_RESOLUTION; i++) {
    leds_16[i] = { 0, 0, 0 };
    leds_16_temp[i] = { 0, 0, 0 };
    leds_16_fx[i] = { 0, 0, 0 };
  }

  for (uint16_t i = 0; i < CONFIG.LED_COUNT; i++) {
    leds_scaled[i] = { 0, 0, 0 };
    leds_out[i] = CRGB(0, 0, 0);
  }
}

void scale_image_to_half(CRGB16* led_array) {
  for (uint16_t i = 0; i < (NATIVE_RESOLUTION >> 1); i++) {
    leds_16_temp[i].r = led_array[i << 1].r * SQ15x16(0.5) + led_array[(i << 1) + 1].r * SQ15x16(0.5);
    leds_16_temp[i].g = led_array[i << 1].g * SQ15x16(0.5) + led_array[(i << 1) + 1].g * SQ15x16(0.5);
    leds_16_temp[i].b = led_array[i << 1].b * SQ15x16(0.5) + led_array[(i << 1) + 1].b * SQ15x16(0.5);
    // Clear the second half of the temp buffer
    leds_16_temp[(NATIVE_RESOLUTION >> 1) + i] = { 0, 0, 0 };
  }

  memcpy(led_array, leds_16_temp, sizeof(CRGB16) * NATIVE_RESOLUTION);
}

void unmirror() {
  for (uint16_t i = 0; i < NATIVE_RESOLUTION; i++) {  // Interpolation
    SQ15x16 index = (NATIVE_RESOLUTION >> 1) + (i / 2.0);

    int32_t index_whole = index.getInteger();
    SQ15x16 index_fract = index - (SQ15x16)index_whole;

    int32_t index_left = index_whole + 0;
    int32_t index_right = index_whole + 1;

    SQ15x16 mix_left = SQ15x16(1.0) - index_fract;
    SQ15x16 mix_right = SQ15x16(1.0) - mix_left;

    CRGB16 out_col;
    out_col.r = leds_16[index_left].r * mix_left + leds_16[index_right].r * mix_right;
    out_col.g = leds_16[index_left].g * mix_left + leds_16[index_right].g * mix_right;
    out_col.b = leds_16[index_left].b * mix_left + leds_16[index_right].b * mix_right;

    leds_16_temp[i] = out_col;
  }

  memcpy(leds_16, leds_16_temp, sizeof(CRGB16) * NATIVE_RESOLUTION);
}

void shift_leds_up(CRGB16* led_array, uint16_t offset) {
  memcpy(leds_16_temp, led_array, sizeof(CRGB16) * NATIVE_RESOLUTION);
  memcpy(led_array + offset, leds_16_temp, (NATIVE_RESOLUTION - offset) * sizeof(CRGB16));
  memset(led_array, 0, offset * sizeof(CRGB16));
}

void shift_leds_down(CRGB* led_array, uint16_t offset) {
  memcpy(led_array, led_array + offset, (NATIVE_RESOLUTION - offset) * sizeof(CRGB));
  memset(led_array + (NATIVE_RESOLUTION - offset), 0, offset * sizeof(CRGB));
}

void mirror_image_downwards(CRGB16* led_array) {
  uint16_t half_res = NATIVE_RESOLUTION >> 1;
  for (uint16_t i = 0; i < half_res; i++) { // Loop up to half resolution
    // Copy the pixel from the second half
    leds_16_temp[half_res + i] = led_array[half_res + i];
    // Mirror it to the first half (e.g., index 159 mirrors to 0, 158 to 1, etc.)
    leds_16_temp[half_res - 1 - i] = led_array[half_res + i];
  }

  memcpy(led_array, leds_16_temp, sizeof(CRGB16) * NATIVE_RESOLUTION);
}

void intro_animation() {
  MASTER_BRIGHTNESS = 1.0;
  #ifndef ARDUINO_ESP32S3_DEV
  ledcWrite(SWEET_SPOT_LEFT_CHANNEL, 0.0 * 4096);
  ledcWrite(SWEET_SPOT_CENTER_CHANNEL, 0.0 * 4096);
  ledcWrite(SWEET_SPOT_RIGHT_CHANNEL, 0.0 * 4096);
  #endif

  for (float progress = 0.3; progress <= 0.925; progress += 0.01) {
    float total_vals = 0.925 - 0.3;
    float brightness = (progress - 0.3) / total_vals;

    brightness *= brightness;

    MASTER_BRIGHTNESS = brightness;
    #ifndef ARDUINO_ESP32S3_DEV
    ledcWrite(SWEET_SPOT_LEFT_CHANNEL, brightness * 4096);
    ledcWrite(SWEET_SPOT_RIGHT_CHANNEL, brightness * 4096);
    #endif

    float pos = (cos(progress * 5) + 1) / 2.0;
    float pos_whole = pos * NATIVE_RESOLUTION;
    for (uint8_t i = 0; i < NATIVE_RESOLUTION; i++) {
      float delta = fabs(pos_whole - i);
      if (delta > 5.0) {
        delta = 5.0;
      }
      float led_level = 1.0 - (delta / 5.0);
      CRGB16 out_col = hsv(progress, 0, led_level);
      leds_16[i] = out_col;
    }
    show_leds();
  }

  clear_all_led_buffers();

  const uint8_t particle_count = 16;
  struct particle {
    float phase;
    float speed;
    CRGB16 col;
  };
  particle particles[particle_count];

  for (uint8_t i = 0; i < particle_count; i++) {
    float prog = i / float(particle_count);
    particles[i].phase = 0.0;
    particles[i].speed = 0.002 * (i + 1);
    particles[i].col = hsv(prog, CONFIG.SATURATION, 1.0);
  }
  MASTER_BRIGHTNESS = 1.0;
  float center_brightness = 0.0;

  for (uint16_t i = 0; i < 50; i++) {
    if (center_brightness < 1.0) {
      center_brightness += 0.2;
      #ifndef ARDUINO_ESP32S3_DEV
      ledcWrite(SWEET_SPOT_CENTER_CHANNEL, (center_brightness * center_brightness) * 4096);
      #endif
    }

    float dimming = 1.0;
    float anim_prog = i / 50.0;
    if (anim_prog >= 0.5) {
      anim_prog = (anim_prog - 0.5) * 2.0;
      dimming = 1.0 - anim_prog;
      dimming *= dimming;
      MASTER_BRIGHTNESS = dimming;
      #ifndef ARDUINO_ESP32S3_DEV
      ledcWrite(SWEET_SPOT_LEFT_CHANNEL, 0);
      ledcWrite(SWEET_SPOT_CENTER_CHANNEL, dimming * 4096);
      ledcWrite(SWEET_SPOT_RIGHT_CHANNEL, 0);
      #endif
    } else {
      anim_prog *= 2.0;
      dimming = 1.0 - anim_prog;
      dimming *= dimming;
      #ifndef ARDUINO_ESP32S3_DEV
      ledcWrite(SWEET_SPOT_LEFT_CHANNEL, dimming * 4096);
      ledcWrite(SWEET_SPOT_RIGHT_CHANNEL, dimming * 4096);
      #endif
    }

    clear_all_led_buffers();

    for (uint8_t p = 0; p < particle_count; p++) {
      particles[p].phase += particles[p].speed;

      float pos = (sin(particles[p].phase * 5) + 1) / 2.0;
      float pos_whole = pos * NATIVE_RESOLUTION;
      for (uint8_t pix = 0; pix < NATIVE_RESOLUTION; pix++) {
        float delta = fabs(pos_whole - pix);
        if (delta > 10.0) {
          delta = 10.0;
        }
        float led_level = 1.0 - (delta / 10.0);
        led_level *= led_level;
        CRGB16 out_col = particles[p].col;
        out_col.r *= led_level;
        out_col.g *= led_level;
        out_col.b *= led_level;
        leds_16[pix].r += out_col.r;
        leds_16[pix].g += out_col.g;
        leds_16[pix].b += out_col.b;
      }
    }
    show_leds();
    FastLED.delay(1);
  }
  MASTER_BRIGHTNESS = 0.0;
  #ifndef ARDUINO_ESP32S3_DEV
  ledcWrite(SWEET_SPOT_LEFT_CHANNEL, 0);
  ledcWrite(SWEET_SPOT_CENTER_CHANNEL, 0);
  ledcWrite(SWEET_SPOT_RIGHT_CHANNEL, 0);
  #endif
}

void run_transition_fade() {
  if (MASTER_BRIGHTNESS > 0.0) {
    MASTER_BRIGHTNESS -= 0.02;

    if (MASTER_BRIGHTNESS < 0.0) {
      MASTER_BRIGHTNESS = 0.0;
    }
  } else {
    if (mode_transition_queued == true) {  // If transition for MODE button press
      mode_transition_queued = false;
      if (mode_destination == -1) {  // Triggered via button
        CONFIG.LIGHTSHOW_MODE++;
        if (CONFIG.LIGHTSHOW_MODE >= NUM_MODES) {
          CONFIG.LIGHTSHOW_MODE = 0;
        }
      } else {  // Triggered via Serial
        CONFIG.LIGHTSHOW_MODE = mode_destination;
        mode_destination = -1;
      }
    }

    if (noise_transition_queued == true) {  // If transition for NOISE button press
      noise_transition_queued = false;
      // start noise cal
      if (debug_mode) {
        USBSerial.println("COLLECTING AMBIENT NOISE SAMPLES...");
      }
      propagate_noise_cal();
      start_noise_cal();
    }
  }
}

/*
void distort_exponential() {
  for (uint8_t i = 0; i < NATIVE_RESOLUTION; i++) {
    float prog = i / float(NATIVE_RESOLUTION - 1);
    float prog_distorted = prog * prog;
    leds_fx[i] = lerp_led_NEW(prog_distorted, leds);
  }
  load_leds_from_fx();
}

void distort_logarithmic() {
  for (uint8_t i = 0; i < NATIVE_RESOLUTION; i++) {
    float prog = i / float(NATIVE_RESOLUTION - 1);
    float prog_distorted = sqrt(prog);
    leds_fx[i] = lerp_led_NEW(prog_distorted, leds);
  }
  load_leds_from_fx();
}

void increase_saturation(uint8_t amount) {
  for (uint8_t i = 0; i < NATIVE_RESOLUTION; i++) {
    CHSV hsv = rgb2hsv_approximate(leds[i]);
    hsv.s = qadd8(hsv.s, amount);
    leds[i] = hsv;
  }
}

void fade_top_half(bool shifted = false) {
  int16_t shift = 0;
  if (shifted == true) {
    shift -= (NATIVE_RESOLUTION >> 1); // Use half resolution
  }
  for (uint8_t i = 0; i < (NATIVE_RESOLUTION >> 1); i++) {
    float fade = i / float(NATIVE_RESOLUTION >> 1);

    leds[(NATIVE_RESOLUTION - 1 - i) + shift].r *= fade;
    leds[(NATIVE_RESOLUTION - 1 - i) + shift].g *= fade;
    leds[(NATIVE_RESOLUTION - 1 - i) + shift].b *= fade;
  }
}
*/

float apply_contrast_float(float value, float intensity) {
  float mid_point = 0.5;
  float factor = (intensity * 2.0) + 1.0;

  float contrasted_value = (value - mid_point) * factor + mid_point;
  contrasted_value = constrain(contrasted_value, 0.0, 1.0);

  return contrasted_value;
}

SQ15x16 apply_contrast_fixed(SQ15x16 value, SQ15x16 intensity) {
  SQ15x16 mid_point = 0.5;
  SQ15x16 factor = (intensity * 2.0) + 1.0;

  SQ15x16 contrasted_value = (value - mid_point) * factor + mid_point;
  if (contrasted_value > SQ15x16(1.0)) {
    contrasted_value = 1.0;
  } else if (contrasted_value < SQ15x16(0.0)) {
    contrasted_value = 0.0;
  }

  return contrasted_value;
}

#include <stdint.h>

uint8_t apply_contrast(uint8_t value, uint8_t intensity) {
  uint16_t mid_point = NATIVE_RESOLUTION;
  uint16_t factor = (uint16_t)intensity + 1;

  int16_t contrasted_value = (int16_t)value - mid_point;
  contrasted_value = (contrasted_value * factor) + (mid_point << 8);
  contrasted_value >>= 8;

  if (contrasted_value < 0) {
    contrasted_value = 0;
  } else if (contrasted_value > 255) {
    contrasted_value = 255;
  }

  return (uint8_t)contrasted_value;
}

/*
void force_incandescent_output() {
  for (uint8_t i = 0; i < NATIVE_RESOLUTION; i++) {
    uint8_t max_val = 0;
    if (leds[i].r > max_val) { max_val = leds[i].r; }
    if (leds[i].g > max_val) { max_val = leds[i].g; }
    if (leds[i].b > max_val) { max_val = leds[i].b; }

    leds[i] = CRGB(
      uint16_t(incandescent_lookup.r * max_val) >> 8,
      uint16_t(incandescent_lookup.g * max_val) >> 8,
      uint16_t(incandescent_lookup.b * max_val) >> 8);
  }
}
*/

void render_bulb_cover() {
  SQ15x16 cover[4] = { 0.25, 1.00, 0.25, 0.00 };

  for (uint8_t i = 0; i < NATIVE_RESOLUTION; i++) {
    CRGB16 covered_color = {
      leds_16[i].r * cover[i % 4],
      leds_16[i].g * cover[i % 4],
      leds_16[i].b * cover[i % 4],
    };

    SQ15x16 bulb_opacity = CONFIG.BULB_OPACITY;
    SQ15x16 bulb_opacity_inv = 1.0 - bulb_opacity;

    leds_16[i].r = leds_16[i].r * bulb_opacity_inv + covered_color.r * bulb_opacity;
    leds_16[i].g = leds_16[i].g * bulb_opacity_inv + covered_color.g * bulb_opacity;
    leds_16[i].b = leds_16[i].b * bulb_opacity_inv + covered_color.b * bulb_opacity;
  }
}

CRGB force_saturation(CRGB input, uint8_t saturation) {
  CHSV out_col_hsv = rgb2hsv_approximate(input);
  CHSV out_col_hsv_sat = out_col_hsv;
  out_col_hsv_sat.setHSV(out_col_hsv.h, saturation, out_col_hsv.v);

  return CRGB(out_col_hsv_sat);
}

CRGB force_hue(CRGB input, uint8_t hue) {
  CHSV out_col_hsv = rgb2hsv_approximate(input);
  CHSV out_col_hsv_hue = out_col_hsv;
  out_col_hsv_hue.setHSV(hue, out_col_hsv.s, out_col_hsv.v);

  return CRGB(out_col_hsv_hue);
}

void blend_buffers(CRGB16* output_array, CRGB16* input_a, CRGB16* input_b, uint8_t blend_mode, SQ15x16 mix) {
  if (blend_mode == BLEND_MIX) {
    for (uint8_t i = 0; i < NATIVE_RESOLUTION; i++) {
      output_array[i].r = input_a[i].r * (1.0 - mix) + input_b[i].r * (mix);
      output_array[i].g = input_a[i].g * (1.0 - mix) + input_b[i].g * (mix);
      output_array[i].b = input_a[i].b * (1.0 - mix) + input_b[i].b * (mix);
    }
  } else if (blend_mode == BLEND_ADD) {
    for (uint8_t i = 0; i < NATIVE_RESOLUTION; i++) {
      output_array[i].r = input_a[i].r + (input_b[i].r * mix);
      output_array[i].g = input_a[i].g + (input_b[i].g * mix);
      output_array[i].b = input_a[i].b + (input_b[i].b * mix);
    }
  } else if (blend_mode == BLEND_MULTIPLY) {
    for (uint8_t i = 0; i < NATIVE_RESOLUTION; i++) {
      output_array[i].r = input_a[i].r * input_b[i].r;
      output_array[i].g = input_a[i].g * input_b[i].g;
      output_array[i].b = input_a[i].b * input_b[i].b;
    }
  }
}

void apply_prism_effect(float iterations, SQ15x16 opacity) {
  // Handle the whole number part of iterations
  uint8_t whole_iterations = (uint8_t)iterations;
  
  // Store original values that we need to preserve
  SQ15x16 original_hue_position = hue_position;
  
  // Apply full iterations
  for (uint8_t i = 0; i < whole_iterations; i++) {
    memcpy(leds_16_fx, leds_16, sizeof(CRGB16) * NATIVE_RESOLUTION);

    scale_image_to_half(leds_16_fx);
    shift_leds_up(leds_16_fx, (NATIVE_RESOLUTION >> 1));
    mirror_image_downwards(leds_16_fx);
    
    // Apply color shift to this prism iteration
    // Each successive prism gets a slight hue shift
    float hue_shift = (i * 0.05); // 5% hue shift per prism
    
    // Apply the hue shift to the prism
    for (uint8_t j = 0; j < NATIVE_RESOLUTION; j++) {
      // Only shift colors if there's actual color data
      if (leds_16_fx[j].r > 0 || leds_16_fx[j].g > 0 || leds_16_fx[j].b > 0) {
        leds_16_fx[j] = adjust_hue_and_saturation(
          leds_16_fx[j], 
          fmod_fixed(hue_position + hue_shift, 1.0), 
          CONFIG.SATURATION
        );
      }
    }

    // memcpy(leds_16_fx_2, leds_16, sizeof(CRGB16) * NATIVE_RESOLUTION); // No longer needed
    blend_buffers(leds_16, leds_16, leds_16_fx, BLEND_ADD, opacity); // Blend original (leds_16) with processed (leds_16_fx)
  }
  
  // Handle the fractional part if any
  float fractional_part = iterations - whole_iterations;
  if (fractional_part > 0.01) { // Only process if the fractional part is significant
    memcpy(leds_16_fx, leds_16, sizeof(CRGB16) * NATIVE_RESOLUTION);

    scale_image_to_half(leds_16_fx);
    shift_leds_up(leds_16_fx, (NATIVE_RESOLUTION >> 1));
    mirror_image_downwards(leds_16_fx);
    
    // Apply color shift to the fractional prism as well
    float hue_shift = (whole_iterations * 0.05);
    
    // Apply the hue shift to the prism
    for (uint8_t j = 0; j < NATIVE_RESOLUTION; j++) {
      // Only shift colors if there's actual color data
      if (leds_16_fx[j].r > 0 || leds_16_fx[j].g > 0 || leds_16_fx[j].b > 0) {
        leds_16_fx[j] = adjust_hue_and_saturation(
          leds_16_fx[j], 
          fmod_fixed(hue_position + hue_shift, 1.0), 
          CONFIG.SATURATION
        );
      }
    }

    // memcpy(leds_16_fx_2, leds_16, sizeof(CRGB16) * NATIVE_RESOLUTION); // No longer needed
    // Apply the effect with reduced opacity based on the fractional part
    blend_buffers(leds_16, leds_16, leds_16_fx, BLEND_ADD, opacity * fractional_part); // Blend original (leds_16) with processed (leds_16_fx)
  }
  
  // Restore the original values to prevent side effects
  hue_position = original_hue_position;
}

void clear_leds() {
  memset(leds_16, 0, sizeof(CRGB16) * NATIVE_RESOLUTION);
}

void process_color_shift() {
  if (PALETTE_MODE_ENABLED) {
    // In palette mode we want the gradient to stay fixed; skip auto hue shifting
    return;
  }
  if (CONFIG.AUTO_COLOR_SHIFT == true) {
    float direction = (CONFIG.MOOD - 0.5) * 2.0;
    direction *= fabs(direction);
    hue_position += (direction * 0.00015);
  }

  int16_t rounded_index = spectral_history_index - 1;
  while (rounded_index < 0) {
    rounded_index += SPECTRAL_HISTORY_LENGTH;
  }

  SQ15x16 novelty_now = novelty_curve[rounded_index];
  
  static uint32_t last_debug_print = 0;
  extern bool use_ansi_colors; // Defined in main.cpp
  if (color_shift_debug_logging_enabled && !use_ansi_colors && millis() - last_debug_print > 2000) {
    // Only print if not using the new debug display
    USBSerial.printf("COLOR_SHIFT - novelty:%.3f - hue_pos:%.3f - speed:%.3f\n", 
                     float(novelty_now), float(hue_position), float(hue_shift_speed));
    last_debug_print = millis();
  }

  // EXACT REFERENCE IMPLEMENTATION - DO NOT MODIFY
  // Remove bottom 10%, stretch values to still occupy full 0.0-1.0 range
  novelty_now -= SQ15x16(0.10);
  if (novelty_now < 0.0) {
    novelty_now = 0.0;
  }
  novelty_now *= SQ15x16(1.111111);  // ---- 1.0 / (1.0 - 0.10)

  // Adjust novelty scaling - cube was making values too small
  novelty_now = novelty_now * novelty_now;  // Square instead of cube for better sensitivity
  
  // Lower the cap to allow more color movement
  if (novelty_now > 0.02) {
    novelty_now = 0.02;
  }

  // Lower threshold for triggering color shift
  if (novelty_now > hue_shift_speed * 0.5) {  // More sensitive trigger
    hue_shift_speed = novelty_now * SQ15x16(0.75);
  } else {
    hue_shift_speed *= SQ15x16(0.99);
  }
  
  // Ensure minimum speed to prevent getting stuck at red
  if (hue_shift_speed < 0.0001) {
    hue_shift_speed = 0.0001;
  }

  // Add and wrap
  hue_position += (hue_shift_speed * hue_push_direction);
  while (hue_position < 0.0) {
    hue_position += 1.0;
  }
  while (hue_position >= 1.0) {
    hue_position -= 1.0;
  }

  if (fabs_fixed(hue_position - hue_destination) <= 0.01) {
    hue_push_direction *= -1.0;
    hue_shifting_mix_target *= -1.0;
    hue_destination = random_float();
    //printf("###################################### NEW DEST: %f\n", hue_destination);
  }

  SQ15x16 hue_shifting_mix_distance = fabs_fixed(hue_shifting_mix - hue_shifting_mix_target);
  if (hue_shifting_mix < hue_shifting_mix_target) {
    hue_shifting_mix += hue_shifting_mix_distance * 0.01;
  } else if (hue_shifting_mix > hue_shifting_mix_target) {
    hue_shifting_mix -= hue_shifting_mix_distance * 0.01;
  }

  /*
    if(chance(0.2) == true){ //0.2% of loops
        hue_push_direction *= -1.0;
        printf("###################################### SWITCH DIR: %f\n", hue_push_direction);
    }
    */
}

void make_smooth_chromagram() {
  memset(chromagram_smooth, 0, sizeof(SQ15x16) * 12);

  for (uint8_t i = 0; i < CONFIG.CHROMAGRAM_RANGE; i++) {
    SQ15x16 note_magnitude = spectrogram_smooth[i];

    if (note_magnitude > 1.0) {
      note_magnitude = 1.0;
    } else if (note_magnitude < 0.0) {
      note_magnitude = 0.0;
    }

    uint8_t chroma_bin = i % 12;
    chromagram_smooth[chroma_bin] += note_magnitude / SQ15x16(CONFIG.CHROMAGRAM_RANGE / 12.0);
  }

  static SQ15x16 max_peak = 0.001;

  max_peak *= 0.999;
  if (max_peak < 0.01) {
    max_peak = 0.01;
  }

  for (uint16_t i = 0; i < 12; i++) {
    if (chromagram_smooth[i] > max_peak) {
      SQ15x16 distance = chromagram_smooth[i] - max_peak;
      max_peak += distance *= SQ15x16(0.05);
    }
  }

  SQ15x16 multiplier = 1.0 / max_peak;

  for (uint8_t i = 0; i < 12; i++) {
    chromagram_smooth[i] *= multiplier;
  }
}


void draw_sprite(CRGB16 dest[], CRGB16 sprite[], uint32_t dest_length, uint32_t sprite_length, float position, SQ15x16 alpha) {
  int32_t position_whole = position;  // Downcast to integer accuracy
  float position_fract = position - position_whole;
  SQ15x16 mix_right = position_fract;
  SQ15x16 mix_left = 1.0 - mix_right;

  for (uint16_t i = 0; i < sprite_length; i++) {
    int32_t pos_left = i + position_whole;
    int32_t pos_right = i + position_whole + 1;

    bool skip_left = false;
    bool skip_right = false;

    if (pos_left < 0) {
      pos_left = 0;
      skip_left = true;
    }
    if (pos_left > dest_length - 1) {
      pos_left = dest_length - 1;
      skip_left = true;
    }

    if (pos_right < 0) {
      pos_right = 0;
      skip_right = true;
    }
    if (pos_right > dest_length - 1) {
      pos_right = dest_length - 1;
      skip_right = true;
    }

    if (skip_left == false) {
      dest[pos_left].r += sprite[i].r * mix_left * alpha;
      dest[pos_left].g += sprite[i].g * mix_left * alpha;
      dest[pos_left].b += sprite[i].b * mix_left * alpha;
    }

    if (skip_right == false) {
      dest[pos_right].r += sprite[i].r * mix_right * alpha;
      dest[pos_right].g += sprite[i].g * mix_right * alpha;
      dest[pos_right].b += sprite[i].b * mix_right * alpha;
    }
  }
}

CRGB16 force_saturation_16(CRGB16 rgb, SQ15x16 saturation) {
  // Convert RGB to HSV
  SQ15x16 max_val = fmax_fixed(rgb.r, fmax_fixed(rgb.g, rgb.b));
  SQ15x16 min_val = fmax_fixed(rgb.r, fmax_fixed(rgb.g, rgb.b));
  SQ15x16 delta = max_val - min_val;

  SQ15x16 hue, saturation_value, value;
  value = max_val;

  if (delta == 0) {
    // The color is achromatic (gray)
    hue = 0;
    saturation_value = 0;
  } else {
    // Calculate hue and saturation
    if (max_val == rgb.r) {
      hue = (rgb.g - rgb.b) / delta;
    } else if (max_val == rgb.g) {
      hue = 2 + (rgb.b - rgb.r) / delta;
    } else {
      hue = 4 + (rgb.r - rgb.g) / delta;
    }

    hue *= 60;
    if (hue < 0) {
      hue += 360;
    }

    saturation_value = delta / max_val;
  }

  // Set the saturation to the input value
  saturation_value = saturation;

  // Convert back to RGB
  SQ15x16 c = saturation_value * value;
  SQ15x16 x = c * (1 - fabs_fixed(fmod_fixed(hue / 60, 2) - 1));
  SQ15x16 m = value - c;

  CRGB16 modified_rgb;
  if (hue >= 0 && hue < 60) {
    modified_rgb.r = c;
    modified_rgb.g = x;
    modified_rgb.b = 0;
  } else if (hue >= 60 && hue < 120) {
    modified_rgb.r = x;
    modified_rgb.g = c;
    modified_rgb.b = 0;
  } else if (hue >= 120 && hue < 180) {
    modified_rgb.r = 0;
    modified_rgb.g = c;
    modified_rgb.b = x;
  } else if (hue >= 180 && hue < 240) {
    modified_rgb.r = 0;
    modified_rgb.g = x;
    modified_rgb.b = c;
  } else if (hue >= 240 && hue < 300) {
    modified_rgb.r = x;
    modified_rgb.g = 0;
    modified_rgb.b = c;
  } else {
    modified_rgb.r = c;
    modified_rgb.g = 0;
    modified_rgb.b = x;
  }

  modified_rgb.r += m;
  modified_rgb.g += m;
  modified_rgb.b += m;

  return modified_rgb;
}

CRGB16 adjust_hue_and_saturation(CRGB16 color, SQ15x16 hue, SQ15x16 saturation) {
  // Store the RGB values
  SQ15x16 r = color.r, g = color.g, b = color.b;

  // Calculate maximum and minimum values of r, g, b
  SQ15x16 max_val = fmax_fixed(r, fmax_fixed(g, b));

  // Calculate the value of the HSV color
  SQ15x16 v = max_val;

  // Use the input saturation
  SQ15x16 s = saturation;

  // Prepare to convert HSV back to RGB
  SQ15x16 c = v * s;  // chroma
  SQ15x16 h_prime = fmod_fixed(hue * SQ15x16(6.0), SQ15x16(6.0));
  SQ15x16 x = c * (SQ15x16(1.0) - fabs_fixed(fmod_fixed(h_prime, SQ15x16(2.0)) - SQ15x16(1.0)));

  // Recalculate r, g, b based on the new hue and saturation
  if (h_prime >= 0 && h_prime < 1) {
    r = c;
    g = x;
    b = 0;
  } else if (h_prime >= 1 && h_prime < 2) {
    r = x;
    g = c;
    b = 0;
  } else if (h_prime >= 2 && h_prime < 3) {
    r = 0;
    g = c;
    b = x;
  } else if (h_prime >= 3 && h_prime < 4) {
    r = 0;
    g = x;
    b = c;
  } else if (h_prime >= 4 && h_prime < 5) {
    r = x;
    g = 0;
    b = c;
  } else if (h_prime >= 5 && h_prime < 6) {
    r = c;
    g = 0;
    b = x;
  }

  // Add the calculated difference to get the final RGB values
  SQ15x16 m = v - c;
  r += m;
  g += m;
  b += m;

  // Clamp the values between 0.0 and 1.0 to account for rounding errors
  r = fmax_fixed(SQ15x16(0.0), fmin_fixed(SQ15x16(1.0), r));
  g = fmax_fixed(SQ15x16(0.0), fmin_fixed(SQ15x16(1.0), g));
  b = fmax_fixed(SQ15x16(0.0), fmin_fixed(SQ15x16(1.0), b));

  // Return the resulting color
  CRGB16 result = { r, g, b };
  return result;
}

void init_secondary_leds() {
  leds_scaled_secondary = new CRGB16[SECONDARY_LED_COUNT];
  leds_out_secondary = new CRGB[SECONDARY_LED_COUNT];

  // Use constants for FastLED template arguments
  FastLED.addLeds<WS2812B, SECONDARY_LED_DATA_PIN, GRB>(leds_out_secondary, SECONDARY_LED_COUNT);
  
  for (uint16_t x = 0; x < SECONDARY_LED_COUNT; x++) {
    leds_out_secondary[x] = CRGB(0, 0, 0);
  }
  
  USBSerial.print("INIT_SECONDARY_LEDS: ");
  USBSerial.println(SB_PASS);
}

void scale_to_secondary_strip() {
  if (SECONDARY_LED_COUNT == NATIVE_RESOLUTION) {
    memcpy(leds_scaled_secondary, leds_16_secondary, sizeof(CRGB16) * NATIVE_RESOLUTION);
  } else {
    for (SQ15x16 i = 0; i < SECONDARY_LED_COUNT; i++) {
      SQ15x16 prog = i / SQ15x16(SECONDARY_LED_COUNT);
      leds_scaled_secondary[i.getInteger()] = lerp_led_16(prog * SQ15x16(NATIVE_RESOLUTION), leds_16_secondary);
    }
  }
}

void apply_brightness_secondary() {
  // Apply the same silence scaling used for the primary LEDs
  float bright_val = SECONDARY_PHOTONS * SECONDARY_PHOTONS * silent_scale;
  
  if (debug_mode && (millis() % 5000 == 0)) {
    USBSerial.print("DEBUG: Secondary brightness = ");
    USBSerial.print(SECONDARY_PHOTONS);
    USBSerial.print("² × silent_scale(");
    USBSerial.print(silent_scale);
    USBSerial.print(") = ");
    USBSerial.println(bright_val);
  }
  
  for (uint16_t i = 0; i < SECONDARY_LED_COUNT; i++) {
    leds_scaled_secondary[i].r *= bright_val;
    leds_scaled_secondary[i].g *= bright_val;
    leds_scaled_secondary[i].b *= bright_val;
  }
}

void show_secondary_leds() {
  scale_to_secondary_strip();
  apply_brightness_secondary();
  // Quantization needs to happen *after* filtering if filter uses scaled values
  // quantize_color_secondary(CONFIG.TEMPORAL_DITHERING); // Moved down

  // Check SECONDARY specific incandescent settings
  if (SECONDARY_INCANDESCENT_FILTER > 0.0) { 
    SQ15x16 filter_strength = SECONDARY_INCANDESCENT_FILTER; // Use fixed-point
    for (uint16_t i = 0; i < SECONDARY_LED_COUNT; i++) {
      // Direct fixed-point to uint8_t conversion (assuming 0.0-1.0 maps to 0-255)
      // Note: Clamping might be needed depending on exact fixed-point behavior
      uint8_t r_raw = (leds_scaled_secondary[i].r * 255).getInteger(); 
      uint8_t g_raw = (leds_scaled_secondary[i].g * 255).getInteger();
      uint8_t b_raw = (leds_scaled_secondary[i].b * 255).getInteger();

      // Apply incandescent color correction using integer/fixed-point math
      // Ensure filter_strength doesn't exceed 1.0 before calculations
      if (filter_strength > SQ15x16(1.0)) filter_strength = SQ15x16(1.0);
      
      uint16_t blue_reduction_fp = uint16_t(b_raw) * uint16_t((filter_strength * 255).getInteger());
      uint8_t blue_reduction = blue_reduction_fp >> 8; // Divide by 256

      // Scale green reduction based on blue reduction using integer math
      uint16_t green_reduction_fp = uint16_t(g_raw) * uint16_t(blue_reduction) * uint16_t((filter_strength * 255).getInteger());
      // Need to divide by 255*256 - approximate by dividing by 2^16 (>> 16)
      uint8_t green_reduction = (green_reduction_fp >> 16);

      leds_out_secondary[i].r = r_raw; // Red remains unchanged in this simple filter
      leds_out_secondary[i].g = (g_raw > green_reduction) ? g_raw - green_reduction : 0;
      leds_out_secondary[i].b = (b_raw > blue_reduction) ? b_raw - blue_reduction : 0;
    }
  } else {
    // If filter is off, just quantize directly
    quantize_color_secondary(CONFIG.TEMPORAL_DITHERING);
  }
  
  // If filter was applied, quantize *after* filtering (using the calculated leds_out_secondary)
  // This assumes quantize_color_secondary can work directly on leds_out_secondary if filter applied
  // OR that the filtered values should be quantized. If quantization should happen first,
  // the logic needs adjustment. Let's assume quantization happens last based on original structure.
  if (SECONDARY_INCANDESCENT_FILTER <= 0.0) { 
      // Quantization was already called if filter is off.
  } else {
      // We need to manually copy the filtered uint8_t values from leds_out_secondary 
      // back to leds_scaled_secondary if quantize_color_secondary *requires* scaled input,
      // OR modify quantize_color_secondary to work on leds_out_secondary.
      // Simpler for now: assume filter output IS the final uint8_t value before base coat/reverse.
      // So, skip re-quantizing if filter was applied.
  }
  
  if (SECONDARY_BASE_COAT) {
    // Add base coat to secondary LEDs similar to primary
    for (uint16_t i = 0; i < SECONDARY_LED_COUNT; i++) {
      leds_out_secondary[i].r = min(255, leds_out_secondary[i].r + 2);
      leds_out_secondary[i].g = min(255, leds_out_secondary[i].g + 2);
      leds_out_secondary[i].b = min(255, leds_out_secondary[i].b + 2);
    }
  }
  
  if (SECONDARY_REVERSE_ORDER) {
    reverse_leds(leds_out_secondary, SECONDARY_LED_COUNT);
  }
}

// Function to apply visual enhancements that make the display "POP" more
void apply_enhanced_visuals() {
  // Only apply if there's actual visual data
  bool has_content = false;
  for (uint16_t i = 0; i < NATIVE_RESOLUTION; i++) {
    if (leds_16[i].r > 0.01 || leds_16[i].g > 0.01 || leds_16[i].b > 0.01) {
      has_content = true;
      break;
    }
  }
  
  if (!has_content) return;
  
  // Store original content
  memcpy(leds_16_fx, leds_16, sizeof(CRGB16) * NATIVE_RESOLUTION);
  
  // 1. Add subtle bloom/glow effect based on audio_vu_level
  float bloom_intensity = 0.15 + float(audio_vu_level) * 0.2;
  
  // Create a blurred version in leds_16_temp
  for (uint16_t i = 1; i < NATIVE_RESOLUTION-1; i++) {
    // Simple 3-pixel box blur
    leds_16_temp[i].r = (leds_16_fx[i-1].r + leds_16_fx[i].r + leds_16_fx[i+1].r) / 3.0;
    leds_16_temp[i].g = (leds_16_fx[i-1].g + leds_16_fx[i].g + leds_16_fx[i+1].g) / 3.0;
    leds_16_temp[i].b = (leds_16_fx[i-1].b + leds_16_fx[i].b + leds_16_fx[i+1].b) / 3.0;
  }
  
  // Edge pixels
  leds_16_temp[0] = leds_16_temp[1];
  leds_16_temp[NATIVE_RESOLUTION-1] = leds_16_temp[NATIVE_RESOLUTION-2];
  
  // Mix original and bloom
  for (uint16_t i = 0; i < NATIVE_RESOLUTION; i++) {
    leds_16[i].r = leds_16_fx[i].r + leds_16_temp[i].r * bloom_intensity;
    leds_16[i].g = leds_16_fx[i].g + leds_16_temp[i].g * bloom_intensity;
    leds_16[i].b = leds_16_fx[i].b + leds_16_temp[i].b * bloom_intensity;
  }
  
  // 2. Add subtle wave-like modulation effect
  static float wave_position = 0.0;
  wave_position += 0.03; // Speed of wave
  
  for (uint16_t i = 0; i < NATIVE_RESOLUTION; i++) {
    float position = float(i) / NATIVE_RESOLUTION;
    float wave = sin(wave_position + position * 6.28) * 0.5 + 0.5;
    
    // Apply subtle wave effect only where there's actual color
    if (leds_16[i].r > 0.05 || leds_16[i].g > 0.05 || leds_16[i].b > 0.05) {
      // Increase brightness slightly with wave
      float boost = 1.0 + (wave * 0.15); 
      leds_16[i].r *= boost;
      leds_16[i].g *= boost;
      leds_16[i].b *= boost;
    }
  }
  
  // 3. Dynamic color enhancement - make colors more vibrant during beats
  if (audio_vu_level > audio_vu_level_average * 1.2) {
    float enhancement = (float(audio_vu_level) / float(audio_vu_level_average) - 1.0) * 0.4;
    if (enhancement > 0.25) enhancement = 0.25;
    
    for (uint16_t i = 0; i < NATIVE_RESOLUTION; i++) {
      if (leds_16[i].r > 0.05 || leds_16[i].g > 0.05 || leds_16[i].b > 0.05) {
        // Find dominant color and enhance it
        if (leds_16[i].r > leds_16[i].g && leds_16[i].r > leds_16[i].b) {
          leds_16[i].r *= (1.0 + enhancement);
        } else if (leds_16[i].g > leds_16[i].r && leds_16[i].g > leds_16[i].b) {
          leds_16[i].g *= (1.0 + enhancement);
        } else if (leds_16[i].b > leds_16[i].r && leds_16[i].b > leds_16[i].g) {
          leds_16[i].b *= (1.0 + enhancement);
        }
      }
    }
  }
  
  // Clip values to ensure they stay in valid range
  clip_led_values(leds_16);
}

// Add a quantization function for the secondary strip similar to primary
void quantize_color_secondary(bool temporal_dither) {
  if (temporal_dither) {
    static uint8_t noise_origin_r_s = 0, noise_origin_g_s = 0, noise_origin_b_s = 0;
    noise_origin_r_s++;
    noise_origin_g_s++;
    noise_origin_b_s++;
    for (uint16_t i = 0; i < SECONDARY_LED_COUNT; i++) {
      SQ15x16 decimal_r = leds_scaled_secondary[i].r * SQ15x16(254);
      SQ15x16 whole_r = decimal_r.getInteger();
      SQ15x16 fract_r = decimal_r - whole_r;
      if (fract_r >= dither_table[(noise_origin_r_s + i) % 8]) whole_r += SQ15x16(1);
      leds_out_secondary[i].r = whole_r.getInteger();

      SQ15x16 decimal_g = leds_scaled_secondary[i].g * SQ15x16(254);
      SQ15x16 whole_g = decimal_g.getInteger();
      SQ15x16 fract_g = decimal_g - whole_g;
      if (fract_g >= dither_table[(noise_origin_g_s + i) % 8]) whole_g += SQ15x16(1);
      leds_out_secondary[i].g = whole_g.getInteger();

      SQ15x16 decimal_b = leds_scaled_secondary[i].b * SQ15x16(254);
      SQ15x16 whole_b = decimal_b.getInteger();
      SQ15x16 fract_b = decimal_b - whole_b;
      if (fract_b >= dither_table[(noise_origin_b_s + i) % 8]) whole_b += SQ15x16(1);
      leds_out_secondary[i].b = whole_b.getInteger();
    }
  } else {
    for (uint16_t i = 0; i < SECONDARY_LED_COUNT; i++) {
      leds_out_secondary[i].r = uint8_t(leds_scaled_secondary[i].r * 255);
      leds_out_secondary[i].g = uint8_t(leds_scaled_secondary[i].g * 255);
      leds_out_secondary[i].b = uint8_t(leds_scaled_secondary[i].b * 255);
    }
  }
}

// Palette LUT cache (256 entries)
static CRGB16 palette_lut[256];
static uint8_t palette_lut_index_cached = 255; // invalid

static void update_palette_lut(uint8_t idx){
  palette_lut_index_cached = idx;
  CRGBPalette16 curPal( gGradientPalettes[idx] );
  for(uint16_t i=0;i<256;i++){
    CRGB col = ColorFromPalette(curPal, i, 255, LINEARBLEND);
    palette_lut[i] = { col.r / 255.0, col.g / 255.0, col.b / 255.0 };
  }
}

CRGB16 get_mode_color(SQ15x16 hue, SQ15x16 saturation, SQ15x16 value) {
  while (hue > SQ15x16(1.0)) hue -= SQ15x16(1.0);
  while (hue < SQ15x16(0.0)) hue += SQ15x16(1.0);

  if (!PALETTE_MODE_ENABLED) {
    return hsv(hue, saturation, value);
  }

  uint8_t palIdx = PALETTE_INDEX % gGradientPaletteCount;
  if(palIdx != palette_lut_index_cached){
    update_palette_lut(palIdx);
  }

  uint8_t colorIdx = uint8_t((float)hue * 255.0f); // 0-255
  CRGB16 result = palette_lut[colorIdx];

  // Apply brightness (value)
  result.r *= value;
  result.g *= value;
  result.b *= value;

  // Apply saturation
  if (saturation < SQ15x16(1.0)) {
    result = desaturate(result, SQ15x16(1.0) - saturation);
  }
  return result;
}

// Gamma correction LUT (gamma = 2.2 -----------------------------------------
static bool gamma_lut_initialized = false;
uint8_t gamma_lut[256];
void init_gamma_lut() {
  if (gamma_lut_initialized) return;
  for (int i = 0; i < 256; i++) {
    float v = i / 255.0f;
    float corrected = powf(v, 1.0f / 2.2f);
    gamma_lut[i] = uint8_t(corrected * 255.0f + 0.5f);
  }
  gamma_lut_initialized = true;
}
// ---------------------------------------------------------------------------

#endif // LED_UTILITIES_H
