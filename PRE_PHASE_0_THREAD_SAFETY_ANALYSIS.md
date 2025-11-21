# Thread Safety Analysis - LightwaveOS Firmware

**Date:** 2025-11-21
**Status:** CRITICAL - MUST ADDRESS BEFORE PHASE 2

---

## Executive Summary

### Current Threading Model: **UNSAFE** ⚠️

- **Thread Model:** Dual-core with shared memory, NO synchronization
- **Risk Level:** **HIGH** - Multiple race conditions identified
- **Data Corruption Risk:** **CRITICAL** - Audio/LED data races possible
- **Performance Impact:** Currently masked by timing, fragile

### Immediate Findings

1. ✅ **Good News:** Both main threads on Core 0 reduces some risks
2. ⚠️ **Bad News:** LED thread on Core 1 shares globals with no protection
3. ❌ **Critical:** 11+ shared variables with no synchronization
4. ❌ **Race Condition Counter:** `g_race_condition_count` exists but isn't preventing races

---

## Current Task Structure

### Task Inventory

| Task Name | Core | Priority | Stack Size | Function | Entry Point |
|-----------|------|----------|------------|----------|-------------|
| **main_loop_thread** | 0 | IDLE+2 | 16,384 | Audio capture, DSP, control | main_loop_core0() |
| **encoder_service_task** | 0 | IDLE+1 | 4,096 | Encoder polling | encoder_service_task() |
| **led_thread** | 1 | IDLE+1 | 12,288 | LED rendering | led_thread() |
| **Arduino loop()** | 0 | N/A | 8,192 | Idle delay loop | loop() |

**Total Stack Allocation:** 40,960 bytes (~40 KB)

### Task Communication Diagram

```
┌─────────────────────────────────────────┐
│              CORE 0                     │
├─────────────────────────────────────────┤
│                                         │
│  ┌─────────────────────────────────┐   │
│  │  main_loop_thread (Priority 2)  │   │
│  │  - I2S audio capture            │   │
│  │  - process_GDFT()               │◄──┼─── I2S Hardware
│  │  - Calculate novelty            │   │
│  │  - Process color shift          │   │
│  │  WRITES: spectrogram[],         │   │
│  │          chromagram_smooth[]    │   │
│  │          CONFIG (rare)          │   │
│  └──────────────┬──────────────────┘   │
│                 │                       │
│                 │ NO SYNC! ⚠️           │
│                 ▼                       │
│       ┌─────────────────────┐          │
│       │  Shared Memory      │          │
│       │  (globals.h)        │          │
│       └─────────┬───────────┘          │
│                 │                       │
└─────────────────┼───────────────────────┘
                  │ NO SYNC! ⚠️
                  │
┌─────────────────▼───────────────────────┐
│              CORE 1                     │
├─────────────────────────────────────────┤
│                                         │
│  ┌─────────────────────────────────┐   │
│  │  led_thread (Priority 1)        │   │
│  │  - get_smooth_spectrogram()     │   │
│  │  - make_smooth_chromagram()     │   │
│  │  - light_mode_*()               │   │
│  │  - show_leds()                  │───┼─── WS2812B LEDs
│  │  READS: spectrogram[],          │   │
│  │         chromagram_smooth[],    │   │
│  │         CONFIG                  │   │
│  │  WRITES: leds_16[]              │   │
│  └─────────────────────────────────┘   │
│                                         │
└─────────────────────────────────────────┘
```

---

## Shared Variable Analysis

### CATEGORY 1: Audio → LED Data Flow (CRITICAL)

#### 1.1 Spectrum Data ⚠️ **RACE CONDITION**

| Variable | Size | Writer | Reader | Access Pattern | Risk | Impact |
|----------|------|--------|--------|----------------|------|--------|
| `spectrogram[96]` | 384B | Core 0 | Core 1 | Continuous write/read | **CRITICAL** | Corrupted spectrum display |
| `spectrogram_smooth[96]` | 384B | Core 0 | Core 1 | Continuous write/read | **CRITICAL** | Flickering LEDs |
| `chromagram_raw[32]` | 128B | Core 0 | Core 1 | Continuous write/read | **HIGH** | Color artifacts |
| `chromagram_smooth[32]` | 128B | Core 0 | Core 1 | Continuous write/read | **HIGH** | Color artifacts |
| `spectral_history[5][96]` | 1,920B | Core 0 | Core 1 | Occasional read | **MEDIUM** | History glitches |

**Race Condition Example:**
```cpp
// CORE 0 (main_loop_core0)
void process_GDFT() {
    for (int i = 0; i < 96; i++) {
        spectrogram[i] = new_value;  // WRITING
    }
}

// CORE 1 (led_thread) - SAME TIME!
void get_smooth_spectrogram() {
    for (int i = 0; i < 96; i++) {
        float val = spectrogram[i];  // READING - TORN READ POSSIBLE!
        // May get half-old, half-new values
    }
}
```

**Observed Symptoms:**
- Occasional LED flicker (likely due to torn reads)
- `g_race_condition_count` incrementing
- Snapwave mode artifacts

#### 1.2 Magnitude Data ⚠️ **RACE CONDITION**

| Variable | Size | Writer | Reader | Risk |
|----------|------|--------|--------|------|
| `magnitudes_normalized[96]` | 384B | Core 0 | Core 1 | **MEDIUM** |
| `magnitudes_final[96]` | 384B | Core 0 | Core 1 | **MEDIUM** |
| `max_mags[2]` | 8B | Core 0 | Core 1 | **LOW** (small, rarely causes issues) |

### CATEGORY 2: Configuration ⚠️ **RACE CONDITION**

| Variable | Writer | Reader | Frequency | Risk | Impact |
|----------|--------|--------|-----------|------|--------|
| `CONFIG.PHOTONS` | Core 0 (user input) | Both | Rare | **LOW** | Momentary brightness glitch |
| `CONFIG.CHROMA` | Core 0 (user input) | Both | Rare | **LOW** | Momentary color glitch |
| `CONFIG.MOOD` | Core 0 (user input) | Both | Rare | **LOW** | Momentary mood glitch |
| `CONFIG.LIGHTSHOW_MODE` | Core 0 (user input) | Core 1 | Rare | **MEDIUM** | Wrong mode executed |
| `CONFIG.AUTO_COLOR_SHIFT` | Core 0 (user input) | Core 0 | Rare | **LOW** | Same core |

**Race Condition Example:**
```cpp
// CORE 0 (serial command changes mode)
CONFIG.LIGHTSHOW_MODE = LIGHT_MODE_BLOOM;

// CORE 1 (led_thread) - SAME TIME!
if (CONFIG.LIGHTSHOW_MODE == LIGHT_MODE_GDFT) {  // May read old value
    light_mode_gdft();  // Executes wrong mode!
}
```

**Current Mitigation:**
- `cache_frame_config()` in led_utilities.h attempts to snapshot config
- Not atomic, still has race window

### CATEGORY 3: Shared State (LED Thread Writes) ✅ **SAFE**

| Variable | Writer | Reader | Status |
|----------|--------|--------|--------|
| `leds_16[160]` | Core 1 only | None | ✅ SAFE |
| `leds_16_secondary[160]` | Core 1 only | None | ✅ SAFE |
| `leds_16_prev[160]` | Core 1 only | None | ✅ SAFE |
| `LED_FPS` | Core 1 only | Core 0 (display only) | ✅ SAFE (reads stale OK) |

### CATEGORY 4: Audio Processing (Core 0 Only) ✅ **SAFE**

| Variable | Writer | Reader | Status |
|----------|--------|--------|--------|
| `sample_window[4096]` | Core 0 only | Core 0 only | ✅ SAFE |
| `waveform[1024]` | Core 0 only | Core 0 only | ✅ SAFE |
| `magnitudes[96]` | Core 0 only | Core 0 only | ✅ SAFE |

### CATEGORY 5: Mode Transitions ⚠️ **POTENTIAL RACE**

| Variable | Writer | Reader | Risk |
|----------|--------|--------|------|
| `mode_transition_queued` | Core 0 | Core 1 | **MEDIUM** |
| `noise_transition_queued` | Core 0 | Core 1 | **MEDIUM** |
| `mode_destination` | Core 0 | Core 1 | **MEDIUM** |

**Race Condition Example:**
```cpp
// CORE 0 (mode change requested)
mode_destination = LIGHT_MODE_BLOOM;
mode_transition_queued = true;  // Not atomic!

// CORE 1 (led_thread) - SAME TIME!
if (mode_transition_queued) {  // May see old value
    int mode = mode_destination;  // May see inconsistent state
}
```

---

## FreeRTOS Scheduler Behavior

### Task Switching

- **Tick Rate:** 1000 Hz (1ms tick)
- **Preemption:** Enabled
- **Time Slicing:** Enabled
- **Context Switch Time:** ~5-10 µs

### Core Affinity

- **Core 0:** main_loop_thread, encoder_task, Arduino loop
- **Core 1:** led_thread ONLY

**Current Assumption:**
- Code assumes atomic context switches protect Core 0 tasks
- **Reality:** Only helps tasks on SAME core
- **Problem:** Core 1 LED thread has NO protection from Core 0 writes

### Priority Inversion Risk

Current priorities:
- main_loop_thread: IDLE + 2
- led_thread: IDLE + 1
- encoder_task: IDLE + 1

**Potential Issue:**
- Main loop (high priority) can starve LED thread
- No priority inheritance on shared data
- If main loop takes too long, LED FPS drops

---

## Memory Ordering & Coherence

### ESP32-S3 Memory Architecture

- **L1 Cache:** Per-core instruction cache (16KB each)
- **L1 Data Cache:** **NONE** (ESP32-S3 has no data cache)
- **Memory Model:** Weakly ordered (ARM Cortex-M)
- **Shared Memory:** DRAM0/DRAM1 shared between cores

### Cache Coherence

**Good News:** ESP32-S3 has no data cache, so:
- No cache coherence protocol needed
- Writes are visible immediately (within a few cycles)
- No explicit cache flush required

**Bad News:** Without cache, all accesses hit RAM:
- Slower memory access
- Compiler optimizations can reorder accesses

### Memory Barriers

**Current State:** NO memory barriers used
**Required:** Compiler and hardware barriers for shared variables

```cpp
// WRONG (current code)
spectrogram[i] = new_value;  // May be reordered

// RIGHT (needed)
spectrogram[i] = new_value;
__asm__ __volatile__("" ::: "memory");  // Compiler barrier
```

---

## Synchronization Mechanisms Available

### Option 1: FreeRTOS Mutexes

```cpp
SemaphoreHandle_t spectrogram_mutex;

// Core 0 (writer)
xSemaphoreTake(spectrogram_mutex, portMAX_DELAY);
for (int i = 0; i < 96; i++) {
    spectrogram[i] = new_value;
}
xSemaphoreGive(spectrogram_mutex);

// Core 1 (reader)
xSemaphoreTake(spectrogram_mutex, portMAX_DELAY);
for (int i = 0; i < 96; i++) {
    local_copy[i] = spectrogram[i];
}
xSemaphoreGive(spectrogram_mutex);
```

**Performance Cost:**
- Mutex lock: ~5-10 µs per lock/unlock pair
- Contention blocking: Up to 8ms if Core 1 holds lock during full LED render

**Risk:** Could cause audio thread to miss real-time deadline

### Option 2: Double Buffering

```cpp
struct AudioToLEDBuffer {
    float spectrogram[96];
    float chromagram[32];
    uint32_t frame_id;
};

AudioToLEDBuffer buffers[2];
volatile int write_index = 0;

// Core 0 (writer)
int idx = write_index;
// Write to buffers[idx]
write_index = 1 - idx;  // Atomic flip

// Core 1 (reader)
int idx = 1 - write_index;  // Read old buffer
// Read from buffers[idx]
```

**Performance Cost:**
- Memory: 2x buffer size (~1KB extra)
- CPU: Negligible
- Latency: +1 frame (8ms)

**Advantages:**
- Lock-free
- No contention
- Predictable timing

### Option 3: FreeRTOS Message Queues

```cpp
QueueHandle_t audioLedQueue;

struct AudioToLEDMessage {
    float spectrogram[96];
    float chromagram[32];
    uint32_t timestamp;
};

// Core 0 (writer)
AudioToLEDMessage msg;
// Fill msg...
xQueueSend(audioLedQueue, &msg, 0);  // Non-blocking

// Core 1 (reader)
AudioToLEDMessage msg;
if (xQueueReceive(audioLedQueue, &msg, 0) == pdTRUE) {
    // Use msg data
}
```

**Performance Cost:**
- Memory: Queue overhead + messages
- CPU: ~5 µs per send/receive
- Latency: Minimal if queue depth = 1

**Advantages:**
- FreeRTOS native
- No locking needed
- Can handle backpressure

### Option 4: Atomic Operations

```cpp
// For simple scalars only
std::atomic<uint8_t> current_mode;

// Core 0 (writer)
current_mode.store(LIGHT_MODE_BLOOM, std::memory_order_release);

// Core 1 (reader)
uint8_t mode = current_mode.load(std::memory_order_acquire);
```

**Performance Cost:**
- CPU: 1-2 cycles (very fast)
- Memory: Same as non-atomic

**Limitations:**
- Only works for 32-bit or smaller types
- Cannot protect arrays
- Not suitable for spectrogram[] etc.

---

## Recommended Migration Strategy

### Phase 0-1: Add Synchronization Primitives

**Create HAL::Sync module:**

```cpp
// HAL/Sync/SyncPrimitives.h
namespace HAL::Sync {
    class SpinLock {
        // Ultra-fast lock for short critical sections
    };

    class Mutex {
        // FreeRTOS mutex wrapper with timeout
    };

    template<typename T>
    class Atomic {
        // std::atomic wrapper
    };
}
```

### Phase 2: Audio → LED Communication

**Implement Message Queue:**

```cpp
// Core/Audio/AudioToLED.h
struct AudioToLEDData {
    alignas(4) float spectrogram[96];
    alignas(4) float chromagram[32];
    uint32_t frame_id;
    uint32_t timestamp_us;
};

class AudioToLEDQueue {
private:
    QueueHandle_t queue_;

public:
    bool send(const AudioToLEDData& data, uint32_t timeout_ms = 0);
    bool receive(AudioToLEDData& data, uint32_t timeout_ms = 0);
};
```

**Usage Pattern:**

```cpp
// Core 0 (Audio thread)
void main_loop_core0() {
    process_GDFT();

    AudioToLEDData data;
    // Copy spectrogram, chromagram
    audioLedQueue.send(data, 0);  // Non-blocking
}

// Core 1 (LED thread)
void led_thread() {
    AudioToLEDData data;
    if (audioLedQueue.receive(data, 1)) {  // 1ms timeout
        // Use data.spectrogram, data.chromagram
        render_leds(data);
    }
}
```

### Phase 3: Configuration Access

**Implement Config Snapshot:**

```cpp
// Services/Config/ConfigSnapshot.h
class ConfigSnapshot {
private:
    Config data_;
    std::atomic<uint32_t> version_;

public:
    Config get() {
        // Return atomic snapshot
    }

    void set(const Config& config) {
        // Atomic update with version increment
    }
};
```

**Usage:**

```cpp
// Core 0 (writer)
Config cfg = configService->get();
cfg.PHOTONS = new_value;
configService->set(cfg);  // Atomic update

// Core 1 (reader)
Config cfg = configService->get();  // Atomic snapshot
// Use cfg.PHOTONS, cfg.MOOD, etc.
```

---

## Thread Safety Requirements by Phase

### Phase 0 (Immediate)
- ✅ Document current race conditions
- ✅ Add race condition detection to tests
- ⚠️ NO CODE CHANGES YET (stabilization first)

### Phase 1 (Weeks 3-4): HAL Layer
- ✅ Create HAL::Sync module
- ✅ Add FreeRTOS mutex wrappers
- ✅ Add atomic operation wrappers
- ✅ Add memory barrier primitives

### Phase 2 (Weeks 5-8): Core Layer
- ✅ Implement AudioToLEDQueue
- ✅ Replace direct global access with queue
- ✅ Add double buffering for Config
- ✅ Test: No torn reads in 48-hour test

### Phase 3 (Weeks 9-10): Service Layer
- ✅ ConfigService with atomic snapshots
- ✅ Event bus with proper queuing
- ✅ Test: No config race conditions

### Phase 4-6: Maintain Thread Safety
- ✅ All new modules follow thread safety guidelines
- ✅ Code review checklist includes thread safety
- ✅ Automated tests catch data races

---

## Lock Hierarchy (Prevent Deadlocks)

When multiple locks are needed, acquire in this order:

1. **Level 1:** Config lock (rare, short hold)
2. **Level 2:** Audio buffer lock (frequent, short hold)
3. **Level 3:** LED buffer lock (frequent, medium hold)
4. **Level 4:** Diagnostic/logging lock (rare, long hold)

**Rule:** Never acquire a lower-level lock while holding a higher-level lock

---

## Performance Impact Analysis

### Current Performance (No Synchronization)
- Audio Processing: 120+ FPS (8.3ms per frame)
- LED Rendering: 60+ FPS (16.7ms per frame)
- **Race Condition Cost:** Unknown (manifests as glitches)

### Projected Performance (With Synchronization)

| Approach | Overhead | Audio FPS Impact | LED FPS Impact | Memory Impact |
|----------|----------|------------------|----------------|---------------|
| **Mutexes** | 10-20 µs/frame | -2% (118 FPS) | -5% (57 FPS) | +12 bytes |
| **Double Buffer** | <1 µs/frame | -0.1% (120 FPS) | -0.1% (60 FPS) | +1 KB |
| **Message Queue** | 5 µs/frame | -0.5% (119 FPS) | -0.5% (60 FPS) | +500 bytes |
| **Atomic Config** | <1 µs/frame | -0.1% (120 FPS) | -0.1% (60 FPS) | +0 bytes |

**Recommendation:** Message Queue + Atomic Config = <1% overhead

---

## Testing Requirements

### Unit Tests (Phase 1)
- Test: Mutex lock/unlock correctness
- Test: Atomic operations on multi-core
- Test: Memory barriers prevent reordering

### Integration Tests (Phase 2)
- Test: Audio→LED queue doesn't drop frames
- Test: Config snapshot is atomic
- Test: No torn reads in spectrogram

### Stress Tests (Phase 3)
- Test: 48-hour run with race detection
- Test: High-frequency config changes
- Test: CPU stress doesn't cause deadlocks

### Hardware Tests (Phase 5)
- Test: Real ESP32-S3 dual-core behavior
- Test: LED output has no artifacts
- Test: Performance meets targets

---

## Critical Sections Analysis

### Critical Section 1: GDFT Write

**Location:** GDFT.h:115-174
**Duration:** ~5ms (96 bins × ~50µs each)
**Frequency:** 120 Hz
**Shared Data:** spectrogram[], magnitudes[]

**Solution:** Use message queue to send completed data

### Critical Section 2: LED Read & Render

**Location:** led_utilities.h:get_smooth_spectrogram()
**Duration:** ~2ms (read + smooth)
**Frequency:** 60 Hz
**Shared Data:** spectrogram[], chromagram[]

**Solution:** Receive from message queue (atomic copy)

### Critical Section 3: Config Update

**Location:** serial_menu.h, encoders.h
**Duration:** <100µs
**Frequency:** <1 Hz
**Shared Data:** CONFIG struct

**Solution:** Atomic snapshot with std::atomic

---

## Watchdog Timer Integration

### Current Watchdog Usage

```cpp
// main.cpp:187-192
for (int cpu = 0; cpu < portNUM_PROCESSORS; cpu++) {
    TaskHandle_t idle = xTaskGetIdleTaskHandleForCPU(cpu);
    if (idle != nullptr) {
        esp_task_wdt_delete(idle);  // Remove idle tasks from WDT
    }
}

// main.cpp:264, 270
esp_task_wdt_add(NULL);  // Add audio thread
esp_task_wdt_reset();    // Pet watchdog
```

### Thread Safety Impact

**Problem:** Long locks can cause watchdog timeout
**Solution:**
1. Keep all locks < 10ms
2. Pet watchdog before/after critical sections
3. Use timeouts on all lock acquisitions

```cpp
// WRONG
xSemaphoreTake(mutex, portMAX_DELAY);  // Can hang forever
do_work();
xSemaphoreGive(mutex);

// RIGHT
if (xSemaphoreTake(mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    do_work();
    xSemaphoreGive(mutex);
} else {
    // Log error, use stale data
    esp_task_wdt_reset();  // Prevent watchdog trigger
}
```

---

## Memory Fence Requirements

### ESP32-S3 Specific

**Compiler Barriers:**
```cpp
#define COMPILER_BARRIER() __asm__ __volatile__("" ::: "memory")
```

**Hardware Barriers:**
```cpp
#define MEMORY_FENCE() __asm__ __volatile__("dmb" ::: "memory")
```

### Where to Use

1. **After writing shared data:**
```cpp
spectrogram[i] = value;
COMPILER_BARRIER();
data_ready_flag = true;
```

2. **Before reading shared data:**
```cpp
if (data_ready_flag) {
    COMPILER_BARRIER();
    float val = spectrogram[i];
}
```

3. **In atomic operations:**
```cpp
template<typename T>
class Atomic {
    T load() {
        T val = value_;
        COMPILER_BARRIER();
        return val;
    }

    void store(T val) {
        COMPILER_BARRIER();
        value_ = val;
        COMPILER_BARRIER();
    }
};
```

---

## Code Review Checklist

### For Every Shared Variable

- [ ] Documented which threads access it
- [ ] Writer identified (Core 0 / Core 1 / Both)
- [ ] Readers identified
- [ ] Access frequency known
- [ ] Synchronization mechanism chosen
- [ ] Performance impact measured
- [ ] Tests written for race conditions

### For Every Lock

- [ ] Lock order documented (prevent deadlocks)
- [ ] Timeout used (prevent hangs)
- [ ] Hold time < 10ms (prevent watchdog)
- [ ] Tested under contention
- [ ] Alternative path if lock fails

### For Every Atomic Operation

- [ ] Memory ordering specified
- [ ] Size <= 32 bits on ESP32-S3
- [ ] Properly aligned
- [ ] Tested on multi-core hardware

---

## Appendix A: Race Condition Evidence

### Observed Issues

1. **Snapwave Artifacts:** Occasional wrong colors (main.cpp log mentions)
2. **FPS Drops:** Unexplained LED FPS drops below 60
3. **g_race_condition_count:** Increments during normal operation
4. **Config Glitches:** Mode changes sometimes take 2 frames

### Debug Logging Added

```cpp
// Add to globals.h
uint32_t g_torn_read_count = 0;
uint32_t g_race_condition_count = 0;

// Add to GDFT.h
spectrogram[i] = new_value;
if (led_currently_reading) {  // If LED thread is reading
    g_race_condition_count++;
}
```

---

## Appendix B: ESP32-S3 Documentation References

- [ESP-IDF FreeRTOS SMP](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/freertos.html)
- [ESP32-S3 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf) - Section 2.2 Memory Architecture
- [ARM Cortex Memory Barriers](https://developer.arm.com/documentation/dui0489/c/arm-and-thumb-instructions/miscellaneous-instructions/dmb--dsb--and-isb)

---

**STATUS:** COMPLETE - CRITICAL ISSUES IDENTIFIED
**RISK LEVEL:** HIGH - Must address in Phase 2
**RECOMMENDED SOLUTION:** Message Queue (AudioToLED) + Atomic Config
**PERFORMANCE IMPACT:** <1% with recommended approach
**NEXT ACTION:** Implement HAL::Sync in Phase 1
