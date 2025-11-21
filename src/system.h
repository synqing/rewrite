uint32_t timing_start = 0;
extern void run_sweet_spot();
extern void show_leds();

void reboot() {
  lock_leds();
  USBSerial.println("--- ! REBOOTING to apply changes (You may need to restart the Serial Monitor)");
  USBSerial.flush();
  for(float i = 1.0; i >= 0.0; i-=0.05){
    MASTER_BRIGHTNESS = i;
    run_sweet_spot();
    show_leds();
    FastLED.delay(12); // Takes ~250ms total
  }
  FastLED.setBrightness(0);
  FastLED.show();
  ESP.restart();
}

void start_timing(const char* func_name) {
  USBSerial.print(func_name);
  USBSerial.print(": ");
  USBSerial.flush();
  timing_start = micros();
}

void end_timing() {
  uint32_t timing_end = micros();
  uint32_t t_delta = timing_end - timing_start;

  USBSerial.print("DONE IN ");
  USBSerial.print(t_delta / 1000.0, 3);
  USBSerial.println(" MS");
}

void check_current_function() {
  function_hits[function_id]++;
}

static void usb_event_callback(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  if (event_base == ARDUINO_USB_EVENTS) {
    //arduino_usb_event_data_t * data = (arduino_usb_event_data_t*)event_data;
    switch (event_id) {
      case ARDUINO_USB_STARTED_EVENT:
        //Serial0.println("USB PLUGGED");
        break;
      case ARDUINO_USB_STOPPED_EVENT:
        //Serial0.println("USB UNPLUGGED");
        break;
      case ARDUINO_USB_SUSPEND_EVENT:
        //Serial0.printf("USB SUSPENDED: remote_wakeup_en: %u\n", data->suspend.remote_wakeup_en);
        break;
      case ARDUINO_USB_RESUME_EVENT:
        //Serial0.println("USB RESUMED");
        break;

      default:
        break;
    }
  } else if (event_base == ARDUINO_FIRMWARE_MSC_EVENTS) {
    //arduino_firmware_msc_event_data_t * data = (arduino_firmware_msc_event_data_t*)event_data;
    switch (event_id) {
      case ARDUINO_FIRMWARE_MSC_START_EVENT:
        //Serial0.println("MSC Update Start");
        msc_update_started = true;
        break;
      case ARDUINO_FIRMWARE_MSC_WRITE_EVENT:
        //HWSerial.printf("MSC Update Write %u bytes at offset %u\n", data->write.size, data->write.offset);
        //Serial0.print(".");
        break;
      case ARDUINO_FIRMWARE_MSC_END_EVENT:
        //Serial0.printf("\nMSC Update End: %u bytes\n", data->end.size);
        break;
      case ARDUINO_FIRMWARE_MSC_ERROR_EVENT:
        //Serial0.printf("MSC Update ERROR! Progress: %u bytes\n", data->error.size);
        break;
      case ARDUINO_FIRMWARE_MSC_POWER_EVENT:
        //Serial0.printf("MSC Update Power: power: %u, start: %u, eject: %u", data->power.power_condition, data->power.start, data->power.load_eject);
        break;

      default:
        break;
    }
  }
}

void enable_usb_update_mode() {
  USB.onEvent(usb_event_callback);

  MSC_Update.onEvent(usb_event_callback);
  MSC_Update.begin();

  MASTER_BRIGHTNESS = 1.0;

  uint8_t led_index = 0;
  uint8_t sweet_index = 0;

  const uint8_t sweet_order[3][3] = {
    {1, 0, 0},
    {0, 1, 0},
    {0, 0, 1}
  };

  while (true) {
    for (uint8_t i = 0; i < NATIVE_RESOLUTION; i++) {
      leds_16[i] = {0, 0, 0};
    }

    if (msc_update_started == false) {
      leds_16[led_index] = {0, 0, 0.25};
      #ifndef ARDUINO_ESP32S3_DEV
      ledcWrite(SWEET_SPOT_LEFT_CHANNEL,   sweet_order[sweet_index][0] * 512);
      ledcWrite(SWEET_SPOT_CENTER_CHANNEL, sweet_order[sweet_index][1] * 512);
      ledcWrite(SWEET_SPOT_RIGHT_CHANNEL,  sweet_order[sweet_index][2] * 512);
      #endif
    }
    else {
      leds_16[NATIVE_RESOLUTION-1-led_index] = {0, 0.25, 0};
      #ifndef ARDUINO_ESP32S3_DEV
      ledcWrite(SWEET_SPOT_LEFT_CHANNEL,   sweet_order[sweet_index][2] * 4095);
      ledcWrite(SWEET_SPOT_CENTER_CHANNEL, sweet_order[sweet_index][1] * 4095);
      ledcWrite(SWEET_SPOT_RIGHT_CHANNEL,  sweet_order[sweet_index][0] * 4095);
      #endif
    }


    show_leds();

    if(led_index == 0 || led_index == NATIVE_RESOLUTION/2){
      sweet_index++;
      if (sweet_index >= 3) {
        sweet_index = 0;
      }
    }

    led_index++;
    if (led_index >= NATIVE_RESOLUTION) {
      led_index = 0;      
    }
    yield();
  }
}

void init_usb() {
  #ifdef ARDUINO_ESP32S3_DEV
    // S3 with CDC on boot doesn't need USB.begin()
    delay(100);
  #else
    USB.begin();
    delay(500);
  #endif
}

void init_sweet_spot() {
  #ifndef ARDUINO_ESP32S3_DEV
  // Only initialize sweet spot LEDs on S2 (S3 has no sweet spot LEDs)
  ledcSetup(SWEET_SPOT_LEFT_CHANNEL, 500, 12);
  ledcAttachPin(SWEET_SPOT_LEFT_PIN, SWEET_SPOT_LEFT_CHANNEL);

  ledcSetup(SWEET_SPOT_CENTER_CHANNEL, 500, 12);
  ledcAttachPin(SWEET_SPOT_CENTER_PIN, SWEET_SPOT_CENTER_CHANNEL);

  ledcSetup(SWEET_SPOT_RIGHT_CHANNEL, 500, 12);
  ledcAttachPin(SWEET_SPOT_RIGHT_PIN, SWEET_SPOT_RIGHT_CHANNEL);
  #endif
}

void generate_a_weights() {
  start_timing("GENERATING A-WEIGHTS");
  for (uint8_t i = 0; i < 13; i++) {
    float decibels = a_weight_table[i][1];
    float bels = decibels / 10.0;
    float ratio = pow(10, bels);
    a_weight_table[i][1] = ratio;
  }

  for (uint8_t i = 0; i < NUM_FREQS; i++) {
    float frequency = notes[i];
    uint8_t low_index = 0;
    uint8_t high_index = 0;
    for (uint8_t x = 0; x < 13; x++) {
      float table_freq = a_weight_table[x][0];
      if (frequency >= table_freq) {
        low_index = x;
        high_index = x + 1;
      }
    }

    float low_freq = a_weight_table[low_index][0];
    float high_freq = a_weight_table[high_index][0];

    float freq_position = (frequency - low_freq) / (high_freq - low_freq);

    float interpolated_weight = (a_weight_table[low_index][1] * (1.0 - freq_position)) + (a_weight_table[high_index][1] * (freq_position));

    frequencies[i].a_weighting_ratio = interpolated_weight;
    if (frequencies[i].a_weighting_ratio > 1.0) {
      frequencies[i].a_weighting_ratio = 1.0;
    }
  }
  end_timing();
}

void generate_window_lookup() {
  start_timing("GENERATING HANN WINDOW LOOKUP TABLE");
  for (uint16_t i = 0; i < 2048; i++) {
    float ratio = i / 4095.0;
    float weighing_factor = 0.54 * (1.0 - cos(TWOPI * ratio));

    window_lookup[i]        = 32767 * weighing_factor;
    window_lookup[4095 - i] = 32767 * weighing_factor;
  }
  end_timing();
}

void precompute_goertzel_constants() {
  for (uint16_t i = 0; i < NUM_FREQS; i++) {
    int16_t n = i;
    frequencies[i].target_freq = notes[n + CONFIG.NOTE_OFFSET];

    float neighbor_left;
    float neighbor_right;

    if (i == 0) {
      neighbor_left = notes[n + CONFIG.NOTE_OFFSET];
      neighbor_right = notes[n + CONFIG.NOTE_OFFSET + 1];
    } else if (i == NUM_FREQS - 1) {
      neighbor_left = notes[n + CONFIG.NOTE_OFFSET - 1];
      neighbor_right = notes[n + CONFIG.NOTE_OFFSET];
    } else {
      neighbor_left = notes[n + CONFIG.NOTE_OFFSET - 1];
      neighbor_right = notes[n + CONFIG.NOTE_OFFSET + 1];
    }

    float neighbor_left_distance_hz = fabs(neighbor_left - frequencies[i].target_freq);
    float neighbor_right_distance_hz = fabs(neighbor_right - frequencies[i].target_freq);
    float max_distance_hz = 0;
    if (neighbor_left_distance_hz > max_distance_hz) {
      max_distance_hz = neighbor_left_distance_hz;
    }
    if (neighbor_right_distance_hz > max_distance_hz) {
      max_distance_hz = neighbor_right_distance_hz;
    }

    frequencies[i].block_size = CONFIG.SAMPLE_RATE / (max_distance_hz * 2.0);

    // CRITICAL FIX: Remove artificial block size cap that was limiting frequency range
    // Bass frequencies NEED larger analysis windows for accuracy
    // With 32kHz sample rate and 4096 sample history, we can handle much larger blocks
    // The previous 2000 sample cap was breaking the frequency allocation algorithm
    if(frequencies[i].block_size > SAMPLE_HISTORY_LENGTH){
        frequencies[i].block_size = SAMPLE_HISTORY_LENGTH;  // Use full available history
    }

    // Calculate the inverse for optimization
    if (frequencies[i].block_size > 0) {
        frequencies[i].inv_block_size_half = 2.0 / frequencies[i].block_size; // Equivalent to 1.0 / (block_size / 2.0)
    } else {
        frequencies[i].inv_block_size_half = 0.0; // Avoid division by zero
    }

    frequencies[i].block_size_recip = 1.0 / float(frequencies[i].block_size);

    // Calculate Q15 coefficient for high-performance integer math
    float omega = 2.0f * PI * frequencies[i].target_freq / (float)CONFIG.SAMPLE_RATE;
    frequencies[i].coeff_q15 = (int32_t)(32768.0f * 2.0f * cos(omega));
    
    // PERFORMANCE OPTIMIZATION: Limit block sizes for high frequencies
    // High frequencies don't need huge windows for accurate detection
    if (i > 48) {  // Upper half of spectrum
      if (frequencies[i].block_size > 256) {
        frequencies[i].block_size_optimized = 256;
      } else {
        frequencies[i].block_size_optimized = frequencies[i].block_size;
      }
    } else {
      frequencies[i].block_size_optimized = frequencies[i].block_size;
    }

    frequencies[i].window_mult = 4096.0 / frequencies[i].block_size;
    frequencies[i].zone = (i / float(NUM_FREQS)) * NUM_ZONES;
  }
}

void debug_function_timing(uint32_t t_now) {
  static uint32_t last_timing_print = t_now;

  if (t_now - last_timing_print >= 30000) {
    USBSerial.println("------------");
    for (uint8_t i = 0; i < 16; i++) {
      USBSerial.print(i);
      USBSerial.print(": ");
      USBSerial.println(function_hits[i]);

      function_hits[i] = 0;
    }

    last_timing_print = t_now;
  }
}

void set_mode_name(uint16_t index, const char* mode_name) {
  uint8_t len = strlen(mode_name);
  for (uint8_t i = 0; i < len; i++) {
    mode_names[32 * index + i] = mode_name[i];
  }
}

void init_system() {
  // SINGLE-CORE OPTIMIZATION: Mutex creation removed
  // Both threads run on Core 0, eliminating need for synchronization
  
  // BUTTON CORRUPTION IMMUNITY: Force clean button state initialization
  noise_button.pin = NOISE_CAL_PIN;
  mode_button.pin = MODE_PIN;
  
  // SURGICAL ADDITION: Force clean button state (ESP32-S3 immunity)
  noise_button.pressed = false;
  noise_button.last_down = 0;
  noise_button.last_up = 0;
  mode_button.pressed = false;
  mode_button.last_down = 0;
  mode_button.last_up = 0;
  
  // CRITICAL: Ensure transition flags start clean
  noise_transition_queued = false;
  mode_transition_queued = false;

  #ifndef ARDUINO_ESP32S3_DEV
  // Only configure button pins on S2 (S3 has no physical buttons)
  pinMode(noise_button.pin, INPUT_PULLUP);
  pinMode(mode_button.pin, INPUT_PULLUP);
  #endif

  // SURGICAL ADDITION: Force clean button state to prevent corruption
  noise_button.pressed = false;
  noise_button.last_down = 0;
  noise_button.last_up = 0;
  mode_button.pressed = false;
  mode_button.last_down = 0;
  mode_button.last_up = 0;
  noise_transition_queued = false;  // CRITICAL: Prevent phantom noise cal trigger

  memcpy(&CONFIG_DEFAULTS, &CONFIG, sizeof(CONFIG)); // Copy defaults values to second CONFIG object

  set_mode_name(0, "GDFT");
  set_mode_name(1, "CHROMAGRAM");
  set_mode_name(2, "CHROMAGRAM DOTS");
  set_mode_name(3, "BLOOM");
  set_mode_name(4, "VU DOT");
  set_mode_name(5, "KALEIDOSCOPE");
  set_mode_name(6, "QUANTUM COLLAPSE");
  set_mode_name(7, "SNAPWAVE");  // FIX: Was incorrectly named "WAVEFORM"
  set_mode_name(8, "SNAPWAVE_DEBUG");  // DEBUG variant

  init_usb();  // Initialize USB first for ESP32-S3
  init_serial(SERIAL_BAUD);

  #ifndef ARDUINO_ESP32S3_DEV
  init_sweet_spot();  // S3 has no sweet spot hardware
  #endif
  
  init_fs();

  #ifndef ARDUINO_ESP32S3_DEV
  // NOISE and MODE held down on boot (S2 only - S3 has no physical buttons)
  if (digitalRead(noise_button.pin) == LOW && digitalRead(mode_button.pin) == LOW) {
    restore_defaults();
  }
  #endif

  init_leds();

  #ifndef ARDUINO_ESP32S3_DEV
  // MODE held down on boot (S2 only - S3 has no physical buttons)
  if (digitalRead(mode_button.pin) == LOW) {
    enable_usb_update_mode();
  }
  #endif

  init_i2s();
  
  // FUCK P2P/WIFI - DISABLED TO STOP CRASHES
  // USBSerial.println("DEBUG: About to init P2P/WiFi...");
  // USBSerial.flush();
  // init_p2p();
  // USBSerial.println("DEBUG: P2P init complete!");
  
  USBSerial.println("P2P/WiFi DISABLED - Skipping init");
  
  generate_a_weights();
  generate_window_lookup();
  precompute_goertzel_constants();

  USBSerial.println("SYSTEM INIT COMPLETE!");

  if (CONFIG.BOOT_ANIMATION == true) {
    intro_animation();
  }
}

void log_fps(uint32_t t_now_us) {
  static uint32_t t_last = t_now_us;
  static float fps_history[10] = {0};
  static uint8_t fps_history_index = 0;

  uint32_t frame_delta_us = t_now_us - t_last;
  float fps_now = 1000000.0 / float(frame_delta_us);

  fps_history[fps_history_index] = fps_now;

  fps_history_index++;
  if (fps_history_index >= 10) {
    fps_history_index = 0;
  }

  float fps_sum = 0;
  for (uint8_t i = 0; i < 10; i++) {
    fps_sum += fps_history[i];
  }

  SYSTEM_FPS = fps_sum / 10.0;

  if (stream_fps == true) {
    USBSerial.print("sbs((fps=");
    USBSerial.print(SYSTEM_FPS);
    USBSerial.println("))");
  }

  t_last = t_now_us;
}

// This is to prevent overuse of internal flash writes!
// Instead of writing every single setting change to
// LittleFS, we wait until no settings have been altered
// for more than 3 seconds before attempting to update
// the flash with changes. This helps in scenarios where
// you're rapidly cycling through modes for example.
void check_settings(uint32_t t_now) {
  if (settings_updated) {
    if (t_now >= next_save_time) {
      if(debug_mode == true){
        USBSerial.println("QUEUED CONFIG SAVE TRIGGERED");
      }
      save_config();
      settings_updated = false;
    }
  }
}
