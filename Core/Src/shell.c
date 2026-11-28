/**
  ******************************************************************************
  * @file      shell.c
  * @brief     UART command shell and formatted terminal output.
  * @author    Omar Ahmed
  ******************************************************************************
  * @details   Receives commands through UART DMA and queues terminal messages.
  *            Controls sensor sampling, OLED updates, and logging, and handles
  *            commands to read, abort, list, and clear sensor files.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "freertos-app.h"
#include "shell.h"
#include "mpu6050.h"
#include "sd.h"
#include "stm32f4xx_ll_usart.h"
#include "oled.h"

#include <string.h>		// Byte and string manipulation
#include <stdio.h>		// Declares vsnprintf, used in shell_printf
#include <stdarg.h>		// Standard arguments, machinery for variadic functions.
#include <stdlib.h>		// Standard library.

/* Variables ---------------------------------------------------------*/
uint8_t uart_rx_buffer[UART_RX_BUFFER_SIZE];
char    uart_tx_buffer[UART_TX_BUFFER_SIZE];

uint8_t rx_head = 0;
uint8_t rx_tail = 0;
char    rx_msg[MSG_LEN_MAX];
uint8_t rx_msg_len = 0;
uint8_t abort_file_read = 0;

uint8_t pause_mpu_sampling = 0;
uint8_t pause_bme_sampling = 0;

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
  * @brief  UART nterrupt call back function. Notify shell read task.
  * @retval none
  */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size){  //size represents number of bytes received when the callback is called

	BaseType_t higher_priority_task_woken = pdFALSE;

	if(huart->Instance == USART2){
		/* Notify shell read task*/
		vTaskNotifyGiveFromISR(shell_read_task_handle, &higher_priority_task_woken);
		portYIELD_FROM_ISR(higher_priority_task_woken);
	}
}

/**
  * @brief  UART transmit complete interrupt call back function.
  * @retval none
  */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        BaseType_t higher_priority_task_woken = pdFALSE;

        vTaskNotifyGiveFromISR(sd_file_read_task_handle,
                              &higher_priority_task_woken);

        portYIELD_FROM_ISR(higher_priority_task_woken);
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
  * @brief Execute supported shell commands.
  * @retval None
  */
void shell_execute(char *msg)
{
    if (msg == NULL || msg[0] == '\0') {
        return;
    }

    if (strcmp(msg, "help") == 0) {
        shell_printf("\r\n");
        shell_printf("%-28s%s\r\n", "help:", "Displays available commands");
        shell_printf("%-28s%s\r\n", "list files:", "Lists SD files and their sizes");
        shell_printf("%-28s%s\r\n", "read mpu:", "Reads the MPU6050 file from SD");
        shell_printf("%-28s%s\r\n", "read bme:", "Reads the BME280 file from SD");
        shell_printf("%-28s%s\r\n", "pause mpu:", "Reads the MPU6050 file from SD");
        shell_printf("%-28s%s\r\n", "pause bme:", "Reads the BME280 file from SD");
        shell_printf("%-28s%s\r\n", "resume mpu:", "Reads the MPU6050 file from SD");
        shell_printf("%-28s%s\r\n", "resume bme:", "Reads the BME280 file from SD");
        shell_printf("%-28s%s\r\n", "pause live bme:", "Pauses live BME280 readings on the OLED");
        shell_printf("%-28s%s\r\n", "resume live bme:", "Resumes live BME280 readings on the OLED");
        shell_printf("%-28s%s\r\n", "pause live mpu:", "Pauses live MPU6050 readings on the OLED");
        shell_printf("%-28s%s\r\n", "resume live mpu:", "Resumes live MPU6050 readings on the OLED");
        shell_printf("%-28s%s\r\n", "abort read:", "Aborts file read from SD");
        shell_printf("%-28s%s\r\n", "pause logging:", "Pauses file logging for both sensors");
        shell_printf("%-28s%s\r\n", "resume logging:", "Resumes file logging for both sensors");
        shell_printf("%-28s%s\r\n", "clear mpu:", "Clears the MPU6050 log file");
        shell_printf("%-28s%s\r\n", "clear bme:", "Clears the BME280 log file");
        shell_printf("\r\n");
    }

    else if (strcmp(msg, "read mpu") == 0 ||
             strcmp(msg, "read bme") == 0) {

        sensor_type_t sensor_type =
            (strcmp(msg, "read mpu") == 0)
            ? SENSOR_MPU6050
            : SENSOR_BME280;

        if (xQueueSend(sd_read_queue_handle, &sensor_type,
                       pdMS_TO_TICKS(250)) == pdPASS) {
            shell_printf("File-read request queued.\r\n");
        } else {
            shell_printf("File-read queue full. Try again later.\r\n");
        }
    }

	else if (strcmp(msg, "pause mpu") == 0) {
		pause_mpu_sampling = 1;
		shell_printf("\r\nMPU6050 sampling paused.\r\n");
	}

	else if (strcmp(msg, "resume mpu") == 0) {
		pause_mpu_sampling = 0;
		xTaskNotifyGive(mpu6050_sample_task_handle);
		shell_printf("\r\nMPU6050 sampling resumed.\r\n");
	}

	else if (strcmp(msg, "pause bme") == 0) {
		pause_bme_sampling = 1;
		shell_printf("\r\nBME280 sampling paused.\r\n");
	}

	else if (strcmp(msg, "resume bme") == 0) {
		pause_bme_sampling = 0;
		xTaskNotifyGive(bme280_sample_task_handle);
		shell_printf("\r\nBME280 sampling resumed.\r\n");
	}

	else if (strcmp(msg, "clear mpu") == 0) {
	    sd_clear_sensor_file(SENSOR_MPU6050);
	}
	else if (strcmp(msg, "clear bme") == 0) {
	    sd_clear_sensor_file(SENSOR_BME280);
	}

    else if (strcmp(msg, "pause live bme") == 0) {
        pause_live_bme = 1;
        shell_printf("\r\nBME280 live OLED display paused.\r\n");
    }

    else if (strcmp(msg, "resume live bme") == 0) {
    	pause_live_bme = 0;
        shell_printf("\r\nBME280 live OLED display resumed.\r\n");
    }

    else if (strcmp(msg, "pause live mpu") == 0) {
    	pause_live_mpu = 1;
        shell_printf("\r\nMPU6050 live OLED display paused.\r\n");
    }

    else if (strcmp(msg, "resume live mpu") == 0) {
    	pause_live_mpu = 0;
        shell_printf("\r\nMPU6050 live OLED display resumed.\r\n");
    }

    else if (strcmp(msg, "pause logging") == 0) {
        pause_sd_logging = 1;
        shell_printf("\r\nSD logging paused for both sensors.\r\n");
    }

    else if (strcmp(msg, "resume logging") == 0) {
        pause_sd_logging = 0;

        shell_printf("\r\nSD logging resumed for both sensors.\r\n");
    }

    else if (strcmp(msg, "abort read") == 0) {
    	abort_file_read = 1;

    }

    else if (strcmp(msg, "list files") == 0) {
        sd_list_files();
    }
    else {
        shell_printf("Unknown command. Type 'help' for available commands.\r\n");
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
    shell_print_msg_t print_msg_send;
    //while (huart2.gState != HAL_UART_STATE_READY){
        // Wait until UART is ready for another operation after the latest HAL_UART_Transmit_DMA
        // Acceptable because it is only called in the main loop
    //}

    // Intialize args --> args-> first agrument after str (the last named argument of the function)
    va_start(args, str);        // Gives vsnprint access to the arguments supplied through ...

    // Formulate the arguments into a string buffer with a max.
    len = vsnprintf(print_msg_send.data, UART_TX_BUFFER_SIZE, str, args);        // Difference between snprintf is that it accepts vardiac (accepts additional arguments)
                                                               // Len recieves the bytes written or would have been written if the buffer is not long enough in the buffer. (minus 1(\0))
    va_end(args);      // Finished using va_start
    if (len > 0){
        if(len > (int) UART_TX_BUFFER_SIZE){
            len = (int) UART_TX_BUFFER_SIZE;  // - \0
        }
        // Starts the transmission and returns before all the bytes have physically been transmitted.

       // Fill msg struct

       print_msg_send.len = len;
       // Queue struct
       xQueueSend(shell_print_queue_handle, &print_msg_send, pdMS_TO_TICKS(250));
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

	// Logging status
	char *logging_status = pause_sd_logging ? "paused" : "running";
	shell_printf("Logging status: %s\r\n", logging_status);
}

/**
* @brief Shell task function
* @retval None
*/

void shell_read_task(void *arg)
{

	UART_Recieve_Start();

	for(;;)
	{
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // Wait until ISR gives notification

		shell_poll();		// poll for msg and send a response
	}

}

/**
* @brief Shell task function
*
* @retval None
*/

void shell_print_task(void *arg)
{

	shell_print_msg_t print_msg_receive;

	for(;;)
	{
		  // Wait until shell_printf gives notification
		xQueueReceive(shell_print_queue_handle, &print_msg_receive, portMAX_DELAY);
		xSemaphoreTake(shell_print_mutex_handle, portMAX_DELAY);
		HAL_UART_Transmit(&huart2,(uint8_t *)print_msg_receive.data, print_msg_receive.len,100);
		xSemaphoreGive(shell_print_mutex_handle);
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

