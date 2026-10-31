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
#include "peripheral_config.h"
#include "main.h"
#include "shell.h"
#include "mpu6050.h"

/* Defines ------------------------------------------------------------------*/

/* Variables ---------------------------------------------------------*/
// SD initialization
uint8_t	 sd_block_addressing = 0;
uint8_t	 sd_initialized = 0;
uint32_t sd_next_block = 0;
uint16_t blocks_written	= 0;

uint8_t sd_buffer_log_flag = 0;

// SD write
uint8_t  sd_write_in_progress = 0;
uint32_t sd_write_start_time = 0;
uint8_t  sd_write_flag = 0;
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

	  sd_next_block= SD_START_BLOCK;
	  sd_initialized = 1;

	  return SD_OK;
}


/**
  * @brief Write block in card
  * @retval sd_status_t value
  */
sd_status_t sd_write_block(uint8_t *buf){

    if (!sd_initialized) {return SD_ERROR_WRITE;}

    if (sd_write_in_progress) { return SD_BUSY; }

    cs_low();

	/* Request writing in the given address block. */
    address = sd_block_addressing ? sd_next_block : sd_next_block * 512U;
    r1 = sd_send_read_r1(24, address, 0x01);
    if (r1 != 0x00) { sd_end_comm(); return SD_ERROR_WRITE; }  // Use the error name from your enum.

    sd_xfer(0xFF);          // gap byte the card expects
    sd_xfer(0xFE);          // data token: 512 bytes follow

	/* Write buffer */
    sd_write_start_time = HAL_GetTick();
    for (uint16_t i = 0; i < SD_BLOCK_SIZE; i++){
         sd_xfer(buf[i]);
	}

    /* CRC16 - read and discard. */
    sd_xfer(0xFF);
    sd_xfer(0xFF);


    /* Check if card accepted the data: bottom 5 bits are 0x05 if accepted. */
    resp = sd_xfer(0xFF);
    if ((resp & 0x1F) != 0x05) { sd_end_comm(); return SD_ERROR_WRITE; }

    sd_write_in_progress = 1;
    // Leave CS low. The card is now programming.

    return SD_BUSY;
}


/**
  * @brief Poll until card finished programming and finished write
  * @retval sd_status_t value
  */
sd_status_t sd_write_poll(void){

	if (!sd_write_in_progress) { return SD_ERROR_WRITE; }

	/* The card holds MISO low while programming its flash.
       Usually 1-5 ms, but can exceed 100 ms when the card does
       internal housekeeping. */
    if(sd_xfer(0xFF) != 0xFF){
    	if(HAL_GetTick()-sd_write_start_time > 1000){
    		sd_end_comm();
    		 sd_write_in_progress = 0;
    		return SD_ERROR_WRITE;
    	}
    	return SD_BUSY;
    }

    /* Check for programming errors. Command -> 13 00 00 00 00 01*/
    r1 = sd_send_read_r1(13, 0, 0x01);
    resp = sd_xfer(0xFF);  // CMD13 returns two status bytes.
    if (r1 != 0x00 || resp != 0x00) {
    	sd_end_comm();
        sd_write_in_progress = 0;
    	return SD_ERROR_WRITE;
    }

    /* End Communication */
    sd_end_comm();
    sd_write_in_progress = 0;
    sd_write_flag = 0;

	/* Advance only on success */
    blocks_written++;
    sd_next_block++;

    return SD_OK;
}


/**
  * @brief Request sd block read
  * @retval sd_status_t
  */

sd_status_t sd_request_block(uint32_t block){

	if (!sd_initialized) { return SD_ERROR_READ; }

	if (sd_write_in_progress || sd_read_request_in_progress) { return SD_BUSY; }

	cs_low();
	/* Request block. Command ->  17 00 00 00 00 01*/
	address = sd_block_addressing ? block : block * 512;
	r1 = sd_send_read_r1(17, address, 0x01);	// r1 = 0x00 when accepted
	if (r1 != 0x00){ sd_end_comm();  return SD_ERROR_READ; }

	sd_read_start_time = HAL_GetTick();
	sd_read_request_in_progress = 1;

	return SD_BUSY;
}


/**
  * @brief read sd block read
  * @retval sd_status_t
  */

sd_status_t sd_read_block(void){

	uint8_t token;


	if (!sd_read_request_in_progress) { return SD_ERROR_READ; }

	/* Error timeout*/
	if (HAL_GetTick() - sd_read_start_time > 1000){
		sd_end_comm();
		sd_read_request_in_progress = 0;
		return SD_ERROR_TIMEOUT;
	}


	/* Expect 0xFE to receive the block after or 0xFF while the card fetches from flash */
	token = sd_xfer(0xFF);
	if (token == 0xFF) { return SD_BUSY; }
	if (token != 0xFE) {
		sd_end_comm();
	    sd_read_request_in_progress = 0;
	    return SD_ERROR_READ;
	}


	/* Read block */
	for (uint16_t i = 0; i < SD_BLOCK_SIZE; i++){
		sd_read_buffer[i] = sd_xfer(0xFF);
	}

	/* CRC16 - read and discard. */
	sd_xfer(0xFF);
	sd_xfer(0xFF);

    /* End Communication */
    sd_end_comm();
    sd_read_request_in_progress = 0;
    sd_read_flag = 0;

    return SD_OK;
}

/**
  * @brief Clear written data on sd
  * @retval sd_status_t
  */
sd_status_t clear_sd(){

    uint8_t empty_buffer[SD_BLOCK_SIZE] = {0};

    uint32_t saved_next_block = sd_next_block;
    uint16_t saved_blocks_written = blocks_written;
    sd_status_t result;


    if (!sd_initialized) {
          return SD_ERROR_NOT_INIT;
    }

    /* Let active transfers finish before clearing. */
    if (sd_write_in_progress || sd_read_request_in_progress ||
         HAL_I2C_GetState(&hi2c1) != HAL_I2C_STATE_READY) {
         return SD_BUSY;
    }

    /* Overwrite each recorded block with zeros. */
    for (uint32_t block = SD_START_BLOCK; block < sd_next_block; block++){

    	sd_next_block = block;
    	result = sd_write_block(empty_buffer);

    	while(result == SD_BUSY){
    		result = sd_write_poll();
    	}


    	//Clearing blocks must not increase the logging counters.
    	sd_next_block = saved_next_block;
    	blocks_written = saved_blocks_written;

        if (result != SD_OK) { return result; }
    }

    /* Discard queued requests and buffered sensor data. */
     sd_write_flag = 0;
     sd_read_flag = 0;
     sd_read_failed = 0;
     sd_buffer_log_flag = 0;

     indx = 0;
     i2c_frame_ready = 0;
     i2c_sample_flag = 0;

     for (uint16_t i = 0; i < SD_BLOCK_SIZE; i++) {
         sd_write_buffer[i] = 0;
         sd_read_buffer[i] = 0;
         mpu_log_buffer[i] = 0;
     }

     /* Start the next log at the beginning of the logging area. */
     sd_next_block = SD_START_BLOCK;
     blocks_written = 0;


     return SD_OK;
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














































