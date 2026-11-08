/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "freertos-app.h"
#include "mpu6050.h"
#include "sd.h"
#include "main.h"
#include "shell.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
TaskHandle_t shell_read_task_handle;
TaskHandle_t shell_print_task_handle;
TaskHandle_t mpu6050_sample_task_handle;
TaskHandle_t bme280_sample_task_handle;
TaskHandle_t sd_sensor_log_task_handle;

QueueHandle_t shell_print_queue_handle;
QueueHandle_t sd_log_queue_handle;

SemaphoreHandle_t i2c_mutex_handle;
SemaphoreHandle_t sd_mutex_handle;


/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
extern void bme280_sample_task(void *arg);
/* USER CODE END FunctionPrototypes */

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
void FreeRTOS_Init(void){

	shell_print_queue_handle = xQueueCreate(4, sizeof(shell_print_msg_t));
	sd_log_queue_handle = xQueueCreate(4, sizeof(sensor_sd_log_t));

	i2c_mutex_handle = xSemaphoreCreateMutex();
	sd_mutex_handle = xSemaphoreCreateMutex();

	xTaskCreate(
			shell_read_task,
			"shell_read_task",
			512,
			NULL,
			3,
			&shell_read_task_handle);

	xTaskCreate(
			shell_print_task,
			"shell_print_task",
			512,
			NULL,
			3,
			&shell_print_task_handle);

	xTaskCreate(
			mpu6050_sample_task,
			"mpu6050_sample_task",
			512,
			NULL,
			1,
			&mpu6050_sample_task_handle);

	xTaskCreate(
			bme280_sample_task,
			"bme280_sample_task",
			512,
			NULL,
			1,
			&bme280_sample_task_handle);

	xTaskCreate(
			sd_sensor_log_task,
			"sd_sensor_log_task",
			512,
			NULL,
			2,
			&sd_sensor_log_task_handle);


}
/* USER CODE END Application */

