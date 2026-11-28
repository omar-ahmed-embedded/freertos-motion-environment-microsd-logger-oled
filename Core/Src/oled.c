/**
  ******************************************************************************
  * @file      oled.c
  * @brief     Live sensor display on the SPI OLED.
  * @author    Omar Ahmed
  ******************************************************************************
  * @details   Receives sensor readings from a queue and displays BME280 values
  *            on the left and MPU6050 values on the right. Supports independent
  *            pause and resume controls for each display section.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "oled.h"
#include "freertos-app.h"
#include <stdio.h>
/* Defines ------------------------------------------------------------------*/
uint8_t pause_live_bme = 0;
uint8_t pause_live_mpu = 0;
/* Variables ---------------------------------------------------------*/

/* Code ---------------------------------------------------------*/
/**
  * @brief Write one line within a screen half.
  * @retval None
  */
static void oled_write_line(uint8_t x, uint8_t y, char *text)
{
    ssd1306_SetCursor(x, y);
    ssd1306_WriteString(text, Font_6x8, White);
}

/**
  * @brief Format and display one numeric value.
  * @retval None
  */
static void oled_write_value(uint8_t x, uint8_t y,
                             const char *format, double value)
{
    char text[16];

    int length = snprintf(text, sizeof(text), format, value);

    if (length < 0) {
        return;
    }

    /* Each screen half fits at most ten 6-pixel characters. */
    text[10] = '\0';

    oled_write_line(x, y, text);
}

/**
* @brief mpu6050 sample task
* @retval None
*/
/**
  * @brief Receive sensor readings and display them on the OLED.
  * @retval None
  */
void oled_display_task(void *arg)
{
    (void)arg;

    oled_sensor_msg_t message;
    oled_sensor_msg_t latest_mpu = {0};
    oled_sensor_msg_t latest_bme = {0};

    uint8_t have_mpu = 0;
    uint8_t have_bme = 0;

    ssd1306_Init();

    ssd1306_Fill(Black);
    ssd1306_Line(60, 0, 60, 63, White);

    oled_write_line(0, 0, "BME280");
    oled_write_line(66, 0, "MPU6050");

    oled_write_line(0, 16, "Waiting");
    oled_write_line(66, 16, "Waiting");

    ssd1306_UpdateScreen();

    for (;;) {
        /* Block until either sensor sends an update. */
        if (xQueueReceive(oled_queue_handle, &message,
                          portMAX_DELAY) != pdPASS) {
            continue;
        }

        switch (message.sensor_type) {
        case SENSOR_MPU6050:
            latest_mpu = message;
            have_mpu = 1;
            break;

        case SENSOR_BME280:
            latest_bme = message;
            have_bme = 1;
            break;

        default:
            continue;
        }

        /* Redraw both halves using their latest available readings. */
        ssd1306_Fill(Black);
        ssd1306_Line(60, 0, 60, 63, White);

        oled_write_line(0, 0, "BME280");
        oled_write_line(66, 0, "MPU6050");

        /* Left half: BME280. */
        if (have_bme) {
            oled_write_line(0, 8, "Temp (C)");
            oled_write_value(0, 16, "%7.2f",
                             latest_bme.temperature_c);

            oled_write_line(0, 24, "Pres(hPa)");
            oled_write_value(0, 32, "%7.2f",
                             latest_bme.pressure_pa / 100.0);

            oled_write_line(0, 40, "Hum (%RH)");
            oled_write_value(0, 48, "%7.2f",
                             latest_bme.humidity_rh);
        } else {
            oled_write_line(0, 16, "Waiting");
        }

        /* Right half: MPU6050. */
        if (have_mpu) {
        	oled_write_value(66, 8,  "Ax:%+.2f", latest_mpu.ax_g);
        	oled_write_value(66, 16, "Ay:%+.2f", latest_mpu.ay_g);
        	oled_write_value(66, 24, "Az:%+.2f", latest_mpu.az_g);

        	oled_write_value(66, 32, "Gx:%+.1f", latest_mpu.gx_dps);
        	oled_write_value(66, 40, "Gy:%+.1f", latest_mpu.gy_dps);
        	oled_write_value(66, 48, "Gz:%+.1f", latest_mpu.gz_dps);

        	oled_write_value(66, 56, "T:%.1fC", latest_mpu.temperature_c);

        } else {
            oled_write_line(66, 16, "Waiting");
        }

        ssd1306_UpdateScreen();

    }
}

