// Test file to diagnose frequency detection issues
// This file prints debug information about the audio pipeline

#ifndef test_audio_diagnostics_h
#define test_audio_diagnostics_h

void diagnose_audio_pipeline() {
  static uint32_t last_debug_time = 0;
  uint32_t now = millis();
  
  // Run diagnostics every second
  if (now - last_debug_time < 1000) return;
  last_debug_time = now;
  
  USBSerial.println("\n==== AUDIO PIPELINE DIAGNOSTICS ====");
  
  // 1. Check sample rate and configuration
  USBSerial.print("Sample Rate: ");
  USBSerial.print(CONFIG.SAMPLE_RATE);
  USBSerial.println(" Hz");
  
  USBSerial.print("Samples Per Chunk: ");
  USBSerial.println(CONFIG.SAMPLES_PER_CHUNK);
  
  USBSerial.print("Sample History Length: ");
  USBSerial.println(SAMPLE_HISTORY_LENGTH);
  
  // 2. Check sample_window contents
  float max_sample = 0;
  float min_sample = 0;
  float avg_sample = 0;
  int zero_count = 0;
  
  for (int i = 0; i < SAMPLE_HISTORY_LENGTH; i++) {
    float sample = sample_window[i];
    if (sample > max_sample) max_sample = sample;
    if (sample < min_sample) min_sample = sample;
    avg_sample += sample;
    if (sample == 0) zero_count++;
  }
  avg_sample /= SAMPLE_HISTORY_LENGTH;
  
  USBSerial.print("\nSample Window Stats:");
  USBSerial.print("\n  Max: ");
  USBSerial.print(max_sample);
  USBSerial.print("\n  Min: ");
  USBSerial.print(min_sample);
  USBSerial.print("\n  Avg: ");
  USBSerial.print(avg_sample);
  USBSerial.print("\n  Zero samples: ");
  USBSerial.print(zero_count);
  USBSerial.print(" / ");
  USBSerial.println(SAMPLE_HISTORY_LENGTH);
  
  // 3. Check frequency configuration
  USBSerial.println("\nFrequency Configuration:");
  USBSerial.print("NUM_FREQS: ");
  USBSerial.println(NUM_FREQS);
  
  // Show first few and last few frequencies
  for (int i = 0; i < 5; i++) {
    USBSerial.print("  Freq[");
    USBSerial.print(i);
    USBSerial.print("]: ");
    USBSerial.print(frequencies[i].target_freq);
    USBSerial.print(" Hz, block_size: ");
    USBSerial.println(frequencies[i].block_size);
  }
  USBSerial.println("  ...");
  for (int i = NUM_FREQS - 5; i < NUM_FREQS; i++) {
    USBSerial.print("  Freq[");
    USBSerial.print(i);
    USBSerial.print("]: ");
    USBSerial.print(frequencies[i].target_freq);
    USBSerial.print(" Hz, block_size: ");
    USBSerial.println(frequencies[i].block_size);
  }
  
  // 4. Check waveform buffer
  max_sample = 0;
  zero_count = 0;
  for (int i = 0; i < CONFIG.SAMPLES_PER_CHUNK; i++) {
    if (abs(waveform[i]) > max_sample) max_sample = abs(waveform[i]);
    if (waveform[i] == 0) zero_count++;
  }
  
  USBSerial.print("\nWaveform Buffer Stats:");
  USBSerial.print("\n  Max amplitude: ");
  USBSerial.print(max_sample);
  USBSerial.print("\n  Zero samples: ");
  USBSerial.print(zero_count);
  USBSerial.print(" / ");
  USBSerial.println(CONFIG.SAMPLES_PER_CHUNK);
  
  // 5. Check if sliding window is working
  static short last_window_end = 0;
  bool window_changed = (sample_window[SAMPLE_HISTORY_LENGTH-1] != last_window_end);
  last_window_end = sample_window[SAMPLE_HISTORY_LENGTH-1];
  
  USBSerial.print("\nSliding window updating: ");
  USBSerial.println(window_changed ? "YES" : "NO");
  
  // 6. Show detected frequencies
  USBSerial.println("\nDetected Frequencies (magnitude > 0.5):");
  int detected_count = 0;
  for (int i = 0; i < NUM_FREQS; i++) {
    if (magnitudes_normalized[i] > 0.5) {
      USBSerial.print("  ");
      USBSerial.print(frequencies[i].target_freq);
      USBSerial.print(" Hz: ");
      USBSerial.println(magnitudes_normalized[i]);
      detected_count++;
      if (detected_count >= 10) {
        USBSerial.println("  ... (more detected)");
        break;
      }
    }
  }
  if (detected_count == 0) {
    USBSerial.println("  None detected");
  }
  
  USBSerial.println("==== END DIAGNOSTICS ====\n");
}

// Function to diagnose DC offset issues
void diagnose_dc_offset() {
  static uint32_t last_dc_debug = 0;
  uint32_t now = millis();
  
  // Run every 2 seconds
  if (now - last_dc_debug < 2000) return;
  last_dc_debug = now;
  
  USBSerial.println("\n==== DC OFFSET DIAGNOSTICS ====");
  
  // Show current DC offset setting
  USBSerial.print("CONFIG.DC_OFFSET: ");
  USBSerial.println(CONFIG.DC_OFFSET);
  
  // Calculate actual DC offset from raw samples
  int64_t raw_sum = 0;
  int32_t raw_min = INT32_MAX;
  int32_t raw_max = INT32_MIN;
  
  for (int i = 0; i < CONFIG.SAMPLES_PER_CHUNK; i++) {
    int32_t raw_sample = audio_raw_state.getRawSamples()[i];
    raw_sum += raw_sample;
    if (raw_sample < raw_min) raw_min = raw_sample;
    if (raw_sample > raw_max) raw_max = raw_sample;
  }
  
  int32_t raw_avg = raw_sum / CONFIG.SAMPLES_PER_CHUNK;
  
  USBSerial.println("\nRaw I2S samples (32-bit):");
  USBSerial.print("  Average: ");
  USBSerial.println(raw_avg);
  USBSerial.print("  Min: ");
  USBSerial.println(raw_min);
  USBSerial.print("  Max: ");
  USBSerial.println(raw_max);
  USBSerial.print("  Range: ");
  USBSerial.println(raw_max - raw_min);
  
  // Show what happens after bit shift
  USBSerial.println("\nAfter >>14 shift:");
  USBSerial.print("  Average: ");
  USBSerial.println(raw_avg >> 14);
  USBSerial.print("  Min: ");
  USBSerial.println(raw_min >> 14);
  USBSerial.print("  Max: ");
  USBSerial.println(raw_max >> 14);
  
  // Calculate DC offset from processed waveform
  int32_t waveform_sum = 0;
  int16_t waveform_min = INT16_MAX;
  int16_t waveform_max = INT16_MIN;
  
  for (int i = 0; i < CONFIG.SAMPLES_PER_CHUNK; i++) {
    waveform_sum += waveform[i];
    if (waveform[i] < waveform_min) waveform_min = waveform[i];
    if (waveform[i] > waveform_max) waveform_max = waveform[i];
  }
  
  int16_t waveform_avg = waveform_sum / CONFIG.SAMPLES_PER_CHUNK;
  
  USBSerial.println("\nProcessed waveform:");
  USBSerial.print("  Average: ");
  USBSerial.println(waveform_avg);
  USBSerial.print("  Min: ");
  USBSerial.println(waveform_min);
  USBSerial.print("  Max: ");
  USBSerial.println(waveform_max);
  USBSerial.print("  Range: ");
  USBSerial.println(waveform_max - waveform_min);
  
  // Suggest DC offset correction
  int32_t suggested_offset = raw_avg >> 14;
  USBSerial.print("\nSuggested DC_OFFSET: ");
  USBSerial.println(suggested_offset);
  
  USBSerial.println("==== END DC OFFSET DIAGNOSTICS ====\n");
}

// Function to generate a test tone in the sample window
void generate_test_tone(float frequency, float amplitude = 16000) {
  USBSerial.print("Generating test tone at ");
  USBSerial.print(frequency);
  USBSerial.println(" Hz");
  
  for (int i = 0; i < SAMPLE_HISTORY_LENGTH; i++) {
    float t = (float)i / CONFIG.SAMPLE_RATE;
    sample_window[i] = (short)(amplitude * sin(2.0 * PI * frequency * t));
  }
}

// Function to test specific frequencies
void test_frequency_detection() {
  static uint32_t last_test_time = 0;
  static int test_freq_index = 0;
  uint32_t now = millis();
  
  // Run test every 3 seconds
  if (now - last_test_time < 3000) return;
  last_test_time = now;
  
  // Test frequencies: 110Hz, 220Hz, 440Hz, 880Hz, 1760Hz, 3520Hz, 7040Hz
  float test_frequencies[] = {110.0, 220.0, 440.0, 880.0, 1760.0, 3520.0, 7040.0};
  int num_test_freqs = sizeof(test_frequencies) / sizeof(test_frequencies[0]);
  
  // Generate test tone
  float test_freq = test_frequencies[test_freq_index];
  generate_test_tone(test_freq);
  
  // Run GDFT
  process_GDFT();
  
  // Find which bin should detect this frequency
  int best_bin = -1;
  float min_diff = 999999;
  for (int i = 0; i < NUM_FREQS; i++) {
    float diff = fabs(frequencies[i].target_freq - test_freq);
    if (diff < min_diff) {
      min_diff = diff;
      best_bin = i;
    }
  }
  
  USBSerial.print("\nTest Tone Results for ");
  USBSerial.print(test_freq);
  USBSerial.println(" Hz:");
  USBSerial.print("  Expected bin: ");
  USBSerial.print(best_bin);
  USBSerial.print(" (");
  USBSerial.print(frequencies[best_bin].target_freq);
  USBSerial.println(" Hz)");
  USBSerial.print("  Detected magnitude: ");
  USBSerial.println(magnitudes_normalized[best_bin]);
  
  // Show neighboring bins
  USBSerial.println("  Neighboring bins:");
  for (int i = max(0, best_bin - 2); i <= min(NUM_FREQS - 1, best_bin + 2); i++) {
    USBSerial.print("    Bin ");
    USBSerial.print(i);
    USBSerial.print(" (");
    USBSerial.print(frequencies[i].target_freq);
    USBSerial.print(" Hz): ");
    USBSerial.println(magnitudes_normalized[i]);
  }
  
  // Move to next test frequency
  test_freq_index++;
  if (test_freq_index >= num_test_freqs) {
    test_freq_index = 0;
  }
}

#endif // test_audio_diagnostics_h