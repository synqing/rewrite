/**
 * @file audio_input_factory.h  
 * @brief Simple IM69D130 support for Shield2Go
 */

#ifndef AUDIO_INPUT_FACTORY_H
#define AUDIO_INPUT_FACTORY_H

#include <driver/i2s.h>
#include "constants.h"
#include "globals.h"

// Forward declaration
class AudioInputBase;
extern AudioInputBase* g_audioInput;

struct AudioStats {
    float rms_level = 0.0f;
    float peak_level = 0.0f; 
    float db_level = 0.0f;
    float spl_db = 0.0f;
    bool spl_valid = false;
    uint32_t clipping_count = 0;
    uint32_t dropout_count = 0;
};

class AudioInputBase {
public:
    virtual ~AudioInputBase() = default;
    virtual esp_err_t configure() = 0;
    virtual esp_err_t readSamples(void* buffer, size_t buffer_size, size_t* bytes_read) = 0;
    virtual void processBuffer(void* i2s_buffer, float* float_buffer, size_t sample_count) = 0;
    virtual bool supportsSPL() const { return false; }
    virtual float getSPL() const { return -1.0f; }
    virtual const char* getMicrophoneName() const = 0;
    virtual bool isHealthy() const { return true; }
    virtual const AudioStats& getStats() const { return stats; }
    virtual void updateSampleWindow(const float* samples, size_t count) {}
    virtual esp_err_t recover() { return ESP_OK; }
    
protected:
    AudioStats stats;
};

#if MICROPHONE_TYPE == MIC_TYPE_IM69D130

class IM69D130Input : public AudioInputBase {
private:
    static constexpr uint8_t SHIFT_BITS = 12;
    static constexpr float NORMALIZE_FACTOR = 524288.0f; // 2^19
    static constexpr float SPL_REF_LEVEL = 0.0631f;  // -26 dBFS
    static constexpr float SPL_REF_DB = 94.0f;
    
    // DC blocking filter
    float dc_prev_input = 0.0f;
    float dc_prev_output = 0.0f;
    static constexpr float DC_ALPHA = 0.995f;
    
public:
    esp_err_t configure() override {
        i2s_config_t i2s_config = {
            .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
            .sample_rate = DEFAULT_SAMPLE_RATE,
            .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,  // 32-bit for ADAU7002
            .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
            .dma_buf_count = 8,
            .dma_buf_len = 128,
            .use_apll = true,
            .tx_desc_auto_clear = false,
            .fixed_mclk = 0
        };
        
        esp_err_t ret = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
        if (ret != ESP_OK) return ret;
        
        i2s_pin_config_t pin_config = {
            .mck_io_num = I2S_PIN_NO_CHANGE,
            .bck_io_num = I2S_BCLK_PIN,
            .ws_io_num = I2S_WS_PIN,
            .data_out_num = I2S_PIN_NO_CHANGE,
            .data_in_num = I2S_DATA_PIN
        };
        
        ret = i2s_set_pin(I2S_NUM_0, &pin_config);
        if (ret != ESP_OK) return ret;
        
        // Critical delay for ADAU7002 initialization
        vTaskDelay(pdMS_TO_TICKS(100));
        
        return ESP_OK;
    }
    
    esp_err_t readSamples(void* buffer, size_t buffer_size, size_t* bytes_read) override {
        return i2s_read(I2S_NUM_0, buffer, buffer_size, bytes_read, portMAX_DELAY);
    }
    
    void processBuffer(void* i2s_buffer, float* float_buffer, size_t sample_count) override {
        int32_t* samples_32 = (int32_t*)i2s_buffer;
        
        for (size_t i = 0; i < sample_count; i++) {
            // Extract 20-bit data from 32-bit frame
            int32_t raw_sample = samples_32[i] >> SHIFT_BITS;
            
            // Normalize to [-1.0, 1.0]
            float sample = raw_sample / NORMALIZE_FACTOR;
            
            // Apply DC blocking
            float filtered = sample - dc_prev_input + DC_ALPHA * dc_prev_output;
            dc_prev_input = sample;
            dc_prev_output = filtered;
            
            float_buffer[i] = filtered;
        }
        
        // Update SPL
        updateSPL(float_buffer, sample_count);
    }
    
    bool supportsSPL() const override { return true; }
    
    float getSPL() const override {
        return stats.spl_valid ? stats.spl_db : -1.0f;
    }
    
    const char* getMicrophoneName() const override {
        return "IM69D130 + ADAU7002";
    }
    
private:
    void updateSPL(const float* samples, size_t count) {
        float sum_squares = 0.0f;
        for (size_t i = 0; i < count; i++) {
            sum_squares += samples[i] * samples[i];
        }
        
        stats.rms_level = sqrtf(sum_squares / count);
        
        if (stats.rms_level > 0.0000001f) {
            stats.spl_db = SPL_REF_DB + 20.0f * log10f(stats.rms_level / SPL_REF_LEVEL);
            stats.spl_db = constrain(stats.spl_db, 30.0f, 130.0f);
            stats.spl_valid = true;
        } else {
            stats.spl_db = 30.0f;
            stats.spl_valid = true;
        }
    }
};

#else

class SPH0645Input : public AudioInputBase {
private:
    static constexpr float NORMALIZE_FACTOR = 32768.0f;
    
public:
    esp_err_t configure() override {
        i2s_config_t i2s_config = {
            .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
            .sample_rate = DEFAULT_SAMPLE_RATE,
            .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
            .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
            .dma_buf_count = 8,
            .dma_buf_len = 128,
            .use_apll = false,
            .tx_desc_auto_clear = false,
            .fixed_mclk = 0
        };
        
        esp_err_t ret = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
        if (ret != ESP_OK) return ret;
        
        i2s_pin_config_t pin_config = {
            .mck_io_num = I2S_PIN_NO_CHANGE,
            .bck_io_num = I2S_BCLK_PIN,
            .ws_io_num = I2S_WS_PIN,
            .data_out_num = I2S_PIN_NO_CHANGE,
            .data_in_num = I2S_DATA_PIN
        };
        
        return i2s_set_pin(I2S_NUM_0, &pin_config);
    }
    
    esp_err_t readSamples(void* buffer, size_t buffer_size, size_t* bytes_read) override {
        return i2s_read(I2S_NUM_0, buffer, buffer_size, bytes_read, portMAX_DELAY);
    }
    
    void processBuffer(void* i2s_buffer, float* float_buffer, size_t sample_count) override {
        int16_t* samples_16 = (int16_t*)i2s_buffer;
        
        for (size_t i = 0; i < sample_count; i++) {
            float_buffer[i] = samples_16[i] / NORMALIZE_FACTOR;
        }
    }
    
    const char* getMicrophoneName() const override {
        return "SPH0645 Analog";
    }
};

#endif

class AudioInputFactory {
public:
    static AudioInputBase* createAudioInput() {
        #if MICROPHONE_TYPE == MIC_TYPE_IM69D130
            return new IM69D130Input();
        #else
            return new SPH0645Input();
        #endif
    }
    
    static esp_err_t initializeGlobalAudioInput() {
        if (g_audioInput) return ESP_OK;
        
        g_audioInput = createAudioInput();
        if (!g_audioInput) return ESP_ERR_NO_MEM;
        
        esp_err_t ret = g_audioInput->configure();
        if (ret != ESP_OK) {
            delete g_audioInput;
            g_audioInput = nullptr;
        }
        
        return ret;
    }
};

// Global instance
AudioInputBase* g_audioInput = nullptr;

#endif // AUDIO_INPUT_FACTORY_H