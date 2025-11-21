#include "types.h"

// Implement the missing DSP functions that ESP-DSP would normally provide
void dsps_mulc_f32_ae32_fast(float* input, float* output, int len, float C, int step_in, int step_out) {
    for (int i = 0; i < len; i++) {
        output[i * step_out] = input[i * step_in] * C;
    }
}

void dsps_add_f32_ae32_fast(float* input1, float* input2, float* output, int len, int step) {
    for (int i = 0; i < len; i++) {
        output[i * step] = input1[i * step] + input2[i * step];
    }
}

void dsps_mul_f32_ae32_fast(float* input1, float* input2, float* output, int len, int step) {
    for (int i = 0; i < len; i++) {
        output[i * step] = input1[i * step] * input2[i * step];
    }
} 