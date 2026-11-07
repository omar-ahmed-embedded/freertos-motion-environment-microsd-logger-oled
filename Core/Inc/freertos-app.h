/**
  ******************************************************************************
  * @file       bme280.h
  * @brief      header file for bme280.c.
  * @author		Omar Ahmed
  ******************************************************************************
  */

#ifndef FREERTOS_APP_H
#define FREERTOS_APP_H

/* Includes ---------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"


extern TaskHandle_t shell_read_task_handle;
extern TaskHandle_t shell_print_task_handle;
extern TaskHandle_t mpu6050_sample_task_handle;

extern QueueHandle_t shell_print_queue_handle;
extern QueueHandle_t sd_log_queue_handle;
/* Functions ---------------------------------------------------------*/
void FreeRTOS_Init(void);

#endif /* FREERTOS_APP_H */
