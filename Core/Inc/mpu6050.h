/**
  ******************************************************************************
  * @file		mpu.h
  * @brief		Public interface and register definitions for the MPU6050
  * @author		Omar Ahmed
  *******************************************************************************
  * @details	Defines the sensor address, registers, frame size, logging buffer layout
  * 			and initialization status code. Declares shared sensor buffers, flags, functions.
  *
  *******************************************************************************
  */
#ifndef MPU6050_H
#define MPU6050_H

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_i2c.h"
#include "shell.h"
#include "sd.h"
/* Defines ------------------------------------------------------------*/
#define MPU_I2C_RX_FRAME_SIZE	14
#define LOG_BUFFER_SIZE	512
#define MPU_FRAME_PERBLOCK		(LOG_BUFFER_SIZE/MPU_I2C_RX_FRAME_SIZE)  //36
#define MPU_LOG_DATA_SIZE		(MPU_FRAME_PERBLOCK*MPU_I2C_RX_FRAME_SIZE)		//504

#define MPU6050_ADDR  			0x68
#define MPU6050_DATA_START_REG 	0x3B

#define MPU6050_WHO_AM_I_REG      	0x75
#define MPU6050_PWR_MGMT_1_REG    	0x6B
#define MPU6050_GYRO_CONFIG_REG   	0x1B
#define MPU6050_ACCEL_CONFIG_REG  	0x1C
#define MPU6050_CONFIG_REG			0x1A
#define MPU6050_SMPLRT_DIV_REG   	0x19
/* Variables ---------------------------------------------------------*/
extern I2C_HandleTypeDef hi2c1;

extern uint8_t i2c_mpu6050_rx_buffer[MPU_I2C_RX_FRAME_SIZE];

extern int i2c_sample_count;
extern volatile uint32_t i2c_overrun_count;
extern uint8_t start_timer;

extern uint8_t display_live_mpu;


typedef enum {
	MPU_OK = 0,
	MPU_ERROR_WHO_AM_I,
	MPU_ERROR_PWR_MGMT,
	MPU_ERROR_SMPLRT_DIV,
	MPU_ERROR_CONFIG_REG,	//output rate
	MPU_ERROR_FS_SEL,		// Config gyro
	MPU_ERROR_AFS_SEL,		// Config accl

} mpu_status_t;




/* Functions ------------------------------------------------------------------*/
mpu_status_t mpu6050_init(void);
void convert_display_live_sensor(void);
void decode_print_sd_rx_block(uint8_t *read_buffer);

// RTOS
void mpu6050_sample_task(void *arg);


#endif
