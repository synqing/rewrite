/*----------------------------------------
  Sensory Bridge FILESYSTEM ACCESS
  ----------------------------------------*/

extern void reboot(); // system.h

void update_config_filename(uint32_t input) {
  snprintf(config_filename, 24, "/CONFIG_%05lu.BIN", input);
}

void init_config_defaults() {
  // CONFIG is already initialized with defaults in globals.h
  // This function exists for compatibility
  CONFIG_DEFAULTS = CONFIG;
}

// Restore all defaults defined in globals.h by removing saved data and rebooting
void factory_reset() {
  lock_leds();
  USBSerial.print("Deleting ");
  USBSerial.print(config_filename);
  USBSerial.print(": ");

  if (LittleFS.remove(config_filename)) {
    USBSerial.println("file deleted");
  } else {
    USBSerial.println("delete failed");
  }

  USBSerial.print("Deleting noise_cal.bin: ");
  if (LittleFS.remove("/noise_cal.bin")) {
    USBSerial.println("file deleted");
  } else {
    USBSerial.println("delete failed");
  }

  reboot();
}

// Restore only configuration defaults
void restore_defaults() {
  lock_leds();
  USBSerial.print("Deleting ");
  USBSerial.print(config_filename);
  USBSerial.print(": ");

  if (LittleFS.remove(config_filename)) {
    USBSerial.println("file deleted");
  } else {
    USBSerial.println("delete failed");
  }

  reboot();
}

// Flag to indicate config needs saving
bool config_save_pending = false;

// Save configuration to LittleFS
void save_config() {
  // FUCK THE FILESYSTEM - DO NOTHING
  if (debug_mode) {
    USBSerial.println("CONFIG SAVE BYPASSED - NO FILESYSTEM");
  }
}

// Actual file write - call this from main loop when safe
void do_config_save() {
  // FUCK THE FILESYSTEM - DO NOTHING
  return;
  
  if (!config_save_pending) return;
  
  config_save_pending = false;
  lock_leds();
  
  if (debug_mode) {
    USBSerial.print("LITTLEFS: ");
  }
  
  // Multiple yields to ensure watchdog doesn't trigger
  yield();
  delay(1);  // Give time for other tasks
  yield();
  
  File file = LittleFS.open(config_filename, FILE_WRITE);
  if (!file) {
    if (debug_mode) {
      USBSerial.print("Failed to open ");
      USBSerial.print(config_filename);
      USBSerial.println(" for writing!");
    }
    unlock_leds();
    return;
  }
  
  file.seek(0);
  uint8_t config_buffer[512];
  memcpy(config_buffer, &CONFIG, sizeof(CONFIG));
  
  // Write in small chunks with delays
  for (uint16_t i = 0; i < 512; i += 32) {
    size_t bytes_to_write = (32 < (512 - i)) ? 32 : (512 - i);
    file.write(&config_buffer[i], bytes_to_write);
    if (i % 64 == 0) {
      yield();
      vTaskDelay(1);  // Use RTOS delay instead of blocking delay
    }
  }
  
  file.close();
  
  if (debug_mode) {
    USBSerial.print("WROTE ");
    USBSerial.print(config_filename);
    USBSerial.println(" SUCCESSFULLY");
  }
  
  unlock_leds();
}

// Save configuration to LittleFS after delay
void save_config_delayed() {
  if(debug_mode == true){
    USBSerial.println("CONFIG SAVE QUEUED");
  }
  // Increased delay to 10 seconds to avoid conflicts
  next_save_time = millis()+10000;
  settings_updated = true;
}

// Load configuration from LittleFS
void load_config() {
  lock_leds();
  if (debug_mode) {
    USBSerial.print("LITTLEFS: ");
  }

  bool queue_factory_reset = false;
  File file = LittleFS.open(config_filename, FILE_READ);
  if (!file) {
    if (debug_mode) {
      USBSerial.print("Failed to open ");
      USBSerial.print(config_filename);
      USBSerial.println(" for reading!");
      USBSerial.println("Initializing with default CONFIG values...");
    }
    // Initialize with defaults when file doesn't exist
    init_config_defaults();
    save_config();  // Create the file with defaults
    unlock_leds();  // CRITICAL: Must unlock before returning!
    return;
  } else {
    file.seek(0);
    uint8_t config_buffer[512];
    for (uint16_t i = 0; i < sizeof(CONFIG); i++) {
      config_buffer[i] = file.read();
    }

    memcpy(&CONFIG, config_buffer, sizeof(CONFIG));

    if (debug_mode) {
      USBSerial.println("READ CONFIG SUCCESSFULLY");
    }
  }
  file.close();

  if (queue_factory_reset == true) {
    factory_reset();
  }
  unlock_leds();
}

// Save noise calibration to LittleFS
void save_ambient_noise_calibration() {
  lock_leds();
  if (debug_mode) {
    USBSerial.print("SAVING AMBIENT_NOISE PROFILE... ");
  }
  
  // CRITICAL: Yield to watchdog before file operations
  yield();
  
  File file = LittleFS.open("/noise_cal.bin", FILE_WRITE);
  if (!file) {
    if (debug_mode) {
      USBSerial.println("Failed to open file for writing!");
    }
    unlock_leds();  // CRITICAL: Must unlock before returning!
    return;
  }

  bytes_32 temp;

  file.seek(0);
  for (uint16_t i = 0; i < NUM_FREQS; i++) {
    float in_val = float(noise_samples[i]);

    temp.long_val_float = in_val;

    file.write(temp.bytes[0]);
    file.write(temp.bytes[1]);
    file.write(temp.bytes[2]);
    file.write(temp.bytes[3]);
    
    // Yield every 8 frequencies to prevent watchdog timeout
    if ((i & 0x07) == 0) {
      yield();
      vTaskDelay(1);  // Give time to other tasks
    }
  }

  file.close();
  if (debug_mode) {
    USBSerial.println("SAVE COMPLETE");
  }

  unlock_leds();
}

// Load noise calibration from LittleFS
void load_ambient_noise_calibration() {
  lock_leds();
  if (debug_mode) {
    USBSerial.print("LOADING AMBIENT_NOISE PROFILE... ");
  }
  File file = LittleFS.open("/noise_cal.bin", FILE_READ);
  if (!file) {
    if (debug_mode) {
      USBSerial.println("Failed to open file for reading!");
    }
    unlock_leds();  // CRITICAL: Must unlock before returning!
    return;
  }

  bytes_32 temp;

  file.seek(0);
  for (uint16_t i = 0; i < NUM_FREQS; i++) {
    temp.bytes[0] = file.read();
    temp.bytes[1] = file.read();
    temp.bytes[2] = file.read();
    temp.bytes[3] = file.read();

    noise_samples[i] = SQ15x16(temp.long_val_float);
  }

  file.close();
  if (debug_mode) {
    USBSerial.println("LOAD COMPLETE");
  }

  unlock_leds();
}

// Initialize LittleFS
void init_fs() {
  lock_leds();
  USBSerial.print("INIT FILESYSTEM: ");
  USBSerial.println("BYPASSED - FUCK IT");
  
  // Just use defaults and skip filesystem entirely
  update_config_filename(FIRMWARE_VERSION);
  init_config_defaults();
  
  // Set some reasonable defaults for LED testing
  CONFIG.LED_COUNT = 160;
  CONFIG.LED_TYPE = LED_NEOPIXEL;
  CONFIG.LIGHTSHOW_MODE = LIGHT_MODE_SNAPWAVE;
  CONFIG.PHOTONS = 0.5;
  CONFIG.CHROMA = 0.5;
  CONFIG.MOOD = 0.5;
  CONFIG.SATURATION = 1.0;
  CONFIG.AUTO_COLOR_SHIFT = true;
  CONFIG.SAMPLES_PER_CHUNK = 128;  // Force lower latency
  CONFIG.SAMPLE_RATE = 32000;  // 32kHz for S3
  
  unlock_leds();
}
