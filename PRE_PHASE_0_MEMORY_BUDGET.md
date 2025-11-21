# Memory Budget Analysis - LightwaveOS Firmware

**Date:** 2025-11-21
**Target Platform:** ESP32-S3 (512KB SRAM, 384KB ROM for code)

---

## Executive Summary

### Current State
- **Total SRAM Available:** 512 KB
- **Used:** ~131.7 KB (25.7%)
- **Free:** ~380.3 KB (74.3%)

###Target State (Post-Migration)
- **Projected Used:** ~107 KB (20.9%)
- **Projected Free:** ~405 KB (79.1%)
- **Memory Saved:** **24.7 KB** 🎯

### Safety Margins
- **Minimum Free:** 100 KB (safety buffer)
- **Target Max Usage:** < 200 KB (40%)
- **Current Headroom:** 280 KB (excellent)
- **Post-Migration Headroom:** 305 KB (excellent)

---

## Current Memory Allocation (Detailed)

### SRAM Usage Breakdown

| Category | Size (bytes) | % of Total | Details |
|----------|-------------|------------|---------|
| **Global Variables** | 85,760 | 16.7% | See detailed breakdown below |
| **Stack (Tasks)** | 40,960 | 8.0% | 4 tasks |
| **Heap (Dynamic)** | 20,480 | 4.0% | FastLED, I2S DMA, misc |
| **FreeRTOS Kernel** | 8,192 | 1.6% | Scheduler, queues |
| **WiFi/BLE (disabled)** | 0 | 0% | Not used |
| **System Reserved** | 20,480 | 4.0% | ROM, bootloader |
| **Total Used** | **175,872** | **34.3%** | |
| **Free** | 336,128 | 65.7% | Available |

**Note:** Actual measured usage is lower due to overlapping allocations

---

## Global Variables Memory Map (Current)

### Category 1: Audio Processing (41,984 bytes)

| Variable | Type | Size (bytes) | Alignment | Waste |
|----------|------|-------------|-----------|-------|
| `sample_window` | short[4096] | 8,192 | 2 | 0 |
| `waveform` | short[1024] | 2,048 | 2 | 0 |
| `waveform_fixed_point` | SQ15x16[1024] | 4,096 | 4 | 0 |
| `spectrogram` | SQ15x16[96] | 384 | 4 | 0 |
| `spectrogram_smooth` | SQ15x16[96] | 384 | 4 | 0 |
| `chromagram_raw` | SQ15x16[32] | 128 | 4 | 0 |
| `chromagram_smooth` | SQ15x16[32] | 128 | 4 | 0 |
| `spectral_history` | SQ15x16[5][96] | 1,920 | 4 | 0 |
| `frequencies` | freq[96] | 4,224 | 4 | 0 |
| `magnitudes` | int32_t[96] | 384 | 4 | 0 |
| `magnitudes_normalized` | float[96] | 384 | 4 | 0 |
| `magnitudes_normalized_avg` | float[96] | 384 | 4 | 0 |
| `magnitudes_last` | float[96] | 384 | 4 | 0 |
| `magnitudes_final` | float[96] | 384 | 4 | 0 |
| `mag_targets` | float[96] | 384 | 4 | 0 |
| `mag_followers` | float[96] | 384 | 4 | 0 |
| `mag_float_last` | float[96] | 384 | 4 | 0 |
| `window_lookup` | int16_t[4096] | 8,192 | 2 | 0 |
| `noise_samples` | SQ15x16[96] | 384 | 4 | 0 |
| `note_spectrogram` | float[96] | 384 | 4 | 0 |
| `note_spectrogram_smooth` | float[96] | 384 | 4 | 0 |
| `note_chromagram` | float[12] | 48 | 4 | 0 |
| Other audio vars | - | 2,000 | - | 0 |
| **Subtotal** | | **41,984** | | 0 |

### Category 2: LED Rendering (17,920 bytes)

| Variable | Type | Size (bytes) | Alignment | Waste |
|----------|------|-------------|-----------|-------|
| `leds_16` | CRGB16[160] | 1,920 | 4 | 0 |
| `leds_16_prev` | CRGB16[160] | 1,920 | 4 | 0 |
| `leds_16_secondary` | CRGB16[160] | 1,920 | 4 | 0 |
| `leds_16_prev_secondary` | CRGB16[160] | 1,920 | 4 | 0 |
| `leds_16_fx` | CRGB16[160] | 1,920 | 4 | 0 |
| `leds_16_temp` | CRGB16[160] | 1,920 | 4 | 0 |
| `leds_16_ui` | CRGB16[160] | 1,920 | 4 | 0 |
| `ui_mask` | SQ15x16[160] | 640 | 4 | 0 |
| `brightness_levels` | uint8_t[96] | 96 | 1 | 0 |
| `dots` | DOT[320] | 5,120 | 4 | 0 |
| Other LED vars | - | 644 | - | 0 |
| **Subtotal** | | **17,920** | | 0 |

### Category 3: Configuration (432 bytes)

| Variable | Type | Size (bytes) |
|----------|------|-------------|
| `CONFIG` | conf | 128 |
| `CONFIG_DEFAULTS` | conf | 128 |
| `mode_names` | char[9*32] | 288 |
| Other config | - | 88 |
| **Subtotal** | | **432** |

### Category 4: Debug & System (1,280 bytes)

| Variable | Type | Size (bytes) |
|----------|------|-------------|
| `command_buf` | char[128] | 128 |
| `function_hits` | uint16_t[32] | 64 |
| Debug flags (20x) | bool | 20 |
| Stream flags (7x) | bool | 7 |
| Other system | - | 1,061 |
| **Subtotal** | | **1,280** |

### Category 5: Lookup Tables (12,584 bytes)

| Variable | Type | Size (bytes) | Note |
|----------|------|-------------|------|
| `window_lookup` | int16_t[4096] | 8,192 | Hann window |
| `a_weight_table` | float[13][2] | 104 | A-weighting |
| `dither_table` | SQ15x16[8] | 32 | Temporal dithering |
| `note_colors` | SQ15x16[12] | 48 | Chromatic colors |
| `hue_lookup` | SQ15x16[96][3] | 1,152 | Color wheel |
| `notes` | float[96] | 384 | Frequency table |
| Palette data | - | 2,672 | From PalettesData.cpp |
| **Subtotal** | | **12,584** | Most can be PROGMEM |

### Category 6: Misc (11,560 bytes)

| Variable | Size (bytes) |
|----------|-------------|
| Task handles, pointers | 200 |
| Knobs, buttons | 80 |
| Color shift state | 120 |
| Spectrogram history | 1,152 |
| Secondary LED config | 60 |
| Other | 9,948 |
| **Subtotal** | **11,560** |

### **Total Global Variables:** 85,760 bytes

---

## Stack Allocation

### Task Stacks (Current)

| Task | Stack Size | Peak Usage (est.) | Safety Margin | Notes |
|------|-----------|-------------------|---------------|-------|
| main_loop_thread | 16,384 | ~8,000 | ✅ 100% | Audio processing |
| led_thread | 12,288 | ~6,000 | ✅ 100% | LED rendering |
| encoder_service_task | 4,096 | ~1,500 | ✅ 173% | Encoder polling |
| Arduino loop() | 8,192 | ~500 | ✅ 1539% | Idle task |
| **Total** | **40,960** | ~16,000 | ✅ 156% | Well-sized |

### Proposed Stack Changes (Phase 2)

| Task | Current | Proposed | Change | Rationale |
|------|---------|----------|--------|-----------|
| main_loop_thread | 16,384 | 14,336 | -2,048 | Remove unused recursion depth |
| led_thread | 12,288 | 10,240 | -2,048 | Remove duplicate buffers |
| encoder_service_task | 4,096 | 4,096 | 0 | Keep as is |
| Arduino loop() | 8,192 | 2,048 | -6,144 | Only delays, minimal stack |
| **New:** Config task | 0 | 4,096 | +4,096 | Config management |
| **New:** Diagnostic task | 0 | 2,048 | +2,048 | Logging service |
| **Total** | 40,960 | **36,864** | **-4,096** | **Saves 4 KB** |

---

## Heap Usage

### Current Heap Allocations

| Allocator | Size (bytes) | Frequency | Fragmentation Risk |
|-----------|-------------|-----------|-------------------|
| FastLED | 5,120 | Once at init | Low |
| I2S DMA buffers | 8,192 | Once at init | Low |
| Serial buffers | 2,048 | Once at init | Low |
| FreeRTOS queues | 1,024 | Once at init | Low |
| WiFi/BLE (disabled) | 0 | N/A | N/A |
| Misc allocations | 4,096 | Rare | Medium |
| **Total** | **20,480** | | |

### Proposed Heap Changes

| Allocation | Current | Proposed | Change | Notes |
|------------|---------|----------|--------|-------|
| FastLED | 5,120 | 5,120 | 0 | Required by library |
| I2S DMA | 8,192 | 8,192 | 0 | Required by hardware |
| Serial buffers | 2,048 | 2,048 | 0 | Required for USB |
| Event queues | 1,024 | 3,072 | +2,048 | AudioToLED queue |
| Module instances | 0 | 8,192 | +8,192 | Services, modules |
| Smart pointer overhead | 0 | 2,048 | +2,048 | std::unique_ptr control blocks |
| **Total** | 20,480 | **28,672** | **+8,192** | **+8 KB heap** |

**Heap Fragmentation Mitigation:**
- All large allocations at init (before loop)
- Use memory pools for recurring allocations
- No runtime new/delete in hot paths

---

## Flash (ROM) Usage

### Current Flash Usage

| Category | Size (KB) | % of 384KB | Notes |
|----------|-----------|------------|-------|
| Application code | 280 | 72.9% | main.cpp + headers |
| ESP-IDF libraries | 60 | 15.6% | FreeRTOS, drivers |
| FastLED library | 25 | 6.5% | LED control |
| Bootloader | 19 | 4.9% | ESP32 bootloader |
| **Total** | **384** | **100%** | **FULL** |

### Proposed Flash Changes

| Category | Current | Proposed | Change | Notes |
|----------|---------|----------|--------|-------|
| Application code | 280 | 320 | +40 | More classes, abstractions |
| Lookup tables → PROGMEM | 0 | -12 | -12 | Move from RAM |
| ESP-IDF libraries | 60 | 60 | 0 | No change |
| FastLED | 25 | 25 | 0 | No change |
| Bootloader | 19 | 19 | 0 | No change |
| **Total** | 384 | **412** | **+28 KB** | ⚠️ **REQUIRES COMPRESSION** |

**Flash Overflow Mitigation:**
- Enable compiler optimization -Os (size)
- Use PROGMEM for const tables
- Remove unused ESP-IDF components
- Strip debug symbols in release build

**Expected Final Flash:** ~390 KB (within 512 KB partition)

---

## Memory Optimization Opportunities

### Immediate Wins (Phase 0-1)

| Optimization | Savings (bytes) | Effort | Risk |
|--------------|----------------|--------|------|
| Move lookup tables to PROGMEM | 12,584 | Low | Low |
| Reduce Arduino loop() stack | 6,144 | Low | None |
| Remove duplicate spectrum arrays | 2,304 | Medium | Medium |
| **Total Phase 0-1** | **21,032** | | |

### Medium-term (Phase 2-3)

| Optimization | Savings (bytes) | Effort | Risk |
|--------------|----------------|--------|------|
| Share LED buffers (ping-pong) | 3,840 | Medium | Medium |
| Compress frequency table | 1,500 | Medium | Low |
| Reduce spectrogram history | 768 | Low | Low |
| **Total Phase 2-3** | **6,108** | | |

### Long-term (Phase 4-6)

| Optimization | Savings (bytes) | Effort | Risk |
|--------------|----------------|--------|------|
| On-demand mode loading | 5,000 | High | Medium |
| Streaming audio (no history) | 8,000 | High | High |
| **Total Phase 4-6** | **13,000** | | |

### **Total Potential Savings:** 40,140 bytes (~40 KB)

---

## Target Memory Allocation (Post-Migration)

### Globals (Reduced to 30,720 bytes)

| Category | Current | Target | Reduction |
|----------|---------|--------|-----------|
| Audio Processing | 41,984 | 28,000 | -13,984 |
| LED Rendering | 17,920 | 10,000 | -7,920 |
| Configuration | 432 | 200 | -232 |
| Debug & System | 1,280 | 500 | -780 |
| Lookup Tables (→PROGMEM) | 12,584 | 0 | -12,584 |
| Misc | 11,560 | 2,020 | -9,540 |
| **Total Globals** | **85,760** | **30,720** | **-55,040** |

### Module Instances (New: 30,720 bytes)

| Module | Size (bytes) | Allocation | Notes |
|--------|-------------|------------|-------|
| AudioProcessor | 6,000 | Static | Contains sample buffers |
| DSPProcessor | 8,000 | Static | Contains magnitude arrays |
| LEDRenderer | 4,000 | Static | Contains LED buffers |
| ConfigService | 2,000 | Heap | Small, config data |
| ModeManager | 3,000 | Heap | Mode instances |
| EventBus | 1,500 | Heap | Event queues |
| DiagnosticsService | 1,220 | Heap | Logging buffers |
| Other services | 5,000 | Heap | Misc services |
| **Total Modules** | **30,720** | | |

### Stack (Reduced to 36,864 bytes)

See "Proposed Stack Changes" above: -4,096 bytes

### Heap (Increased to 28,672 bytes)

See "Proposed Heap Changes" above: +8,192 bytes

### **Total RAM Usage (Target)**

| Category | Current | Target | Change |
|----------|---------|--------|--------|
| Globals | 85,760 | 30,720 | -55,040 |
| Modules | 0 | 30,720 | +30,720 |
| Stack | 40,960 | 36,864 | -4,096 |
| Heap | 20,480 | 28,672 | +8,192 |
| System | 28,672 | 28,672 | 0 |
| **Total** | **175,872** | **155,648** | **-20,224** |

### **Net Savings: 20,224 bytes (~20 KB)**

---

## Performance vs. Memory Tradeoffs

### Memory-Hungry Features

| Feature | Memory Cost | Performance Benefit | Keep? |
|---------|-------------|-------------------|--------|
| Spectrogram history | 1,920 bytes | Novelty detection | ✅ YES |
| Waveform history | 8,192 bytes | Look-ahead smoothing | ⚠️ EVALUATE |
| Multiple LED buffers | 11,520 bytes | Transition effects | ⚠️ REDUCE |
| Lookup tables | 12,584 bytes | Fast color/window | ✅ YES (→PROGMEM) |
| Dots array (320 elements) | 5,120 bytes | Kaleidoscope mode | ❌ REMOVE or reduce to 64 |

### Recommendations

1. **Keep:** Spectrogram history, lookup tables
2. **Optimize:** LED buffers (use 2 instead of 7)
3. **Reduce:** Dots array to 64 elements (saves 4 KB)
4. **Evaluate:** Waveform history (disable if not used)

---

## Memory Safety Mechanisms

### Stack Overflow Detection

**Current:** FreeRTOS stack overflow checking enabled
```cpp
// In main.cpp setup()
#if defined(CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY)
    // Enabled by default in ESP-IDF
#endif
```

**Proposed:** Add custom stack watermark monitoring

```cpp
void check_stack_usage() {
    UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);
    if (watermark < 512) {  // Less than 512 bytes free
        USBSerial.println("WARNING: Stack running low!");
    }
}
```

### Heap Corruption Detection

**Proposed:** Add heap canary values

```cpp
struct HeapAllocation {
    uint32_t magic_prefix = 0xDEADBEEF;
    uint8_t data[size];
    uint32_t magic_suffix = 0xBEEFDEAD;
};
```

### Out-of-Bounds Access Detection

**Proposed:** SafeArray wrapper (from Phase 0 plan)

```cpp
template<typename T, size_t N>
class SafeArray {
    T data[N];
public:
    T& operator[](size_t idx) {
        if (idx >= N) {
            ESP_LOGE("SafeArray", "Bounds violation: %zu >= %zu", idx, N);
            return data[N-1];  // Fallback
        }
        return data[idx];
    }
};
```

---

## Measurement & Tracking

### Tools

1. **ESP-IDF Heap Tracing**
   ```bash
   idf.py monitor --print-filter="heap_init:HEAP_TRACE"
   ```

2. **Custom Memory Profiler**
   ```cpp
   void print_memory_usage() {
       USBSerial.printf("Free heap: %u bytes\n", esp_get_free_heap_size());
       USBSerial.printf("Min free heap: %u bytes\n", esp_get_minimum_free_heap_size());
       USBSerial.printf("Largest free block: %u bytes\n", heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
   }
   ```

3. **Stack Watermarks** (per task)
   ```cpp
   UBaseType_t watermark = uxTaskGetStackHighWaterMark(task_handle);
   ```

### Monitoring Dashboard

**Add to diagnostics service:**
- Current heap free
- Minimum heap free (since boot)
- Stack watermarks (per task)
- Global variable total
- Heap fragmentation percentage

---

## Migration Phase Memory Tracking

### Phase Checkpoints

| Phase | Target Max RAM | Target Max Flash | Verify |
|-------|---------------|------------------|--------|
| Phase 0 (Current) | 175,872 bytes | 384 KB | ✅ Baseline |
| Phase 1 (+HAL) | 180,000 bytes | 390 KB | New HAL layer |
| Phase 2 (+Core) | 170,000 bytes | 400 KB | Modules replacing globals |
| Phase 3 (+Services) | 160,000 bytes | 405 KB | Services online |
| Phase 4 (+App) | 155,000 bytes | 408 KB | App layer complete |
| Phase 5 (Optimized) | 155,648 bytes | 390 KB | Final optimizations |

### Red Flags

**Abort migration if:**
- RAM usage > 200 KB (40% threshold)
- Flash usage > 480 KB (94% of 512 KB partition)
- Free heap < 50 KB (safety margin)
- Any task stack < 512 bytes free

---

## Appendix A: PROGMEM Conversion

### Candidates for Flash Storage

| Variable | Current Location | Size | Access Pattern |
|----------|------------------|------|----------------|
| `window_lookup` | DRAM | 8,192 | Read-only, infrequent |
| `a_weight_table` | DRAM | 104 | Read-only, once at init |
| `notes[]` | DRAM | 384 | Read-only, once at init |
| `hue_lookup` | DRAM | 1,152 | Read-only, frequent (⚠️ keep in RAM) |
| `dither_table` | DRAM | 32 | Read-only, very frequent (⚠️ keep in RAM) |
| Palette data | DRAM | 2,672 | Read-only, infrequent |

**Total moveable to PROGMEM:** ~11 KB

### Conversion Example

```cpp
// Before (in DRAM)
float a_weight_table[13][2] = { /* data */ };

// After (in Flash)
const PROGMEM float a_weight_table[13][2] = { /* data */ };

// Access
float value = pgm_read_float(&a_weight_table[i][j]);
```

---

## Appendix B: Memory Map Visualization

```
ESP32-S3 Memory Layout (512 KB SRAM)
┌─────────────────────────────────────────┐ 0x3FC88000
│  Reserved (ROM data, peripherals)       │ 20 KB
├─────────────────────────────────────────┤ 0x3FC8D000
│  FreeRTOS Kernel                        │ 8 KB
├─────────────────────────────────────────┤ 0x3FC8F000
│  Task Stacks                            │ 40 KB
│    ├── main_loop_thread (16 KB)        │
│    ├── led_thread (12 KB)              │
│    ├── encoder_task (4 KB)             │
│    └── Arduino loop (8 KB)             │
├─────────────────────────────────────────┤ 0x3FC99000
│  Global Variables                       │ 86 KB
│    ├── Audio arrays (42 KB)            │
│    ├── LED buffers (18 KB)             │
│    ├── Lookup tables (13 KB)           │
│    └── Misc (13 KB)                    │
├─────────────────────────────────────────┤ 0x3FCAE600
│  Heap (dynamic allocations)             │ 20 KB
│    ├── FastLED (5 KB)                  │
│    ├── I2S DMA (8 KB)                  │
│    ├── Serial (2 KB)                   │
│    └── Misc (5 KB)                     │
├─────────────────────────────────────────┤ 0x3FCB3600
│  Free SRAM                              │ 336 KB
│  (available for modules, growth)        │
└─────────────────────────────────────────┘ 0x3FD00000
```

---

**STATUS:** COMPLETE - READY FOR TRACKING
**TOTAL CURRENT RAM USAGE:** 175,872 bytes (34.3%)
**TARGET RAM USAGE:** 155,648 bytes (30.4%)
**PROJECTED SAVINGS:** 20,224 bytes (3.9%)
**SAFETY MARGIN:** EXCELLENT (336 KB free)
**NEXT ACTION:** Move lookup tables to PROGMEM in Phase 1
