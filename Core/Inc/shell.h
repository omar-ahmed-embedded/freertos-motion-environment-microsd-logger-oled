/**
  ******************************************************************************
  * @file      shell.h
  * @brief     UART shell definitions and task interfaces.
  * @author    Omar Ahmed
  ******************************************************************************
  * @details   Defines command buffers and queued print messages. Declares shell
  *            tasks, command handling, formatted output, and shared control flags.
  ******************************************************************************
  */

#ifndef SHELL_H
#define SHELL_H

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"


/* Defines ------------------------------------------------------------------*/
#define UART_RX_BUFFER_SIZE	64
#define UART_TX_BUFFER_SIZE	512
#define MSG_LEN_MAX			64

/* Variables ---------------------------------------------------------*/
extern UART_HandleTypeDef huart2;

extern uint8_t uart_rx_buffer[UART_RX_BUFFER_SIZE];
extern char uart_tx_buffer[UART_TX_BUFFER_SIZE];

extern uint8_t pause_sd_logging;
extern uint8_t pause_mpu_sampling;
extern uint8_t pause_bme_sampling;

/* Functions ---------------------------------------------------------*/
void shell_printf(const char *msg, ...);		// ... is ellipsis. variadic function --> accepts any number of additional arguments
void UART_Recieve_Start(void);
void shell_poll(void);
void shell_execute(char *msg);
int  parse_int(char *str, uint32_t *val);
void decode_print_sd_rx_block(uint8_t *read_buffer);
void print_status(void);


// FreeRTOS


/* Functions ---------------------------------------------------------*/
void shell_read_task(void *arg);
void shell_print_task(void *arg);

typedef struct{
	char data[UART_TX_BUFFER_SIZE];
	uint16_t len;
} shell_print_msg_t;

#endif
