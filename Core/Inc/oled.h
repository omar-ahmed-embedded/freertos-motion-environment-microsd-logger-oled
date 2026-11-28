/**
  ******************************************************************************
  * @file      oled.h
  * @brief     OLED messages and display task interface.
  * @author    Omar Ahmed
  ******************************************************************************
  * @details   Defines the sensor display message and declares the display task
  *            and its pause flags.
  ******************************************************************************
  */

#ifndef INC_OLED_H_
#define INC_OLED_H_

/* Includes ------------------------------------------------------------------*/
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "ssd1306_tests.h"
#include "sd.h"
/* Defines ------------------------------------------------------------------*/

/* Variables ---------------------------------------------------------*/
extern uint8_t pause_live_mpu;
extern uint8_t pause_live_bme;

typedef struct {
    sensor_type_t sensor_type;

    float ax_g;
    float ay_g;
    float az_g;

    float gx_dps;
    float gy_dps;
    float gz_dps;

    float temperature_c;
    float pressure_pa;
    float humidity_rh;
} oled_sensor_msg_t;



/* Functions ---------------------------------------------------------*/
void oled_display_task(void *arg);

#endif /* INC_OLED_H_ */
