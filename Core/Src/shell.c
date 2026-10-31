/**
  ******************************************************************************
  * @file      shell.c
  * @brief     UART command shell and formatted terminal output.
  * @author		Omar Ahmed
  ******************************************************************************
  * @details   Receives commands through UART DMA and processes help, status,
  *            block reading, logging control, live display, and clearing commands.
  *            Decodes stored sensor frames into values with units and transmits
  *            formatted messages using blocking UART transfers.
  *
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "shell.h"
#include "sd.h"
#include "mpu6050.h"
#include "stm32f4xx_ll_usart.h"

#include <string.h>		// Byte and string manipulation
#include <stdio.h>		// Declares vsnprintf, used in shell_printf
#include <stdarg.h>		// Standard arguments, machinery for variadic functions.
#include <stdlib.h>		// Standard library.

/* Variables ---------------------------------------------------------*/
uint8_t uart_rx_buffer[UART_RX_BUFFER_SIZE];
char    uart_tx_buffer[UART_TX_BUFFER_SIZE];
volatile uint8_t uart_rx_ready = 0;

uint8_t rx_head = 0;
uint8_t rx_tail = 0;
char    rx_msg[MSG_LEN_MAX];
uint8_t rx_msg_len = 0;
uint8_t pause_sd_logging = 0;


/* Code ---------------------------------------------------------*/
/**
  * @brief  Intialize recieve on the UART through DMA1. Always running.
  */

void UART_Recieve_Start(void){
	rx_tail = 0;
	rx_msg_len = 0;
	HAL_UARTEx_ReceiveToIdle_DMA(&huart2, uart_rx_buffer, UART_RX_BUFFER_SIZE); // Interrupt fires when idel or when data is more than size
}

/**
  * @brief  UART nterrupt call back function
  * @retval none
  */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size){  //size represents number of bytes received when the callback is called

	if(huart->Instance == USART2){
		uart_rx_ready = 1;
	}
}

/**
  * @brief  Pol what is received in rx_buffer and hand it to shell_execute.
  * @retval none
  */
void shell_poll(void){
	// Buffer size - NDTR, to get where the recieve msg stopped.
	// NDTR is updated continously.
	rx_head = (UART_RX_BUFFER_SIZE - (uint16_t)__HAL_DMA_GET_COUNTER(huart2.hdmarx))
			% UART_RX_BUFFER_SIZE;


	while (rx_tail != rx_head){			// tail = head when a msg is complete

		char c = uart_rx_buffer[rx_tail];
		rx_tail = (rx_tail+1) % UART_RX_BUFFER_SIZE; 	// Increment tail. Wrap at buffer end.

		if (c == '\r' || c == '\n'){		// When a msg is done
			if (rx_msg_len>0){
				rx_msg[rx_msg_len]= '\0';
				shell_execute(rx_msg);
				rx_msg_len = 0;
			}
		}
		else if (rx_msg_len < (MSG_LEN_MAX - 1)){
			rx_msg[rx_msg_len] = c;
			rx_msg_len++;
		}
		else{
			rx_msg_len = 0;	// Clear the rest of the line in overflow msgs
		}
	}
}


/**
  * @brief  Chech content of received msg, act on it, and print a response.
  * @retval none
  */
void shell_execute(char *msg){

	uint32_t block = 0;		// stroul return unsigned long, which is 32 bits on ARM32 M4
	sd_status_t result = 0;

	// Check msg
	// help
	if(strncmp(msg, "help",4) == 0){
		shell_printf("\r\n");
		// Pad the string with spaces from the right to align the text
		shell_printf("%-24s%s\r\n", "help:", "this text");
		shell_printf("%-24s%s\r\n", "status:", "prints current SD card type and logging information");
		shell_printf("%-24s%s\r\n", "read block <number>:", "prints decoded MPU6050 data stored in that block,");
		shell_printf("%-24sstart block: %u\r\n", "",SD_START_BLOCK);
		shell_printf("%-24s%s\r\n", "pause sensor logging:", "pauses logging in sd");
		shell_printf("%-24s%s\r\n", "resume sensor logging:", "resumes logging in sd");
		shell_printf("%-24s%s\r\n", "display live sensor:", "displayes live decoded sensor readings");
		shell_printf("%-24s%s\r\n", "pause live sensor:", "pauses live sensor display");
		shell_printf("%-24s%s\r\n", "clear sd:", "clears logged data in sd and resets logging buffers");

	}	// Print a specefic block
	else if (strncmp(msg, "read block", 10) == 0){
		if (sd_read_flag) {
		        shell_printf("An SD read is already waiting or running.\r\n");
		}
		else if(!parse_int(msg+11, &block) || block < SD_START_BLOCK|| block >= sd_next_block){
			shell_printf("Error: invalid block address!\r\n");

		}
		else {
			sd_rx_block = block;
			sd_read_failed = 0;
			sd_read_flag = 1;
		}
	}
	else if (strncmp(msg, "clear sd", 8) == 0){
		result = clear_sd();
		shell_printf("\r\nClearing sd...\r\n");
		if (result == SD_OK){ shell_printf("SD cleared and logging buffers reset. \r\n"); }
		else { shell_printf("Error sd_not cleared: error %s \r\n", error_str(result)); }
	}
	else if (strncmp(msg, "pause sensor logging", 20) == 0){
		pause_sd_logging = 1;
		 shell_printf("SD logging paused.\r\n");
	}
	else if(strncmp(msg, "resume sensor logging", 21) == 0){
		pause_sd_logging = 0;
		shell_printf("SD logging resumed.\r\n");
	}
	else if(strncmp(msg, "status", 6) == 0){
		print_status();
	}
	else if (strncmp(msg, "display live sensor", 19) == 0){
		display_live_mpu = 1;
		shell_printf("Live sensor readings displaying...\r\n");
	}
	else if (strncmp(msg, "pause live sensor", 17) == 0){
			display_live_mpu = 0;
			shell_printf("Live sensor display paused\r\n");
	}
	else{
		shell_printf("Error: unknown command!\r\n\r\n");
	}
}


/**
  * @brief  Parse a decimal value from a string and reject any trailing junk
  * @retval int, 1 on success, 0 on failure.
  */
int parse_int(char *str, uint32_t *val){

	uint32_t v;		// uint32_t is same size as unsigned long on Cortex M4
	char *end;

	while (*str == ' '){
		str++;
	}
	if(*str<'0' || *str>'9'){
		return 0;
	}

	v=strtoul(str, &end, 10);

	while (*end == ' '){
		end++;
	}

	if (*end!='\0'){
		return 0;

	}

	*val = v;
	return 1;
}


/**
 * @brief  Formats a message and hand it to the TX DMA.
 * * @retval None
 */

void shell_printf(const char *str, ...){

    va_list args;   // A type for handling the arguments represented by ..., it iterates over the additional arguments
    int len;

    //while (huart2.gState != HAL_UART_STATE_READY){
        // Wait until UART is ready for another operation after the latest HAL_UART_Transmit_DMA
        // Acceptable because it is only called in the main loop
    //}

    // Intialize args --> args-> first agrument after str (the last named argument of the function)
    va_start(args, str);        // Gives vsnprint access to the arguments supplied through ...

    // Formulate the arguments into a string buffer with a max.
    len = vsnprintf(uart_tx_buffer, UART_TX_BUFFER_SIZE, str, args);        // Difference between snprintf is that it accepts vardiac (accepts additional arguments)
                                                               // Len recieves the bytes written or would have been written if the buffer is not long enough in the buffer. (minus 1(\0))
    va_end(args);      // Finished using va_start
    if (len > 0){
        if(len > (int) UART_TX_BUFFER_SIZE){
            len = (int) UART_TX_BUFFER_SIZE;
        }
        // Starts the transmission and returns before all the bytes have physically been transmitted.
        HAL_UART_Transmit(&huart2,(uint8_t *)uart_tx_buffer, (uint16_t) len,100);
    }

}


/**
  * @brief Print current system status
  * @retval none
  */
void print_status(void){


	char *card_type =  sd_block_addressing ? "SDHC/SDXC": "SDSC";  	// Card type
	char *block_addressing = sd_block_addressing ? "block address (x1)"
			: "byte address (x512)"; // Block addressing

    if (sd_initialized) {
    	shell_printf("\r\nCard type: %s\r\n", card_type);
		shell_printf("Block addressinge: %s\r\n", block_addressing);
    }
    else {
    	shell_printf("SD card has not been successfully intialized\r\n");
    }

	// Number oflocks written
	shell_printf("Number of blocks written this session: %u\r\n", blocks_written);

	// Next block
	shell_printf("Next block written: %u\r\n", sd_next_block);

	// Logging status
	char *logging_status = pause_sd_logging ? "paused" : "running";
	shell_printf("Logging status: %s\r\n", logging_status);


}


/**
  * @brief Convert the read bytes into human readable values and print them
  * @retval none
  */
void decode_print_sd_rx_block(uint8_t *read_buffer){

	// Raw valiue
	int16_t accel_x;
	int16_t accel_y;
	int16_t accel_z;
	int16_t temp;
	int16_t gyro_x;
	int16_t gyro_y;
	int16_t gyro_z;

	float ax_g;
	float ay_g;
	float az_g;
	float gx_dps;
	float gy_dps;
	float gz_dps;
	float temp_c;


	for (uint16_t i = 0; i < MPU_LOG_DATA_SIZE; i += I2C_RX_FRAME_SIZE){

		uint8_t *frame = read_buffer + i;
		// Raw bytes
		accel_x = (int16_t)((frame[0]  << 8) | frame[1]);
		accel_y = (int16_t)((frame[2]  << 8) | frame[3]);
		accel_z = (int16_t)((frame[4]  << 8) | frame[5]);
		temp    = (int16_t)((frame[6]  << 8) | frame[7]);
		gyro_x  = (int16_t)((frame[8]  << 8) | frame[9]);
		gyro_y  = (int16_t)((frame[10] << 8) | frame[11]);
		gyro_z  = (int16_t)((frame[12] << 8) | frame[13]);

		// Convert to units
		ax_g = accel_x / 16384.0f;   // Accel	// for AFS_SEL = 0, ±2g range
		ay_g = accel_y / 16384.0f;
		az_g = accel_z / 16384.0f;
		temp_c = temp / 340.0f + 36.53f;	// Temp
		gx_dps = gyro_x / 131.0f;   	// Gyro	 // for FS_SEL = 0, ±250°/s range
		gy_dps = gyro_y / 131.0f;
		gz_dps = gyro_z / 131.0f;

		// Print values
		shell_printf("accel_x=%6.3f g   accel_y=%6.3f g   accel_z=%6.3f g\r\n",
						ax_g, ay_g, az_g);
		shell_printf("gyro_x =%7.2f dps  gyro_y =%7.2f dps  gyro_z =%7.2f dps\r\n",
						gx_dps, gy_dps, gz_dps);
		shell_printf("temp   =%5.1f C\r\n", temp_c);

	}
}




/**
* @brief Clear overflow flag when it occurs
* @retval None
*/
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{

    if(huart->Instance == USART2){
        __HAL_UART_CLEAR_OREFLAG(huart);    // A new byte finished arriving in the shift register before the previous one was read out of DR
        __HAL_UART_CLEAR_NEFLAG(huart);     // If the three samples in the middle of sampling disagree, the bit was noisy
        __HAL_UART_CLEAR_FEFLAG(huart);     // Frame error: stop bit was where it should not be

        if (huart->RxState != HAL_UART_STATE_BUSY_RX)
        {
        UART_Recieve_Start();
        }
    }
}

