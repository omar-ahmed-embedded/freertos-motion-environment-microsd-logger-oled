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
#include "semphr.h"


extern TaskHandle_t shell_read_task_handle;
extern TaskHandle_t shell_print_task_handle;
extern TaskHandle_t mpu6050_sample_task_handle;
extern TaskHandle_t bme280_sample_task_handle;
extern TaskHandle_t sd_sensor_log_task_handle;
extern TaskHandle_t sd_file_read_task_handle;

extern QueueHandle_t shell_print_queue_handle;
extern QueueHandle_t sd_log_queue_handle;
extern QueueHandle_t sd_read_queue_handle;

extern SemaphoreHandle_t i2c_mutex_handle;
extern SemaphoreHandle_t sd_mutex_handle;
extern SemaphoreHandle_t i2c_rx_done_semaphore_handle;
extern SemaphoreHandle_t shell_print_mutex_handle;

/* Functions ---------------------------------------------------------*/
void FreeRTOS_Init(void);

#endif /* FREERTOS_APP_H */
