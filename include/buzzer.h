#ifndef BUZZER_H
#define BUZZER_H

#define BUZZ_TIMER                   TIM4
#define BUZZ_TIMER_CLK_ENABLE        __HAL_RCC_TIM4_CLK_ENABLE
#define BUZZ_TIMER_IRQ               TIM4_IRQn

#define BUZZ_PIN_IRQ                 EXTI15_10_IRQn


// Commands sent over queue by ISRs
#define BUZZ_CMD_PIN_CHANGE     1
#define BUZZ_CMD_65MS_TIMEOUT   2
#define BUZZ_CMD_125MS_TIMEOUT  3


#define P_APP_SEQUENCE_UI     (P_APP_SEQUENCE_n_ON | P2_ARR(1))
#define P_APP_SEQUENCE_WARN   (P_APP_SEQUENCE_n_ON | P2_ARR(2))
#define P_APP_SEQUENCE_ERROR  (P_APP_SEQUENCE_n_ON | P2_ARR(3))
#define P_APP_SEQUENCE_FAULT  (P_APP_SEQUENCE_n_ON | P2_ARR(4))


extern QueueHandle_t g_buzzer_cmd_q;


#ifdef __cplusplus
extern "C" {
#endif

void buzzer_hw_init(void);

#ifdef __cplusplus
}
#endif

#endif // BUZZER_H
