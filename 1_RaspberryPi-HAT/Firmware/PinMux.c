/*
 **
 ** Source file generated on November 26, 2014 at 13:02:50.	
 **
 ** Copyright (C) 2014 Analog Devices Inc., All Rights Reserved.
 **
 ** This file is generated automatically based upon the options selected in 
 ** the Pin Multiplexing configuration editor. Changes to the Pin Multiplexing
 ** configuration should be made by changing the appropriate options rather
 ** than editing this file.
 **
 ** Selected Peripherals
 ** --------------------
 ** UART0 (Tx, Rx)
 **
 ** GPIO (unavailable)
 ** ------------------
 ** P0_06, P0_07
 */

#include "device.h"

//Use standard output on P0.6 and P0.7 for Debug interface on HAT
//#define UART0_TX_PORTP0_MUX  ((uint16_t) ((uint16_t) 2<<12))
//#define UART0_RX_PORTP0_MUX  ((uint16_t) ((uint16_t) 2<<14))

//Use alternate output on P3.7 amd P3.6 for RPi connector on HAT
#define UART0_ALT_TX_PORTP3_MUX  ((uint16_t) ((uint16_t) 1<<12))
#define UART0_ALT_RX_PORTP3_MUX  ((uint16_t) ((uint16_t) 1<<14))

#define I2C_SCL_PORTP3_MUX  ((uint16_t) ((uint16_t) 1<<8))
#define I2C_SDA_PORTP3_MUX  ((uint16_t) ((uint16_t) 1<<10))

int32_t adi_initpinmux(void);

/*
 * Initialize the Port Control MUX Registers
 */
int32_t adi_initpinmux(void) {
    /* Port Control MUX registers */
    //Use standard output on P0.6 and P0.7 for Debug interface on HAT
//    *((volatile uint32_t *)REG_GPIO0_GPCON) = UART0_TX_PORTP0_MUX | UART0_RX_PORTP0_MUX;
    
    //Use alternate output on P3.7 amd P3.6 for RPi connector on HAT
    *((volatile uint32_t *)REG_GPIO3_GPCON) = I2C_SCL_PORTP3_MUX | I2C_SDA_PORTP3_MUX
     | UART0_ALT_TX_PORTP3_MUX | UART0_ALT_RX_PORTP3_MUX;
  //*((volatile uint32_t *)REG_GPIO3_GPCON) = UART0_ALT_TX_PORTP3_MUX | UART0_ALT_RX_PORTP3_MUX;
    
    return 0;
}
