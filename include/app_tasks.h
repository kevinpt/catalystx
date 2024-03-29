#ifndef APP_TASKS_H
#define APP_TASKS_H

#define DEBOUNCE_TASK_MS    11    // Run debounce filter
#define DEBOUNCE_FILTER_MS  80
#define LVGL_TASK_MS        5


void app_tasks_init(void);
void gui_tasks_init(void);
void audio_tasks_init(void);
void buzzer_task_init(void);

#endif // APP_TASKS_H
