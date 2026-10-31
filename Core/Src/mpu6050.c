/**
  ******************************************************************************
  * @file      mpu6050.c
  * @brief     MPU6050 initialization, sampling, and live data display.
  * @author	   Omar Ahmed
  ******************************************************************************
  * @details   Configures sensor settings, and receives
  *            14-byte measurement frames through I2C DMA. Handles I2C callbacks
  *            and converts raw readings to acceleration, angular velocity,
  *            and temperature for terminal output.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "mpu6050.h"



/* Variables ---------------------------------------------------------*/
extern I2C_HandleTypeDef hi2c1;

volatile uint8_t i2c_sample_flag = 0;
volatile uint8_t i2c_frame_ready  = 0;

uint8_t i2c_rx_buffer[I2C_RX_FRAME_SIZE];
uint8_t mpu_log_buffer[LOG_BUFFER_SIZE];

int i2c_sample_count = 0;
uint16_t indx = 0;
volatile uint32_t i2c_overrun_count = 0;
int count;
int count2;
uint8_t start_timer = 0;

uint8_t display_live_mpu = 0;

/**
* @brief mpu6050 initialization
* @retval None
*/
mpu_status_t mpu6050_init(void){

	// Confirm the chip is there
	uint8_t id =0;			// id should be 0x68
	if(HAL_I2C_Mem_Read(&hi2c1, MPU6050_ADDR << 1, MPU6050_WHO_AM_I_REG, I2C_MEMADD_SIZE_8BIT, &id, 1, 100) != HAL_OK || id != 0x68){
	    return MPU_ERROR_WHO_AM_I;

	}

	/* Wake sensor up ans set clock. */
	uint8_t pwr = 0x01; 	// Sleep = 0.  CLKSEL = 0x01 -> set PLL with X-axis gyroscope reference.
	if (HAL_I2C_Mem_Write(&hi2c1, MPU6050_ADDR << 1, MPU6050_PWR_MGMT_1_REG, I2C_MEMADD_SIZE_8BIT, &pwr, 1, 1000)!= HAL_OK) {
		return MPU_ERROR_PWR_MGMT;
	}

	/* Sample Rate = Gyroscope Output Rate / (1 + SMPLRT_DIV) */
	uint8_t config = 0x03;       // Select output rate as 1 kHz. DLPF_CFG = 3
	uint8_t sample_div = 199;      // 1 kHz / (1 + 199) = 5Hz
	if (HAL_I2C_Mem_Write(&hi2c1, MPU6050_ADDR << 1, MPU6050_CONFIG_REG, I2C_MEMADD_SIZE_8BIT, &config,  1, 100) != HAL_OK){
		return MPU_ERROR_CONFIG_REG;
	}

	if (HAL_I2C_Mem_Write(&hi2c1, MPU6050_ADDR << 1, MPU6050_SMPLRT_DIV_REG, I2C_MEMADD_SIZE_8BIT, &sample_div, 1, 100) != HAL_OK){
		return MPU_ERROR_SMPLRT_DIV;
	}

	/* Config gyro range. */
	uint8_t gyro_cfg = (0 << 3);   // FS_SEL=0 -> ±250 °/s

	if (HAL_I2C_Mem_Write(&hi2c1, MPU6050_ADDR << 1, MPU6050_GYRO_CONFIG_REG, I2C_MEMADD_SIZE_8BIT, &gyro_cfg, 1, 100) != HAL_OK){
		return MPU_ERROR_FS_SEL;
	}
	 shell_printf("Gyro range set at ±250 °/s\r\n");

	/* Config acceleration range. */
	uint8_t accel_cfg = (0 << 3);  // AFS_SEL=0 -> ±2 g
	if (HAL_I2C_Mem_Write(&hi2c1, MPU6050_ADDR << 1, MPU6050_ACCEL_CONFIG_REG, I2C_MEMADD_SIZE_8BIT, &accel_cfg, 1, 100) != HAL_OK){
		return MPU_ERROR_AFS_SEL;
	}
	shell_printf("Accelerometer range set at ±2 g\r\n");

	return MPU_OK;
}


/**
* @brief Sample mpu6050
* @retval None
*/
void mpu6050_sample(void){
	// Sensor by defualt updates its registers with 8kHz (DLPF_CFG = 0)
	if (HAL_I2C_Mem_Read_DMA(&hi2c1, MPU6050_ADDR << 1, MPU6050_DATA_START_REG,
						I2C_MEMADD_SIZE_8BIT, i2c_rx_buffer, I2C_RX_FRAME_SIZE) != HAL_OK){
		shell_printf("MPU6050 read error!\r\n");
	}
}

/**
* @brief Print latest mpu6050readings
* @retval None
*/
void convert_display_live_sensor(void){

	// Combine raw values
	int16_t accel_x = (int16_t)((i2c_rx_buffer[0]  << 8) | i2c_rx_buffer[1]);
	int16_t accel_y = (int16_t)((i2c_rx_buffer[2]  << 8) | i2c_rx_buffer[3]);
	int16_t accel_z = (int16_t)((i2c_rx_buffer[4]  << 8) | i2c_rx_buffer[5]);
	int16_t temp    = (int16_t)((i2c_rx_buffer[6]  << 8) | i2c_rx_buffer[7]);
	int16_t gyro_x  = (int16_t)((i2c_rx_buffer[8]  << 8) | i2c_rx_buffer[9]);
	int16_t gyro_y  = (int16_t)((i2c_rx_buffer[10] << 8) | i2c_rx_buffer[11]);
	int16_t gyro_z  = (int16_t)((i2c_rx_buffer[12] << 8) | i2c_rx_buffer[13]);

	// Convert to units
	// Accel
	float ax_g = accel_x / 16384.0f;   // for AFS_SEL = 0, ±2g range
	float ay_g = accel_y / 16384.0f;
	float az_g = accel_z / 16384.0f;

	// Gyro
	float gx_dps = gyro_x / 131.0f;    // for FS_SEL = 0, ±250°/s range
	float gy_dps = gyro_y / 131.0f;
	float gz_dps = gyro_z / 131.0f;

	// Temp
	float temp_c = temp / 340.0f + 36.53f;

	// Display live values
	shell_printf("\r\n\r\naccel_x=%6.3f g   accel_y=%6.3f g   accel_z=%6.3f g\r\n",
				ax_g, ay_g, az_g);
	shell_printf("gyro_x =%7.2f dps  gyro_y =%7.2f dps  gyro_z =%7.2f dps\r\n",
				gx_dps, gy_dps, gz_dps);
	shell_printf("temp   =%5.1f C\r\n\r\n", temp_c);


}

/**
* @brief I2C DMA receive interrupt call back
* @retval None
*/
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c){

	if (hi2c->Instance == I2C1){
		i2c_frame_ready = 1;

	}
}


/**
* @brief Handle I2C error
* @retval None
*/
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c){

	if (hi2c->Instance == I2C1){
        i2c_sample_flag = 0;
        i2c_frame_ready = 0;
    }


}
