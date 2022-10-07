#ifndef APP_TASKS_H
#define APP_TASKS_H

#define DEBOUNCE_TASK_MS    11    // Run debounce filter
#define DEBOUNCE_FILTER_MS  80

#define P_EVENT_BUTTON_n_PRESS      (P1_EVENT | P2_BUTTON | P2_ARR(0) | P4_PRESS)
#define P_EVENT_BUTTON_n_RELEASE    (P1_EVENT | P2_BUTTON | P2_ARR(0) | P4_RELEASE)

#define P_EVENT_BUTTON__USER_PRESS    (P_EVENT_BUTTON_n_PRESS | P2_ARR(0))
#define P_EVENT_BUTTON__USER_RELEASE  (P_EVENT_BUTTON_n_RELEASE | P2_ARR(0))

void app_tasks_init(void);
void audio_tasks_init(void);

#endif // APP_TASKS_H
