/**
  ******************************************************************************
  * @file       shell.h
  * @brief      Public interface for the UART command shell.
  * @author		Omar Ahmed
  ******************************************************************************
  * @details 	Defines UART buffer and command length limits. Declares shared
  *          	shell state and functions for command reception, parsing,
  *          	formatted output, status display, and stored sensor data decoding.
  *
  *
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
extern volatile uint8_t uart_rx_ready;
extern uint8_t pause_sd_logging;

/* Functions ---------------------------------------------------------*/
void shell_printf(const char *msg, ...);		// ... is ellipsis. variadic function --> accepts any number of additional arguments
void UART_Recieve_Start(void);
void shell_poll(void);
void shell_execute(char *msg);
int  parse_int(char *str, uint32_t *val);
void decode_print_sd_rx_block(uint8_t *read_buffer);
void print_status(void);

#endif
