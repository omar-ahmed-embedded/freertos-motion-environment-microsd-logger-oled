/**
  ******************************************************************************
  * @file       sd.c
  * @brief      mciroSD driver
  * @author		Omar Ahmed
  ******************************************************************************
  * @details 	Initializes the card, handles byte or block addressing, and reads
  *          	and writes 512-byte blocks. Separates read requests and write
  *          	initiation from completion polling. Tracks logging progress and
  *          	clears the current session's logs by overwriting them with zeros.
  *
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "sd.h"
#include "main.h"
#include "shell.h"
#include "mpu6050.h"
#include "ff.h"
#include "fatfs.h" // USERFatFS and USERPath
#include "freertos-app.h"

/* Defines ------------------------------------------------------------------*/

/* Variables ---------------------------------------------------------*/
// SD initialization
uint8_t	 sd_block_addressing = 0;
uint8_t	 sd_initialized = 0;


// SD write
uint8_t  sd_write_in_progress = 0;
uint32_t sd_write_start_time = 0;
uint8_t  sd_write_buffer[SD_BLOCK_SIZE];

// SD read
uint32_t  sd_rx_block = 0; 			// Requested block number.
uint8_t  sd_read_flag = 0;			// Read requested.
uint8_t  sd_read_failed = 0;
uint8_t  sd_read_request_in_progress = 0; // CMD17 accepted; awaiting data.
uint32_t sd_read_start_time = 0;
uint8_t  sd_read_buffer[SD_BLOCK_SIZE];

uint8_t  r1;
uint8_t  resp;
uint32_t address;



/* Code ---------------------------------------------------------*/
void cs_low(void){ HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET); }
void cs_high(void){ HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET); }
void sd_end_comm(void){ cs_high(); sd_xfer(0xFF); }

/**
  * @brief Transfer a byte to the SD card
  * @retval none
  */
uint8_t sd_xfer(uint8_t out){

	uint8_t in = 0xFF;
	HAL_SPI_TransmitReceive(&hspi1, &out, &in, 1, 100);		//Receive in. 1 = size of frames
	return in;

}

/**
  * @brief Format and send a command to the SD card
  * @retval none
  */
void sd_send_command(uint8_t cmd, uint32_t arg, uint8_t crc){

	sd_xfer(0xFF);
	sd_xfer(0x40 | cmd);
	sd_xfer((uint8_t)(arg>>24)); //last byte of arg
    sd_xfer((uint8_t)(arg >> 16));
    sd_xfer((uint8_t)(arg >> 8));
    sd_xfer((uint8_t)(arg));	// first byte of arg
    sd_xfer(crc);

}
/**
  * @brief Read SD response
  * @retval response
  */
uint8_t sd_read_r1(void){

	for(uint8_t i = 0; i < 10; i++){	// Send 1111 1111 ten times or until a valid reponse is received.
		uint8_t r = sd_xfer(0xFF);
		if((r&0x80)==0){return r;}		// When idle, SD always has 1 at bit 7 as MISO is driven high and 0 in a valid response
	}									// return once a response is valid
    return 0xFF;                      // nothing came back
}

/**
  * @brief Send a command and read response
  * @retval r1
  */

uint8_t sd_send_read_r1(uint8_t cmd, uint32_t arg, uint8_t crc){

	sd_send_command(cmd, arg, crc);
	return sd_read_r1();
}

/**
  * @brief SD card intialization
  * @retval sd_status_t value
  */

sd_status_t sd_init(void){

	uint8_t  resp[4];
	uint32_t t0;

	/* Wake card up by sending 74+ clocks with CS and MOSI High */
	HAL_Delay(10);
	cs_high();
	for (int i = 0; i < 10; i++) { sd_xfer(0xFF); }// 10*8 = 80 clocks

	/* Start communicating */
	cs_low();

	/* Enter SPI mode. Command->CMDO = 	40 00 00 00 00 95 */
	r1 = 0xFF;
	for(uint8_t i = 0; i<10 && r1!= 0x01; i++){		// Try 10 times. First attempt often fails
		r1 = sd_send_read_r1(0, 0x00000000, 0x95);		// Expect r1 = 0x01(idle)
	}
	if (r1 != 0x01) { sd_end_comm(); return SD_ERROR_CMDO;}	// Raise an error if r1 is still not 0x01

	/* Check version and if the card support the specified voltage range. Command: CMD8	48 00 00 01 AA 87 */
	r1 = sd_send_read_r1(8, 0x000001AA, 0x87);		// Expect r1 = 0x01(idle)
	// 0x1 -> host has operating range of 2.7-3.6V
	// AA is a chosen pattern for the card to echo back
	if (r1 != 0x01) { sd_end_comm(); return SD_ERROR_CMD8; }
	for (uint8_t i = 0; i < 4; i++){ resp[i]=sd_xfer(0xFF); }

	if (resp[2] != 0x01 || resp[3] != 0xAA) { sd_end_comm(); return SD_ERROR_CMD8; }

	/* Finish intialization and leave idle state. Command->ACMD41 = 69 40 00 00 00 01*/
	// Poll until intialization is complete. 0x01 busy, then 0x00 ready. Can take up  to 1000 ms.
	// Aplication specific commands must be preceeded by the prefix CMD55 = 77 00 00 00 00 01. R1 = 0x01 while still idle
	t0 = HAL_GetTick();
	do{
		sd_send_read_r1(55, 0, 0x01);	// CMD55
		r1 = sd_send_read_r1(41, 0x40000000, 0x01);	// ACMD41
		if (HAL_GetTick() - t0 > 1000) { sd_end_comm(); return SD_ERROR_ACMD41; }
	 } while (r1 != 0x00);

	/* Check if intialization is complete. Command: CMD58	7A 00 00 00 00 01 */
	r1 = sd_send_read_r1(58, 0x00000000, 0x01);
	if (r1 != 0x00) { sd_end_comm(); return SD_ERROR_CMD58; }
	for (int i = 0; i < 4; i++){ resp[i] = sd_xfer(0xFF); }// OCR bytes follow R1 directly
	// OCR[31]: intitialization complete
	if ((resp[0] & 0x80) == 0) { sd_end_comm(); return SD_ERROR_CMD58;}

	// OCR[30]: 1 = SDHC/SDXC block addressing
	sd_block_addressing = (resp[0] & 0x40) ? 1 : 0;

	sd_end_comm();

	 // Now, it is safe to speed up.
	  __HAL_SPI_DISABLE(&hspi1);
	  hspi1.Instance->CR1 = (hspi1.Instance->CR1 & ~SPI_CR1_BR_Msk)
	                         | SPI_BAUDRATEPRESCALER_8;     /* 84/8 = 10.5 MHz */
	  __HAL_SPI_ENABLE(&hspi1);

	 sd_initialized = 1;

	  return SD_OK;
}


/**
  * @brief Write block in card
  * @retval sd_status_t value
  */
sd_status_t sd_write_sector(const uint8_t *buf, uint32_t sector)
{
    if (!sd_initialized) {
        return SD_ERROR_NOT_INIT;
    }

    if (buf == NULL) {
        return SD_ERROR_WRITE;
    }

    uint32_t address = sd_block_addressing
                     ? sector
                     : sector * SD_BLOCK_SIZE;

    cs_low();

    /* CMD24: write the sector requested by FatFs. */
    uint8_t r1 = sd_send_read_r1(24, address, 0x01);

    if (r1 != 0x00) {
        sd_end_comm();
        return SD_ERROR_WRITE;
    }

    sd_xfer(0xFF);
    sd_xfer(0xFE);  /* Start of data. */

    for (uint16_t i = 0; i < SD_BLOCK_SIZE; i++) {
        sd_xfer(buf[i]);
    }

    /* Dummy CRC bytes; assumes SPI data CRC is disabled. */
    sd_xfer(0xFF);
    sd_xfer(0xFF);

    uint8_t response = sd_xfer(0xFF);

    if ((response & 0x1F) != 0x05) {
        sd_end_comm();
        return SD_ERROR_WRITE;
    }

    /* Data accepted. Wait for internal flash programming. */
    uint32_t start = HAL_GetTick();

    while (sd_xfer(0xFF) != 0xFF) {
        if ((uint32_t)(HAL_GetTick() - start) >= 1000U) {
            sd_end_comm();
            return SD_ERROR_TIMEOUT;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* CMD13: check programming status. */
    r1 = sd_send_read_r1(13, 0, 0x01);
    response = sd_xfer(0xFF);

    sd_end_comm();

    if (r1 != 0x00 || response != 0x00) {
        return SD_ERROR_WRITE;
    }

    return SD_OK;
}


/**
  * @brief Request sd block read
  * @retval sd_status_t
  */

/**
  * @brief Read the CSD register and report capacity in 512-byte sectors.
  * @retval SD_OK on success, otherwise an SD error.
  */
sd_status_t sd_get_sector_count(uint32_t *sector_count)
{
    uint8_t csd[16];
    uint8_t token;
    uint64_t sectors;

    if (!sd_initialized) {
        return SD_ERROR_NOT_INIT;
    }
    if (sector_count == NULL) {
        return SD_ERROR_READ;
    }
    if (sd_write_in_progress || sd_read_request_in_progress) {
        return SD_BUSY;
    }

    cs_low();
    if (sd_send_read_r1(9, 0, 0x01) != 0x00) {
        sd_end_comm();
        return SD_ERROR_READ;
    }

    uint32_t start = HAL_GetTick();
    do {
        token = sd_xfer(0xFF);
        if (token == 0xFF) {
            if ((uint32_t)(HAL_GetTick() - start) >= 1000U) {
                sd_end_comm();
                return SD_ERROR_TIMEOUT;
            }
            vTaskDelay(1);
        }
    } while (token == 0xFF);

    if (token != 0xFE) {
        sd_end_comm();
        return SD_ERROR_READ;
    }
    for (uint16_t i = 0; i < sizeof(csd); i++) {
        csd[i] = sd_xfer(0xFF);
    }
    sd_xfer(0xFF); /* Discard CRC16 (SPI data CRC is disabled). */
    sd_xfer(0xFF);
    sd_end_comm();

    if ((csd[0] >> 6) == 1U) {
        /* CSD v2: SDHC/SDXC capacity = (C_SIZE + 1) * 512 KiB. */
        uint32_t c_size = ((uint32_t)(csd[7] & 0x3FU) << 16)
                        | ((uint32_t)csd[8] << 8) | csd[9];
        sectors = ((uint64_t)c_size + 1U) * 1024U;
    } else if ((csd[0] >> 6) == 0U) {
        /* CSD v1: capacity = (C_SIZE + 1) * 2^(C_SIZE_MULT+2)
         *                              * 2^READ_BL_LEN bytes. */
        uint32_t read_bl_len = csd[5] & 0x0FU;
        uint32_t c_size = ((uint32_t)(csd[6] & 0x03U) << 10)
                        | ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
        uint32_t c_size_mult = ((uint32_t)(csd[9] & 0x03U) << 1)
                             | (csd[10] >> 7);
        if (read_bl_len < 9U || read_bl_len > 11U) {
            return SD_ERROR_READ;
        }
        sectors = (((uint64_t)c_size + 1U)
                  << (c_size_mult + 2U + read_bl_len)) / SD_BLOCK_SIZE;
    } else {
        return SD_ERROR_READ;
    }

    if (sectors == 0 || sectors > UINT32_MAX) {
        return SD_ERROR_READ;
    }
    *sector_count = (uint32_t)sectors;
    return SD_OK;
}

/**
  * @brief Read one physical sector into sd_read_buffer.
  * @retval SD_OK on success, otherwise an SD error.
  */
sd_status_t sd_read_sector(uint32_t block)
{
    if (!sd_initialized) {
        return SD_ERROR_NOT_INIT;
    }

    /* SDSC cards use byte addresses; prevent multiplication overflow. */
    if (!sd_block_addressing && block > UINT32_MAX / SD_BLOCK_SIZE) {
        return SD_ERROR_READ;
    }

    cs_low();

    /* CMD17: request one sector. */
    uint8_t response = sd_send_read_r1(17, address, 0x01);

    if (response != 0x00) {
        sd_end_comm();
        return SD_ERROR_READ;
    }

    /* Wait for the start-of-data token. */
    uint32_t start = HAL_GetTick();
    uint8_t token;

    while ((token = sd_xfer(0xFF)) == 0xFF) {
        if ((uint32_t)(HAL_GetTick() - start) >= 1000U) {
            sd_end_comm();
            return SD_ERROR_TIMEOUT;
        }

        vTaskDelay(1);
    }

    if (token != 0xFE) {
        sd_end_comm();
        return SD_ERROR_READ;
    }

    /* Receive the 512-byte sector. */
    for (uint16_t i = 0; i < SD_BLOCK_SIZE; i++) {
        sd_read_buffer[i] = sd_xfer(0xFF);
    }

    /* Receive and discard CRC16; SPI data CRC is disabled. */
    sd_xfer(0xFF);
    sd_xfer(0xFF);

    sd_end_comm();

    return SD_OK;
}

/**
  * @brief read sd file
  * @retval sd_status_t
  */
FRESULT sd_read_file(const TCHAR *filename)
{

    /* Static storage to avoid placing these large objects on the task stack*/
    static FIL file;
    static uint8_t buffer[SD_BLOCK_SIZE];


    FRESULT result;
    UINT bytes_read;

    if (filename == NULL) {
        return FR_INVALID_PARAMETER;
    }

    xSemaphoreTake(sd_mutex_handle, portMAX_DELAY);

    result = f_open(&file, filename, FA_READ);

    if (result != FR_OK) {
        xSemaphoreGive(sd_mutex_handle);
        return result;
    }

    for (;;) {
        result = f_read(&file, buffer, sizeof(buffer), &bytes_read);

        if (result != FR_OK) {
            break;
        }

        if (bytes_read == 0) {
            break; /* End of file. */
        }

        /* Print this chunk, 16 bytes per line. */
        for (UINT row = 0; row < bytes_read; row += 16U) {
            char line[16U * 3U + 1U];
            UINT position = 0;

            /* Print the bytes received, 16 per line. */
            for (UINT i = 0; i < bytes_read; i++) {
                shell_printf("%02X ", (unsigned int)buffer[i]);

                if ((i + 1U) % 16U == 0U || i + 1U == bytes_read) {			// Every 16 line, or after bytes read
                    shell_printf("\r\n");
                }
            }
        }
    }

    /* Close even if reading failed. */
    FRESULT close_result = f_close(&file);

    xSemaphoreGive(sd_mutex_handle);

    /* Preserve a read error; otherwise report the close result. */
    return (result != FR_OK) ? result : close_result;
}



/**
  * @brief sd sensor log task
  * @retval
  */
void sd_sensor_log_task(void *arg){

	sensor_sd_log_t log;
	static FIL mpu_file;
	FRESULT result;
	UINT bytes_written;

	/* Makes the card’s filesystem available to FatFs.*/
	result = f_mount(&USERFatFS, USERPath, 1);

	 if (result != FR_OK) {
	        shell_printf("SD mount failed: %d\r\n", (int)result);
	        vTaskDelete(NULL);
	        return;
	 }


	for(;;){

		xQueueReceive(sd_log_queue_handle, &log, portMAX_DELAY);

        /* Log MPU6050*/
		if(log.sensor_type == SENSOR_MPU6050){
			/* Create file for MPU6050*/
			xSemaphoreTake(sd_mutex_handle, portMAX_DELAY);			// Shared with sd_read

			result = f_open(&mpu_file, "0:/MPU6050.BIN", FA_WRITE | FA_OPEN_ALWAYS);

			if (result != FR_OK) {
				shell_printf("File open failed: %d\r\n", (int)result);
				xSemaphoreGive(sd_mutex_handle);
				continue;
			}

			/* Move to the end so new readings are appended. */
			f_lseek(&mpu_file, f_size(&mpu_file));

			result = f_write(&mpu_file,log.buffer, SD_BLOCK_SIZE, &bytes_written);

			if (result != FR_OK || bytes_written != SD_BLOCK_SIZE) {
			    shell_printf("MPU write failed: result=%d, bytes=%u\r\n",
			                 (int)result, (unsigned int)bytes_written);

			    FRESULT close_result = f_close(&mpu_file);

			    if (close_result != FR_OK) {
			        shell_printf("MPU close failed: %d\r\n", (int)close_result);
			    }
			    xSemaphoreGive(sd_mutex_handle);
			    continue;
			}

			shell_printf("MPU data saved.\r\n");

			result = f_close(&mpu_file); // Also flushes the metadata

			if (result != FR_OK) {
				shell_printf("File close failed: %d\r\n", (int)result);
			}
		}
		/* Log BME280*/
		else if (log.sensor_type == SENSOR_BME280){
		    static FIL bme_file;

		    result = f_open(&bme_file, "0:/BME280.BIN", FA_WRITE | FA_OPEN_APPEND);

		    if (result != FR_OK) {
		        shell_printf("BME file open failed: %d\r\n", (int)result);
		        continue;
		    }

		    result = f_write(&bme_file, log.buffer, SD_BLOCK_SIZE, &bytes_written);

		    if (result != FR_OK || bytes_written != SD_BLOCK_SIZE) {
		        shell_printf("BME write failed: result=%d, bytes=%u\r\n",
		                     (int)result, (unsigned int)bytes_written);

		        FRESULT close_result = f_close(&bme_file);

		        if (close_result != FR_OK) {
		            shell_printf("BME close failed: %d\r\n", (int)close_result);
		        }

		        continue;
		    }

		    result = f_close(&bme_file);

		    if (result != FR_OK) {
		        shell_printf("BME close failed: %d\r\n", (int)result);
		        continue;
		    }

		    shell_printf("BME data saved.\r\n");
		}
		else{
			shell_printf("Unkown sensor type!\r\n");
		}
	}
}



/**
  * @brief Convert the sd_status error to a string
  * @retval error as a string
  */

char *error_str(sd_status_t res){

	switch (res){

	case SD_OK:
		return "SD_OK";
	case SD_BUSY:
		return "SD_BUSY";
	case SD_ERROR_CMDO:
		return "SD_ERROR_CMDO";
	case SD_ERROR_CMD8:
		return "SD_ERROR_CMD8";
	case SD_ERROR_ACMD41:
		return "SD_ERROR_ACMD41";
	case SD_ERROR_CMD58:
		return "SD_ERROR_CMD58";
	case SD_ERROR_WRITE:
		return "SD_ERROR_WRITE";
	case SD_ERROR_READ :
		return "SD_ERROR_READ";
	case SD_ERROR_TIMEOUT:
		return "SD_ERROR_TIMEOUT";
	case SD_ERROR_NOT_INIT:
		return "SD_ERROR_NOT_INIT";
	default:
		return "SD_UNKNOWN_STATUS";

	}
}














































