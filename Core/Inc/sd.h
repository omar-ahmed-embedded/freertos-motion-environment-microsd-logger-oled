/**
  ******************************************************************************
  * @file       sd.h
  * @brief      Public interface for the SPI microSD driver.
  * @author		Omar Ahmed
  ******************************************************************************
  * @details    Defines block size, logging start address, and SD status codes.
  *          	Declares transfer buffers, operation flags, logging counters,
  *          	and functions for initialization, reading, writing, and clearing.
  *
  *
  ******************************************************************************
  */

#ifndef SD_H
#define SD_H

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"
#include <stdint.h>
/* Defines ------------------------------------------------------------------*/
#define	SD_START_BLOCK	100000	//51 MB in
#define SD_BLOCK_SIZE	512


/* Variables ---------------------------------------------------------*/
extern SPI_HandleTypeDef hspi1;

extern uint8_t	sd_initialized;
extern uint8_t	sd_block_addressing;






extern uint8_t  sd_write_buffer[SD_BLOCK_SIZE];

extern uint32_t sd_rx_block;
extern uint8_t  sd_read_flag;
extern uint8_t  sd_read_failed;
extern uint8_t  sd_read_request_in_progress;
extern uint8_t  sd_read_buffer[SD_BLOCK_SIZE];

typedef enum {
	SD_OK = 0,
	SD_BUSY,
	SD_ERROR_CMDO,
	SD_ERROR_CMD8,
	SD_ERROR_ACMD41,
	SD_ERROR_CMD58,
	SD_ERROR_WRITE,
	SD_ERROR_READ,
	SD_ERROR_TIMEOUT,
	SD_ERROR_NOT_INIT,

} sd_status_t;

typedef enum {
    SENSOR_MPU6050,
    SENSOR_BME280
} sensor_type_t;

typedef struct {
    uint8_t buffer[SD_BLOCK_SIZE];
    sensor_type_t sensor_type;
} sensor_sd_log_t;

/* Functions ---------------------------------------------------------*/
uint8_t sd_xfer(uint8_t out);
void sd_send_command (uint8_t cmd, uint32_t arg, uint8_t crc);
void cs_low(void);
void cs_high(void);
sd_status_t sd_init(void);
sd_status_t sd_write_sector(const uint8_t *buf, uint32_t sector);
sd_status_t sd_write_poll(void);
sd_status_t sd_request_block(uint32_t block);
sd_status_t sd_read_block(void);
sd_status_t clear_sd();
void sd_sensor_log_task(void *arg);
char *error_str(sd_status_t res);

#endif
