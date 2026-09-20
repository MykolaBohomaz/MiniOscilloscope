/**
 * @file    app.h
 * @brief   Application entry points called from main.c USER CODE blocks.
 *
 *   main():  HAL_Init -> SystemClock_Config -> MX_*_Init  (CubeMX)
 *            app_init()                                   (USER CODE 2)
 *            for (;;) app_loop_once();                    (USER CODE 3)
 */
#ifndef APP_H
#define APP_H

void app_init(void);
void app_loop_once(void);

#endif /* APP_H */
