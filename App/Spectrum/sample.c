#include "sample.h"

#include <stdlib.h> // malloc、free
#include "arm_math.h" // arm_cfft_f32、arm_cmplx_mag_f32
#include "arm_const_structs.h" // arm_cfft_sR_f32_len256

/*-----------------------------------------------------静态变量-----------------------------------------------------*/

static uint8_t dma_busy = 0;

/*-----------------------------------------------------静态函数-----------------------------------------------------*/

// 初始化所有采样相关的标志位
static void Clear_Flag() {
    dma_busy = 0;
    for (uint8_t i = 0; i < SPECTRUM_MAX_CACHE; i++) {
        if (spectrum_sample_arr[i] == NULL)
            break;
        spectrum_sample_arr[i]->sample_flag = Spectrum_Sample_Not;
        spectrum_sample_arr[i]->is_handle = 0;
    }
}

// 处理采样数据：FFT
// 计算：最大峰频率、绘图数据
static void Spectrum_Sample_Handle_Sub(Spectrum_Sample *sample) {
    for (int16_t i = SPECTRUM_SAMPLE_NUM - 1; i >= 0; i--) { // 倒序循环，不可以用unsigned，否则0-1=MAX，永远无法退出循环
        sample->data.fft[i * 2] = toVoltage((float) sample->data.raw[i]); // 使用转真实电压进行FFT
        sample->data.fft[i * 2 + 1] = 0; // 虚部 = 0
    }

    arm_cfft_f32(&arm_cfft_sR_f32_len, sample->data.fft, 0, 1);
    arm_cmplx_mag_f32(sample->data.fft, sample->data.fft, SPECTRUM_SAMPLE_NUM); // magnitude为模长；sample->data.fft前一半为正模长，后一半为负模长

    // 计算最大峰
    float FFT_max;
    uint32_t FFT_max_index;
    float freq = 0;
    arm_max_f32(sample->data.fft + 1, SPECTRUM_SAMPLE_NUM / 2 - 1, &FFT_max, &FFT_max_index); // 只考虑正模长
    FFT_max_index++; // 过滤掉直流分量
    if (FFT_max_index >= 2) {
        float sum = sample->data.fft[FFT_max_index - 2] +
                    sample->data.fft[FFT_max_index - 1] +
                    sample->data.fft[FFT_max_index] +
                    sample->data.fft[FFT_max_index + 1] +
                    sample->data.fft[FFT_max_index + 2];
        freq = (((float) (FFT_max_index - 2) * sample->data.fft[FFT_max_index - 2]) +
                ((float) (FFT_max_index - 1) * sample->data.fft[FFT_max_index - 1]) +
                ((float) (FFT_max_index) * sample->data.fft[FFT_max_index]) +
                ((float) (FFT_max_index + 2) * sample->data.fft[FFT_max_index + 2]) +
                ((float) (FFT_max_index + 1) * sample->data.fft[FFT_max_index + 1])) / sum;
        freq *= (spectrum_KHz_max[spectrum_KHz_max_select] * (1000.0f / (SPECTRUM_SAMPLE_NUM / 2.0f))); // 比例算法
    }
    sample->freq = freq; // 最大峰频率
    sample->max = sample->data.fft[FFT_max_index] / (SPECTRUM_SAMPLE_NUM / 2.0f); // 最大峰电压

    // 计算直流分量
    sample->data.fft[0] /= 2; // 除直流分量外，其他分量被镜像了一份；所以从数值上看，应该把直流分量/2
    sample->bias = sample->data.fft[0] / (SPECTRUM_SAMPLE_NUM / 2.0f);
}

/*-----------------------------------------------------接口函数-----------------------------------------------------*/

void Spectrum_Sample_Init(void) {
    for (uint8_t i = 0; i < SPECTRUM_MAX_CACHE; i++) {
        spectrum_sample_arr[i] = (Spectrum_Sample *) malloc(sizeof(Spectrum_Sample));
        if (spectrum_sample_arr[i] == NULL)
            break;
    }

    Spectrum_Sample_Refresh_Sample_Rate();
}

void Spectrum_Sample_DeInit(void) {
    HAL_ADC_Stop_DMA(&hadc1);
    HAL_TIM_Base_Stop(&htim3);

    // 释放变量空间
    for (uint8_t i = 0; i < SPECTRUM_MAX_CACHE; i++) {
        if (spectrum_sample_arr[i] == NULL)
            break;
        free(spectrum_sample_arr[i]);
        spectrum_sample_arr[i] = NULL;
    }
}

// 根据时间刻度选择设置采样率
void Spectrum_Sample_Refresh_Sample_Rate(void) {
    HAL_TIM_Base_Stop(&SPECTRUM_htim);
    HAL_ADC_Stop_DMA(&SPECTRUM_hadc);

    // 根据采样定理，采样频率必须是被采样信号最高频率的2倍
    Set_TIM_Freq(&SPECTRUM_htim, spectrum_KHz_max[spectrum_KHz_max_select] * (1000.0f * 2.0f));

    Clear_Flag();
    Spectrum_Sample_Try_Start_New_ADC();
}

void Spectrum_Sample_Try_Process(void) {
    for (uint8_t i = 0; i < SPECTRUM_MAX_CACHE; i++) {
        if (spectrum_sample_arr[i] == NULL)
            break;
        if (spectrum_sample_arr[i]->sample_flag == Spectrum_Sample_Finished && spectrum_sample_arr[i]->is_handle == 0) {
            Spectrum_Sample_Handle_Sub(spectrum_sample_arr[i]);
            spectrum_sample_arr[i]->is_handle = 1;
        }
    }
}

// 在以下几个位置被调用：
// DMA全传输完成中断
// UI绘图消耗掉一组数据
// 重新设置采样率
void Spectrum_Sample_Try_Start_New_ADC(void) {
    static uint8_t busy;
    if (busy)
        return;
    busy = 1;

    if (!dma_busy) {
        for (uint8_t i = 0; i < SPECTRUM_MAX_CACHE; i++) {
            if (spectrum_sample_arr[i] == NULL)
                break;
            if (spectrum_sample_arr[i]->sample_flag == Spectrum_Sample_Not) {
                dma_busy = 1;
                spectrum_sample_arr[i]->sample_flag = Spectrum_Sample_Doing;
                HAL_ADC_Start_DMA(&SPECTRUM_hadc, (uint32_t *) spectrum_sample_arr[i]->data.raw, SPECTRUM_SAMPLE_NUM);
                HAL_TIM_Base_Start(&SPECTRUM_htim);
                break;
            }
        }
    }

    busy = 0;
}

/*-----------------------------------------------------回调函数-----------------------------------------------------*/

void Spectrum_Sample_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
    UNUSED(hadc);
    HAL_TIM_Base_Stop(&SPECTRUM_htim);
    HAL_ADC_Stop_DMA(&SPECTRUM_hadc);
    dma_busy = 0;
    for (uint8_t i = 0; i < SPECTRUM_MAX_CACHE; i++) {
        if (spectrum_sample_arr[i]->sample_flag == Spectrum_Sample_Doing) {
            spectrum_sample_arr[i]->sample_flag = Spectrum_Sample_Finished;
            break;
        }
    }
    Spectrum_Sample_Try_Start_New_ADC();
}
