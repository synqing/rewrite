/*----------------------------------------
  Sensory Bridge FILESYSTEM ACCESS
  ----------------------------------------*/

#include "phase0_filesystem_safe.h"

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
  // Queue the actual save to happen in main loop
  config_save_pending = true;
}

// Actual file write - call this from main loop when safe
void do_config_save() {
  if (!config_save_pending) return;

  config_save_pending = false;
  lock_leds();

  if (debug_mode) {
    USBSerial.print("SAVING CONFIG: ");
  }

  // Use SafeFile for atomic write with CRC32 validation
  auto result = Phase0::Filesystem::SafeFile::write(
    config_filename,
    &CONFIG,
    sizeof(CONFIG)
  );

  if (!result.ok()) {
    if (debug_mode) {
      USBSerial.printf("FAILED - %s\n", result.statusString());
    }
  } else {
    if (debug_mode) {
      USBSerial.printf("SUCCESS (%zu bytes)\n", result.bytes_processed);
    }
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
    USBSerial.print("LOADING CONFIG: ");
  }

  size_t bytes_read = 0;
  auto result = Phase0::Filesystem::SafeFile::read(
    config_filename,
    &CONFIG,
    sizeof(CONFIG),
    &bytes_read
  );

  if (!result.ok()) {
    if (debug_mode) {
      USBSerial.printf("FAILED - %s\n", result.statusString());
      USBSerial.println("Using default CONFIG values...");
    }
    // Initialize with defaults when file doesn't exist or is corrupt
    init_config_defaults();
    save_config();  // Create the file with defaults
    unlock_leds();
    return;
  }

  if (debug_mode) {
    USBSerial.printf("SUCCESS (%zu bytes)\n", bytes_read);
  }

  unlock_leds();
}

// Save noise calibration to LittleFS
void save_ambient_noise_calibration() {
  lock_leds();
  if (debug_mode) {
    USBSerial.print("SAVING AMBIENT_NOISE PROFILE... ");
  }

  // Convert noise samples to float array for saving
  float noise_float[NUM_FREQS];
  for (uint16_t i = 0; i < NUM_FREQS; i++) {
    noise_float[i] = float(noise_samples[i]);
  }

  // Use SafeFile for atomic write with CRC32 validation
  auto result = Phase0::Filesystem::SafeFile::write(
    "/noise_cal.bin",
    noise_float,
    sizeof(noise_float)
  );

  if (!result.ok()) {
    if (debug_mode) {
      USBSerial.printf("FAILED - %s\n", result.statusString());
    }
  } else {
    if (debug_mode) {
      USBSerial.println("SUCCESS");
    }
  }

  unlock_leds();
}

// Load noise calibration from LittleFS
void load_ambient_noise_calibration() {
  lock_leds();
  if (debug_mode) {
    USBSerial.print("LOADING AMBIENT_NOISE PROFILE... ");
  }

  // Load into float array first
  float noise_float[NUM_FREQS];
  size_t bytes_read = 0;

  auto result = Phase0::Filesystem::SafeFile::read(
    "/noise_cal.bin",
    noise_float,
    sizeof(noise_float),
    &bytes_read
  );

  if (!result.ok()) {
    if (debug_mode) {
      USBSerial.printf("FAILED - %s\n", result.statusString());
    }
    unlock_leds();
    return;
  }

  // Convert back to SQ15x16 format
  for (uint16_t i = 0; i < NUM_FREQS; i++) {
    noise_samples[i] = SQ15x16(noise_float[i]);
  }

  if (debug_mode) {
    USBSerial.println("SUCCESS");
  }

  unlock_leds();
}

// Initialize LittleFS
void init_fs() {
  lock_leds();
  USBSerial.print("INIT FILESYSTEM: ");

  update_config_filename(FIRMWARE_VERSION);
  init_config_defaults();

  // Initialize safe filesystem with format on failure
  auto result = Phase0::Filesystem::SafeFile::initialize(true);
  if (!result.ok()) {
    USBSerial.printf("FAILED - %s\n", result.statusString());
    USBSerial.println("Using defaults only (no persistence)");
  } else {
    USBSerial.println("SUCCESS");
  }

  unlock_leds();
}
