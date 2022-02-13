#include "os.h"

#include "../App/Scope/app.h"
#include "../App/Spectrum/app.h"

#define APP_SIZE 2

App *app[APP_SIZE];
uint8_t app_p = 0;

static void ShowLogo(void) {
    OLED_Clear();
    OLED_ShowString(12, 49, "Code by WonderBoy", 12, 1);
    OLED_ShowString(45, 73, "V1.0.1", 8, 1);
    OLED_Refresh();
}

void OS_Init(void) {
    HAL_ADCEx_Calibration_Start(&hadc1); // 校准ADC
    HAL_TIM_PWM_Start(&htim16, TIM_CHANNEL_1); // Bias = 1.1V

    OLED_Init();
    Key_Init();

    ShowLogo();
    HAL_Delay(2000);

    app[0] = &Scope;
    app[1] = &Spectrum;
    app[app_p]->Init();

    Buzzer_Beep(1);
}

void OS_Loop(void) {
    if (app[app_p]->Loop()) {
        app[app_p]->DeInit();
        app_p = (app_p + 1) % APP_SIZE;
        app[app_p]->Init();
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc){
    app[app_p]->ADC_ConvCpltCallback(hadc);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    Buzzeer_TIM_PeriodElapsedCallback(htim);
    Key_TIM_PeriodElapsedCallback(htim);
}

void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin) {
    Key_GPIO_EXTI_Callback(GPIO_Pin);
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin) {
    Key_GPIO_EXTI_Callback(GPIO_Pin);
}
