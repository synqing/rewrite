# LightwaveOS Interface Specifications

## Overview

This document provides comprehensive specifications for all module interfaces, communication protocols, and data formats used in the modernized LightwaveOS architecture.

## Table of Contents

1. [Hardware Interfaces](#hardware-interfaces)
2. [Module APIs](#module-apis)
3. [Communication Protocols](#communication-protocols)
4. [Data Formats](#data-formats)
5. [Event Specifications](#event-specifications)
6. [Configuration Schema](#configuration-schema)
7. [Error Codes](#error-codes)

---

## Hardware Interfaces

### I2S Audio Interface

**Purpose**: Capture audio data from INMP441 MEMS microphone

```cpp
// Hardware Configuration
struct I2SConfig {
    // Pin assignments
    uint8_t bck_io_num = 7;       // Bit clock
    uint8_t ws_io_num = 13;       // Word select (L/R)
    uint8_t data_in_num = 8;      // Data input

    // I2S Configuration
    i2s_mode_t mode = I2S_MODE_MASTER | I2S_MODE_RX;
    uint32_t sample_rate = 16000;  // Hz
    i2s_bits_per_sample_t bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    i2s_channel_fmt_t channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;

    // DMA Configuration
    size_t dma_buf_count = 4;
    size_t dma_buf_len = 512;

    // Timing
    uint32_t timeout_ms = 100;
};

// Data Format
struct AudioSample {
    int16_t value;              // -32768 to 32767
    uint32_t timestamp;         // Microseconds since boot
};

// Interface Methods
class I2SAudioInterface {
    virtual esp_err_t initialize(const I2SConfig& config) = 0;
    virtual esp_err_t read(AudioSample* buffer, size_t count, size_t* bytes_read) = 0;
    virtual esp_err_t stop() = 0;
    virtual esp_err_t start() = 0;
};
```

### SPI LED Interface (WS2812B)

**Purpose**: Control addressable LED strips via SPI with DMA

```cpp
// LED Data Format
struct LEDPixel {
    uint8_t green;  // Note: WS2812B uses GRB order
    uint8_t red;
    uint8_t blue;
};

// SPI Configuration for WS2812B
struct SPILEDConfig {
    // Pin assignments
    uint8_t data_pin = 9;       // Primary strip
    uint8_t data_pin_2 = 10;    // Secondary strip (optional)

    // Timing (for 800kHz WS2812B protocol)
    uint32_t t0h_ns = 350;      // 0-bit high time
    uint32_t t0l_ns = 800;      // 0-bit low time
    uint32_t t1h_ns = 700;      // 1-bit high time
    uint32_t t1l_ns = 600;      // 1-bit low time
    uint32_t reset_us = 50;     // Reset time

    // Strip configuration
    uint16_t num_leds = 160;
    bool dual_strip = false;
};

// Interface Methods
class SPILEDInterface {
    virtual esp_err_t initialize(const SPILEDConfig& config) = 0;
    virtual esp_err_t write(const LEDPixel* pixels, size_t count) = 0;
    virtual esp_err_t setBrightness(uint8_t brightness) = 0;
    virtual esp_err_t show() = 0;  // Latch data to LEDs
};
```

### I2C Encoder Interface (M5Rotate8)

**Purpose**: Read rotary encoder positions via I2C

```cpp
// I2C Configuration
struct I2CConfig {
    // Pin assignments
    uint8_t sda_pin = 3;
    uint8_t scl_pin = 4;

    // I2C parameters
    uint32_t frequency = 400000;  // 400kHz fast mode
    uint8_t device_address = 0x41;  // M5Rotate8 address

    // Timing
    uint32_t timeout_ms = 100;
    uint32_t retry_count = 3;
};

// Encoder Data
struct EncoderState {
    int16_t positions[8];        // Current positions
    int8_t increments[8];        // Change since last read
    uint8_t button_states;       // Bit field for button states
    uint32_t timestamp;
};

// Interface Methods
class I2CEncoderInterface {
    virtual esp_err_t initialize(const I2CConfig& config) = 0;
    virtual esp_err_t readAll(EncoderState* state) = 0;
    virtual esp_err_t readSingle(uint8_t encoder_id, int16_t* position) = 0;
    virtual esp_err_t reset(uint8_t encoder_id = 0xFF) = 0;  // 0xFF = all
    virtual esp_err_t recoverFromError() = 0;
};
```

---

## Module APIs

### Audio Processing API

```cpp
namespace Core::Audio {

// Audio frame structure
struct AudioFrame {
    static constexpr size_t FRAME_SIZE = 128;

    int16_t samples[FRAME_SIZE];
    uint32_t timestamp;
    uint8_t channel_count;
    uint32_t sample_rate;

    // Computed properties
    float getRMS() const;
    float getPeak() const;
    bool isSilent(float threshold = 0.01f) const;
};

// Audio processor interface
class IAudioProcessor {
public:
    // Lifecycle
    virtual Status initialize(const AudioConfig& config) = 0;
    virtual Status start() = 0;
    virtual Status stop() = 0;

    // Processing
    virtual Result<AudioFrame> captureFrame() = 0;
    virtual void registerCallback(std::function<void(const AudioFrame&)> cb) = 0;

    // Configuration
    virtual AudioConfig getConfig() const = 0;
    virtual Status reconfigure(const AudioConfig& config) = 0;

    // Statistics
    virtual AudioStats getStats() const = 0;
};

// Audio statistics
struct AudioStats {
    uint64_t frames_captured;
    uint64_t frames_dropped;
    float average_rms;
    float peak_level;
    uint32_t silence_count;
    uint32_t clipping_count;
};

} // namespace Core::Audio
```

### DSP Processing API

```cpp
namespace Core::DSP {

// Frequency spectrum data
class SpectrumData {
public:
    static constexpr size_t NUM_BINS = 96;

    // Access methods
    float getMagnitude(size_t bin) const;
    float getPhase(size_t bin) const;
    float getFrequency(size_t bin) const;

    // Analysis
    size_t getPeakBin() const;
    float getTotalEnergy() const;
    float getSpectralCentroid() const;
    std::array<float, 12> toChromagram() const;

private:
    std::array<float, NUM_BINS> magnitudes;
    std::array<float, NUM_BINS> phases;
    std::array<float, NUM_BINS> frequencies;
};

// DSP processor interface
class IDSPProcessor {
public:
    // Processing
    virtual Result<SpectrumData> computeSpectrum(const AudioFrame& frame) = 0;
    virtual float computeNovelty(const SpectrumData& current) = 0;

    // Feature extraction
    virtual std::array<float, 13> extractMFCC(const AudioFrame& frame) = 0;
    virtual std::array<float, 12> extractChroma(const SpectrumData& spectrum) = 0;

    // Configuration
    virtual Status configure(const DSPConfig& config) = 0;
};

} // namespace Core::DSP
```

### LED Rendering API

```cpp
namespace Core::Render {

// Color structure with operations
struct Color {
    uint8_t r, g, b;

    // Factory methods
    static Color fromHSV(float h, float s, float v);
    static Color fromTemperature(uint16_t kelvin);
    static Color fromHex(uint32_t hex);

    // Operations
    Color blend(const Color& other, float factor) const;
    Color brighten(float factor) const;
    Color gammaCorrect(float gamma = 2.2f) const;

    // Conversions
    uint32_t toHex() const;
    std::tuple<float, float, float> toHSV() const;
};

// Frame buffer for LED data
class RenderFrame {
public:
    explicit RenderFrame(size_t size) : pixels(size) {}

    // Pixel access
    Color& operator[](size_t index);
    const Color& operator[](size_t index) const;

    // Operations
    void fill(const Color& color);
    void fadeToBlack(float rate);
    void blur(float amount);
    void shift(int offset, bool wrap = true);

    // Getters
    size_t size() const { return pixels.size(); }
    const std::vector<Color>& data() const { return pixels; }

private:
    std::vector<Color> pixels;
};

// LED renderer interface
class ILEDRenderer {
public:
    // Lifecycle
    virtual Status initialize(const LEDConfig& config) = 0;

    // Rendering
    virtual Status render(const RenderFrame& frame) = 0;
    virtual Status show() = 0;

    // Effects
    virtual Status transition(const RenderFrame& target, TransitionType type, uint32_t duration_ms) = 0;

    // Configuration
    virtual Status setBrightness(uint8_t brightness) = 0;
    virtual Status setColorCorrection(const Color& correction) = 0;
};

} // namespace Core::Render
```

### Mode Plugin API

```cpp
namespace Application::Modes {

// Audio features for visualization modes
struct AudioFeatures {
    SpectrumData spectrum;
    std::array<float, 12> chromagram;
    float energy;
    float novelty;
    float beat_probability;

    // Time-domain features
    float rms;
    float zero_crossing_rate;
    float spectral_centroid;
    float spectral_flux;
};

// Mode configuration
class ModeConfig {
public:
    void setParameter(const std::string& key, float value);
    float getParameter(const std::string& key, float default_value = 0.0f) const;
    std::vector<std::string> getParameterNames() const;

private:
    std::map<std::string, float> parameters;
};

// Visualization mode interface
class IVisualizationMode {
public:
    virtual ~IVisualizationMode() = default;

    // Core interface
    virtual std::string getName() const = 0;
    virtual std::string getDescription() const = 0;
    virtual RenderFrame process(const AudioFeatures& features) = 0;

    // Configuration
    virtual void configure(const ModeConfig& config) = 0;
    virtual ModeConfig getDefaultConfig() const = 0;

    // Lifecycle hooks
    virtual void onActivate() {}
    virtual void onDeactivate() {}
    virtual void onBeat() {}  // Called on beat detection

    // Capabilities
    virtual bool requiresHistory() const { return false; }
    virtual uint32_t getDesiredFPS() const { return 60; }
};

// Mode factory
using ModeFactory = std::function<std::unique_ptr<IVisualizationMode>()>;

// Mode registry interface
class IModeRegistry {
public:
    virtual void registerMode(const std::string& name, ModeFactory factory) = 0;
    virtual std::unique_ptr<IVisualizationMode> createMode(const std::string& name) = 0;
    virtual std::vector<std::string> getAvailableModes() const = 0;
};

} // namespace Application::Modes
```

---

## Communication Protocols

### Inter-Core Communication

**Protocol**: FreeRTOS Message Queues

```cpp
// Message types
enum class MessageType : uint8_t {
    AUDIO_DATA = 0x01,
    SPECTRUM_DATA = 0x02,
    CONFIG_UPDATE = 0x03,
    MODE_CHANGE = 0x04,
    ERROR_REPORT = 0x05,
    HEARTBEAT = 0x06
};

// Message header
struct MessageHeader {
    MessageType type;
    uint8_t version;
    uint16_t length;
    uint32_t timestamp;
    uint32_t sequence;
};

// Audio data message
struct AudioDataMessage {
    MessageHeader header{MessageType::AUDIO_DATA, 1, sizeof(AudioDataMessage)};
    AudioFrame frame;
    AudioStats stats;
};

// Spectrum data message
struct SpectrumDataMessage {
    MessageHeader header{MessageType::SPECTRUM_DATA, 1, sizeof(SpectrumDataMessage)};
    SpectrumData spectrum;
    float novelty;
    std::array<float, 12> chromagram;
};

// Message queue interface
template<typename T>
class MessageQueue {
public:
    MessageQueue(size_t capacity) : handle(xQueueCreate(capacity, sizeof(T))) {}

    bool send(const T& message, TickType_t timeout = 0) {
        return xQueueSend(handle, &message, timeout) == pdTRUE;
    }

    bool receive(T& message, TickType_t timeout = portMAX_DELAY) {
        return xQueueReceive(handle, &message, timeout) == pdTRUE;
    }

    size_t available() const {
        return uxQueueMessagesWaiting(handle);
    }

private:
    QueueHandle_t handle;
};
```

### Serial Communication Protocol

**Format**: JSON-based command/response

```cpp
// Command structure
struct SerialCommand {
    std::string command;
    std::map<std::string, std::string> parameters;
    uint32_t id;  // Request ID for matching responses
};

// Response structure
struct SerialResponse {
    uint32_t id;  // Matches request ID
    bool success;
    std::string data;
    std::string error;
};

// Serial protocol
class SerialProtocol {
public:
    // Framing
    static constexpr char START_BYTE = 0x02;  // STX
    static constexpr char END_BYTE = 0x03;    // ETX
    static constexpr char ESCAPE_BYTE = 0x1B; // ESC

    // Commands
    static constexpr const char* CMD_GET_CONFIG = "GET_CONFIG";
    static constexpr const char* CMD_SET_CONFIG = "SET_CONFIG";
    static constexpr const char* CMD_GET_MODE = "GET_MODE";
    static constexpr const char* CMD_SET_MODE = "SET_MODE";
    static constexpr const char* CMD_GET_STATUS = "GET_STATUS";
    static constexpr const char* CMD_CALIBRATE = "CALIBRATE";
    static constexpr const char* CMD_RESET = "RESET";

    // Encoding/Decoding
    static std::string encode(const SerialCommand& cmd);
    static Result<SerialCommand> decode(const std::string& data);
};

// Example command/response
// Command: {"cmd":"SET_MODE","params":{"mode":"spectrum"},"id":123}
// Response: {"id":123,"success":true,"data":"Mode changed to spectrum"}
```

### OTA Update Protocol

```cpp
// OTA manifest
struct OTAManifest {
    std::string version;
    std::string board_type;
    uint32_t firmware_size;
    std::string checksum_md5;
    std::string checksum_sha256;
    std::string minimum_version;
    std::vector<std::string> release_notes;
    uint32_t chunk_size = 4096;
};

// OTA commands
enum class OTACommand : uint8_t {
    CHECK_UPDATE = 0x01,
    DOWNLOAD_MANIFEST = 0x02,
    DOWNLOAD_CHUNK = 0x03,
    VERIFY_CHECKSUM = 0x04,
    APPLY_UPDATE = 0x05,
    ROLLBACK = 0x06,
    GET_STATUS = 0x07
};

// OTA status
enum class OTAStatus : uint8_t {
    IDLE = 0x00,
    CHECKING = 0x01,
    DOWNLOADING = 0x02,
    VERIFYING = 0x03,
    APPLYING = 0x04,
    SUCCESS = 0x05,
    FAILED = 0x06,
    ROLLING_BACK = 0x07
};

// OTA interface
class IOTAClient {
public:
    virtual Result<OTAManifest> checkForUpdate() = 0;
    virtual Result<void> downloadFirmware(const OTAManifest& manifest) = 0;
    virtual Result<void> applyUpdate() = 0;
    virtual Result<void> rollback() = 0;
    virtual OTAStatus getStatus() const = 0;
    virtual uint8_t getProgress() const = 0;  // 0-100%
};
```

---

## Data Formats

### Configuration File Format

**Format**: JSON with schema validation

```json
{
  "$schema": "https://lightwave.io/schemas/config-v1.json",
  "version": "1.0.0",
  "audio": {
    "sample_rate": 16000,
    "buffer_size": 128,
    "dc_offset": -14800,
    "agc_enabled": false,
    "noise_floor": 0.01
  },
  "dsp": {
    "num_bins": 96,
    "min_frequency": 55.0,
    "max_frequency": 4186.0,
    "smoothing_factor": 0.8,
    "novelty_threshold": 0.3
  },
  "led": {
    "num_leds": 160,
    "brightness": 255,
    "color_correction": {
      "r": 255,
      "g": 176,
      "b": 240
    },
    "gamma": 2.2,
    "fps_target": 60
  },
  "mode": {
    "current": "spectrum",
    "parameters": {
      "hue_offset": 0.0,
      "saturation": 1.0,
      "gain": 1.5
    }
  }
}
```

### Preset Format

```json
{
  "name": "Warm White",
  "description": "Simulates incandescent bulb colors",
  "author": "LightwaveOS Team",
  "version": "1.0",
  "config": {
    "led.color_correction": {"r": 255, "g": 197, "b": 143},
    "led.gamma": 2.8,
    "mode.current": "spectrum",
    "mode.parameters.saturation": 0.3,
    "mode.parameters.hue_offset": 30
  }
}
```

### Telemetry Data Format

```json
{
  "timestamp": 1698765432,
  "device_id": "esp32-s3-001",
  "firmware_version": "2.1.0",
  "metrics": {
    "uptime_seconds": 3600,
    "heap_free": 98304,
    "heap_min_free": 85120,
    "cpu_usage": 45.2,
    "temperature_celsius": 42.5,
    "fps_audio": 121.3,
    "fps_led": 62.1,
    "frames_dropped": 0,
    "errors": []
  },
  "audio_stats": {
    "peak_level": 0.82,
    "average_rms": 0.15,
    "silence_periods": 2,
    "clipping_events": 0
  }
}
```

---

## Event Specifications

### Event Types

```cpp
namespace Core::Events {

// Base event
class Event {
protected:
    uint32_t timestamp;
    std::string source;
    uint32_t sequence;

public:
    virtual std::string getType() const = 0;
    virtual std::string serialize() const = 0;
};

// System events
class SystemStartedEvent : public Event {
    std::string firmware_version;
    uint32_t free_heap;
};

class SystemErrorEvent : public Event {
    ErrorCode code;
    std::string message;
    std::string stack_trace;
};

// Audio events
class AudioFrameReadyEvent : public Event {
    AudioFrame frame;
    AudioStats stats;
};

class AudioSilenceDetectedEvent : public Event {
    uint32_t duration_ms;
};

// DSP events
class BeatDetectedEvent : public Event {
    float confidence;
    uint32_t bpm;
};

class FrequencyPeakEvent : public Event {
    float frequency;
    float magnitude;
};

// Mode events
class ModeChangedEvent : public Event {
    std::string previous_mode;
    std::string new_mode;
};

class ModeParameterChangedEvent : public Event {
    std::string parameter;
    float old_value;
    float new_value;
};

// Configuration events
class ConfigurationChangedEvent : public Event {
    std::string key;
    std::string old_value;
    std::string new_value;
};

} // namespace Core::Events
```

### Event Bus Interface

```cpp
class IEventBus {
public:
    // Subscription
    using Handler = std::function<void(const Event&)>;
    virtual void subscribe(const std::string& event_type, Handler handler) = 0;
    virtual void unsubscribe(const std::string& event_type, Handler handler) = 0;

    // Publishing
    virtual void publish(std::unique_ptr<Event> event) = 0;
    virtual void publishAsync(std::unique_ptr<Event> event) = 0;

    // Filtering
    virtual void setFilter(std::function<bool(const Event&)> filter) = 0;

    // Statistics
    virtual EventStats getStats() const = 0;
};

struct EventStats {
    uint64_t total_published;
    uint64_t total_delivered;
    std::map<std::string, uint64_t> events_by_type;
    uint32_t queue_depth;
};
```

---

## Configuration Schema

### JSON Schema Definition

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "LightwaveOS Configuration",
  "type": "object",
  "required": ["version", "audio", "dsp", "led", "mode"],
  "properties": {
    "version": {
      "type": "string",
      "pattern": "^\\d+\\.\\d+\\.\\d+$"
    },
    "audio": {
      "type": "object",
      "properties": {
        "sample_rate": {
          "type": "integer",
          "enum": [8000, 16000, 22050, 32000, 44100, 48000]
        },
        "buffer_size": {
          "type": "integer",
          "minimum": 64,
          "maximum": 4096,
          "multipleOf": 64
        },
        "dc_offset": {
          "type": "integer",
          "minimum": -32768,
          "maximum": 32767
        },
        "agc_enabled": {
          "type": "boolean"
        },
        "noise_floor": {
          "type": "number",
          "minimum": 0.0,
          "maximum": 1.0
        }
      }
    },
    "dsp": {
      "type": "object",
      "properties": {
        "num_bins": {
          "type": "integer",
          "minimum": 16,
          "maximum": 256,
          "multipleOf": 16
        },
        "min_frequency": {
          "type": "number",
          "minimum": 20.0,
          "maximum": 20000.0
        },
        "max_frequency": {
          "type": "number",
          "minimum": 20.0,
          "maximum": 20000.0
        },
        "smoothing_factor": {
          "type": "number",
          "minimum": 0.0,
          "maximum": 1.0
        },
        "novelty_threshold": {
          "type": "number",
          "minimum": 0.0,
          "maximum": 1.0
        }
      }
    },
    "led": {
      "type": "object",
      "properties": {
        "num_leds": {
          "type": "integer",
          "minimum": 1,
          "maximum": 1000
        },
        "brightness": {
          "type": "integer",
          "minimum": 0,
          "maximum": 255
        },
        "color_correction": {
          "type": "object",
          "properties": {
            "r": {"type": "integer", "minimum": 0, "maximum": 255},
            "g": {"type": "integer", "minimum": 0, "maximum": 255},
            "b": {"type": "integer", "minimum": 0, "maximum": 255}
          }
        },
        "gamma": {
          "type": "number",
          "minimum": 1.0,
          "maximum": 4.0
        },
        "fps_target": {
          "type": "integer",
          "minimum": 1,
          "maximum": 240
        }
      }
    },
    "mode": {
      "type": "object",
      "properties": {
        "current": {
          "type": "string",
          "enum": ["spectrum", "chromagram", "bloom", "kaleidoscope", "quantum", "snapwave"]
        },
        "parameters": {
          "type": "object",
          "additionalProperties": {
            "type": "number"
          }
        }
      }
    }
  }
}
```

---

## Error Codes

### Error Code Definitions

```cpp
enum class ErrorCode : uint32_t {
    // Success
    OK = 0x0000,

    // Audio errors (0x1xxx)
    AUDIO_INIT_FAILED = 0x1001,
    AUDIO_CAPTURE_TIMEOUT = 0x1002,
    AUDIO_BUFFER_OVERFLOW = 0x1003,
    AUDIO_DMA_ERROR = 0x1004,
    AUDIO_FORMAT_INVALID = 0x1005,

    // DSP errors (0x2xxx)
    DSP_INIT_FAILED = 0x2001,
    DSP_PROCESSING_ERROR = 0x2002,
    DSP_INVALID_PARAMS = 0x2003,
    DSP_OVERFLOW = 0x2004,

    // LED errors (0x3xxx)
    LED_INIT_FAILED = 0x3001,
    LED_SPI_ERROR = 0x3002,
    LED_DMA_ERROR = 0x3003,
    LED_INVALID_DATA = 0x3004,

    // Configuration errors (0x4xxx)
    CONFIG_INVALID = 0x4001,
    CONFIG_PARSE_ERROR = 0x4002,
    CONFIG_SAVE_FAILED = 0x4003,
    CONFIG_LOAD_FAILED = 0x4004,
    CONFIG_VALIDATION_FAILED = 0x4005,

    // System errors (0x5xxx)
    SYSTEM_OUT_OF_MEMORY = 0x5001,
    SYSTEM_STACK_OVERFLOW = 0x5002,
    SYSTEM_WATCHDOG_TIMEOUT = 0x5003,
    SYSTEM_TASK_CREATE_FAILED = 0x5004,
    SYSTEM_QUEUE_FULL = 0x5005,

    // Communication errors (0x6xxx)
    COMM_TIMEOUT = 0x6001,
    COMM_CHECKSUM_ERROR = 0x6002,
    COMM_PROTOCOL_ERROR = 0x6003,
    COMM_BUFFER_OVERFLOW = 0x6004,

    // OTA errors (0x7xxx)
    OTA_DOWNLOAD_FAILED = 0x7001,
    OTA_CHECKSUM_MISMATCH = 0x7002,
    OTA_APPLY_FAILED = 0x7003,
    OTA_ROLLBACK_FAILED = 0x7004,
    OTA_INCOMPATIBLE_VERSION = 0x7005,

    // Unknown error
    UNKNOWN = 0xFFFF
};

// Error information
struct ErrorInfo {
    ErrorCode code;
    std::string message;
    std::string file;
    uint32_t line;
    uint32_t timestamp;

    std::string toString() const {
        return fmt::format("[{}] Error {:#06x}: {} ({}:{})",
                          timestamp, static_cast<uint32_t>(code),
                          message, file, line);
    }
};

// Error handler interface
class IErrorHandler {
public:
    virtual void handleError(const ErrorInfo& error) = 0;
    virtual void handleFatalError(const ErrorInfo& error) = 0;
    virtual std::vector<ErrorInfo> getErrorHistory(size_t count = 10) const = 0;
    virtual void clearErrors() = 0;
};
```

---

## Interface Versioning

### Version Management

```cpp
namespace Core {

// Semantic versioning
struct Version {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;

    std::string toString() const {
        return fmt::format("{}.{}.{}", major, minor, patch);
    }

    bool isCompatible(const Version& other) const {
        return major == other.major && minor >= other.minor;
    }

    bool operator<(const Version& other) const {
        return std::tie(major, minor, patch) <
               std::tie(other.major, other.minor, other.patch);
    }
};

// Interface version information
template<typename Interface>
struct InterfaceVersion {
    static constexpr Version CURRENT = {1, 0, 0};
    static constexpr Version MINIMUM_SUPPORTED = {1, 0, 0};

    static bool isSupported(const Version& version) {
        return version >= MINIMUM_SUPPORTED &&
               version.major == CURRENT.major;
    }
};

// Versioned interface base
template<typename T>
class IVersionedInterface {
public:
    virtual Version getInterfaceVersion() const = 0;
    virtual bool supportsVersion(const Version& version) const = 0;
};

} // namespace Core
```

---

## Conclusion

These interface specifications provide a complete contract for all module interactions in the LightwaveOS system. By adhering to these specifications, modules can be developed independently while maintaining compatibility and enabling seamless integration.

Key benefits:
1. **Clear Contracts**: Every interface is fully specified
2. **Type Safety**: Strong typing prevents errors
3. **Versioning**: Interfaces can evolve safely
4. **Documentation**: Self-documenting code
5. **Testability**: Interfaces enable mocking and testing

The specifications should be treated as the authoritative source for all module development and integration efforts.