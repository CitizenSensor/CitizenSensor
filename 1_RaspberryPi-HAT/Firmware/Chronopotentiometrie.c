/********************************************************************************
This work presents a Firmware for the CitizenSensor ADuCM350 Pi HAT
for providing an electrochemical interface to Citizen Scientists.
It was developed by FabLab Munich and Fraunhofer EMFT and will be published 
under GPL 3.0 License.
*********************************************************************************/

/*********************************************************************************

Copyright FabLab München e.V. and Fraunhofer EMFT 2019-2020

*********************************************************************************/

/*****************************************************************************
 * @file:    CitizenSensor.c
 * @brief:   In this file, impedance measurement, cyclic voltammetry and 
             open circuit potentiometrie are implemented.
 *****************************************************************************/

#include <stdio.h>
#include "arm_math.h"

#include <stddef.h> 
#include <string.h>
#include <stdint.h>

#include "test_common.h"

#include "afe.h"
#include "afe_lib.h"
#include "uart.h"
#include "dma.h"
#include "i2c.h"

#include "bme680.h"

 /* Size of Tx and Rx buffers */
#define RX_BUFFER_SIZE     2
#define TX_BUFFER_SIZE     100

/* Size limit for each measurement */
#define RESULTS_BUFFER_SIZE             (16384)

#define MSG_MAXLEN                  (64)
   
#define PRINT(s)                    test_print(s)

uint32_t samplecount;

 /* Rx and Tx buffers */
static uint8_t RxBuffer[RX_BUFFER_SIZE];
static uint8_t TxBuffer[TX_BUFFER_SIZE];

/* Definitions for BME280 I2C communication */
#define MASTER_CLOCK 400000
#define GUARD_BYTE (0xa5u)
#define MAXBYTES 128
ADI_I2C_DEV_HANDLE hI2cDevice;
struct bme680_dev gas_sensor;
/* allocate oversized buffers with guard data */
uint8_t overtx[MAXBYTES+2u];  /*!< Overallocated transmit data buffer with guard data */
uint8_t overrx[MAXBYTES+2u];  /*!< Overallocated receive data buffer with guard data */
/* the "usable" buffers within the overallocated buffers housing the guard data */
uint8_t* tx = &overtx[1];  /*!< Transmit data buffer */
uint8_t* rx = &overrx[1];  /*!< Receive data buffer */

/* Sequence for Impedance Measurement */
uint32_t seq_afe_impedance[] = {
    19 << 16 | 0x43, /*  0 - Safety Word, Command Count = 15, CRC = xxx  */
    0x84005818,   /* 01 - AFE_FIFO_CFG: DATA_FIFO_SOURCE_SEL = 10 */
    0x8A000034,   /* 02 - AFE_WG_CFG: TYPE_SEL = 10 */
    0x98000000,   /* 03 - AFE_WG_CFG: SINE_FCW = 0 (placeholder, user programmable) */
    0x9E000000,   /* 04 - AFE_WG_AMPLITUDE: SINE_AMPLITUDE = 0 (placeholder, user programmable) */
    0x88000F01,   /* 05 - AFE_DAC_CFG: DAC_ATTEN_EN = 1 */
    0xA0000002,   /* 06 - AFE_ADC_CFG: MUX_SEL = 00010, GAIN_OFFS_SEL = 00 */
    /* RCAL */      
    0x86008811,   /* 07 - DMUX_STATE = 1, PMUX_STATE = 1, NMUX_STATE = 8, TMUX_STATE = 8 */
    0x00000640,   /* 08 - Wait 100us */
    0x80024EF0,   /* 09 - AFE_CFG: WAVEGEN_EN = 1 */
    0x00000C80,   /* 10 - Wait 200us */
    0x8002CFF0,   /* 11 - AFE_CFG: ADC_CONV_EN = 1, DFT_EN = 1 */
    0x00032340,   /* 12 - Wait 13ms */
    0x80024EF0,   /* 13 - AFE_CFG: ADC_CONV_EN = 0, DFT_EN = 0 */
    /* 4-Wire AFE 3, 4, 5, 6 */
    0x86003456,   /* 14 - DMUX_STATE = 6, PMUX_STATE = 5, NMUX_STATE = 4, TMUX_STATE = 3 */
    0x00000640,   /* 15 - Wait 100us */
    0x8002CFF0,   /* 16 - AFE_CFG: ADC_CONV_EN = 1, DFT_EN = 1 */
    0x00032340,   /* 17 - Wait 13ms */
    0x80020EF0,   /* 18 - AFE_CFG: WAVEGEN_EN = 0, ADC_CONV_EN = 0, DFT_EN = 0 */
    0x82000002,   /* 19 - AFE_SEQ_CFG: SEQ_EN = 0 */
};
extern uint32_t seq_afe_auxmeas[];
/* Function prototypes */
extern int32_t          adi_initpinmux          (void)                                                                  ;
extern void             test_print              (char *pBuffer)                                                         ;
float                   fabsolute               (float value_in)                                                        ;
void                    init_bme680             (void)                                                                  ;
void                    method_bme680           (void)                                                                  ;
void                    timer_ms                (uint32_t n_ms)                                                         ;
int8_t                  user_i2c_read           (uint8_t dev_id, uint8_t reg_addr, uint8_t *reg_data, uint16_t len)     ;
int8_t                  user_i2c_write          (uint8_t dev_id, uint8_t reg_addr, uint8_t *reg_data, uint16_t len)     ;
ADI_UART_RESULT_TYPE    uart_Init               (void);
ADI_AFE_DEV_HANDLE      hAfeDevice;
extern ADI_UART_HANDLE     hUartDevice;

int main(void) {
    ADI_UART_RESULT_TYPE uartResult;
    int16_t  rxSize;
    int16_t  mode = 0; 
    
    /* Flag which indicates whether to stop the program */
    _Bool bStopFlag = false;
    
    /* Initialize system */
    SystemInit();

    /* NVIC initialization */
    NVIC_SetPriorityGrouping(12);
    
    /* Change the system clock source to HFXTAL and change clock frequency to 16MHz     */
    /* Requirement for AFE (ACLK)                                                       */
    SystemTransitionClocks(ADI_SYS_CLOCK_TRIGGER_MEASUREMENT_ON);

    /* SPLL with 32MHz used, need to divide by 2 */
    SetSystemClockDivider(ADI_SYS_CLOCK_UART, 2);
    
    /* Test initialization */
    test_Init();

    /* initialize static pinmuxing */
    adi_initpinmux();

    /* Initialize the UART for transferring measurement data out */
    if (ADI_UART_SUCCESS != uart_Init())
    {
        FAIL("uart_Init");
    }
    
    PRINT("\n----------------------\n");    
    PRINT("Type:\n p for Open Circuit Potentiometry\n");
    PRINT(" i for Impedance Measurement\n e for BME680 measurement\n followed by a CR\n");
    PRINT("----------------------\n");
    timer_ms(100);
    
    /* UART processing loop */
    while(1)
    {
      switch (bStopFlag)
      {
        case false:
           rxSize = 2;
           /* Read a character */
           uartResult = adi_UART_BufRx(hUartDevice, RxBuffer, &rxSize);
           if(RxBuffer[0] == 'p' && RxBuffer[1] == '\r' )  // Open Circuit Potentiometry
           {
             mode = 2;
//             PRINT("MODE 2: Open Circuit Potentiometry\n");
//             timer_ms(100);
             init_opencircuit(hAfeDevice);
             /* Stop the program upon receiving carriage return */
             bStopFlag = true;      
           }
           else if(RxBuffer[0] == 'i' && RxBuffer[1] == '\r' )  // Open Circuit Potentiometry
           {
             mode = 3;
             PRINT("MODE 3: Impedance Measurement\n");
             timer_ms(100);
             init_impedance(hAfeDevice);
             /* Stop the program upon receiving carriage return */
             bStopFlag = true;      
           }
           else if(RxBuffer[0] == 'e' && RxBuffer[1] == '\r' )  // BME680
           {
             mode = 4;
             PRINT("MODE 4: BME680 Measurement\n");
             timer_ms(100);
             init_bme680();
             /* Stop the program upon receiving carriage return */
             bStopFlag = true;      
           }
           else {
//             PRINT("Type:\n p for Open Circuit Potentiometry\n i for Impedance Measurement\n e for BME680 measurement\n followed by a CR\n");
    }
           adi_UART_BufFlush(hUartDevice);
//           PRINT("Completed Initialization\n\n");
           timer_ms(100);
        break;
        
        case true:
           if(mode == 2) {  // OCP
             method_opencircuit(hAfeDevice, seq_afe_auxmeas);
             bStopFlag = false;
           }
           else if(mode == 3) {  // EIS
             method_impedance(hAfeDevice, seq_afe_impedance);
             bStopFlag = false;
           }
           else if(mode == 4) {  // BME680
             method_bme680();
             bStopFlag = false;
           }
           else {
             PRINT("no mode chosen\n");
             bStopFlag = false;
           } 
//           timer_ms(100);           
        break;
      }
    } 
}

/* Initialize the UART, set the baud rate and enable */
ADI_UART_RESULT_TYPE uart_Init (void) {
      
    /* UART return code */
    ADI_UART_RESULT_TYPE uartResult;
    ADI_UART_INIT_DATA   initData;
    ADI_UART_GENERIC_SETTINGS_TYPE  Settings;
    /*
     * Initialize UART
     */
    initData.pRxBufferData = RxBuffer;
    initData.RxBufferSize = RX_BUFFER_SIZE;
    initData.pTxBufferData = TxBuffer;
    initData.TxBufferSize = TX_BUFFER_SIZE;
     /* Open UART driver */
    uartResult = adi_UART_Init(ADI_UART_DEVID_0, &hUartDevice, &initData);
    if (ADI_UART_SUCCESS != uartResult)
    {
        test_Fail("adi_UART_Init() failed");
    }
     Settings.BaudRate = ADI_UART_BAUD_9600;
    Settings.bBlockingMode = true;
    Settings.bInterruptMode = true;
    Settings.Parity = ADI_UART_PARITY_NONE;
    Settings.WordLength = ADI_UART_WLS_8;
    Settings.bDmaMode = false;
          
    /* config UART */
    uartResult =  adi_UART_SetGenericSettings(hUartDevice, &Settings);
    if (ADI_UART_SUCCESS != uartResult)
    {
        test_Fail("adi_UART_SetGenericSettings() failed");
    }
     /* enable UART */
    uartResult = adi_UART_Enable(hUartDevice, true);
    if (ADI_UART_SUCCESS != uartResult)
    {
        test_Fail("adi_UART_Enable(true) failed");
    }
     return uartResult;
}

/* Initialization of BME680 Measurement */
void init_bme680(void)
{
    uint16_t bytesRemaining;
    /* Take HCLK/PCLK down to 1MHz for better power utilization */
    /* Need to set PCLK frequency first, because HCLK frequency */
    /* needs to be greater than or equal to the PCLK frequency  */
    /* at all times.                                            */
    //SetSystemClockDivider(ADI_SYS_CLOCK_PCLK, 16);
    //SetSystemClockDivider(ADI_SYS_CLOCK_CORE, 16);
  
    overtx[0] = GUARD_BYTE;
    overrx[0] = GUARD_BYTE;
    
    overtx[MAXBYTES+1u] = GUARD_BYTE;
    overrx[MAXBYTES+1u] = GUARD_BYTE;
    
    for (unsigned int i = 0u; i < MAXBYTES; i++) {
        tx[i] = (unsigned char)i;
    }
  
    /* Initialize I2C driver */
    if (ADI_I2C_SUCCESS != adi_I2C_MasterInit(ADI_I2C_DEVID_0, &hI2cDevice)) {
        FAIL("adi_I2C_MasterInit");
    }    
    
    /* select serial bit rate (~30 kHz max with 1MHz pclk)*/
    if (ADI_I2C_SUCCESS != adi_I2C_SetMasterClock(hI2cDevice, MASTER_CLOCK)) {
        FAIL("adi_I2C_SetMasterClock");
    }

    /* disable blocking mode... i.e., poll for completion */
    if (ADI_I2C_SUCCESS != adi_I2C_SetBlockingMode(hI2cDevice, false)) {
        FAIL("adi_I2C_SetBlockingMode");
    }
    
    /* TESTING AREA */
    
    adi_I2C_MasterTransmit(hI2cDevice, 0X76, 0xD0, 1, tx, 1, false);
    adi_I2C_MasterReceive(hI2cDevice,
                          0x76,         //slaveID,
                          0xD0,         //dataAddress,
                          1,            //dataAddressWidth,
                          rx,           //uint8_t      *pBuffer,
                          1,            //uint16_t const nBufferSize,
                          false);       //bRepeatStart)
    bytesRemaining = 1;                 //bytecount;
    while (bytesRemaining) {
        adi_I2C_GetNonBlockingStatus(hI2cDevice, &bytesRemaining);
    }
    
    /* TESTING AREA */
    
    gas_sensor.dev_id = BME680_I2C_ADDR_PRIMARY;
    gas_sensor.intf = BME680_I2C_INTF;
    gas_sensor.read = user_i2c_read;
    gas_sensor.write = user_i2c_write;
    gas_sensor.delay_ms = timer_ms;
    /* amb_temp can be set to 25 prior to configuring the gas sensor 
     * or by performing a few temperature readings without operating the gas sensor.
     */
    gas_sensor.amb_temp = 25;


    int8_t rslt = BME680_OK;
    rslt = bme680_init(&gas_sensor);
    
    uint8_t set_required_settings;

    /* Set the temperature, pressure and humidity settings */
    gas_sensor.tph_sett.os_hum = BME680_OS_2X;
    gas_sensor.tph_sett.os_pres = BME680_OS_4X;
    gas_sensor.tph_sett.os_temp = BME680_OS_8X;
    gas_sensor.tph_sett.filter = BME680_FILTER_SIZE_3;

    /* Set the remaining gas sensor settings and link the heating profile */
    gas_sensor.gas_sett.run_gas = BME680_ENABLE_GAS_MEAS;
    /* Create a ramp heat waveform in 3 steps */
    gas_sensor.gas_sett.heatr_temp = 320; /* degree Celsius */
    gas_sensor.gas_sett.heatr_dur = 150; /* milliseconds */

    /* Select the power mode */
    /* Must be set before writing the sensor configuration */
    gas_sensor.power_mode = BME680_FORCED_MODE; 

    /* Set the required sensor settings needed */
    set_required_settings = BME680_OST_SEL | BME680_OSP_SEL | BME680_OSH_SEL | BME680_FILTER_SEL 
        | BME680_GAS_SENSOR_SEL;

    /* Set the desired sensor configuration */
    rslt = bme680_set_sensor_settings(set_required_settings,&gas_sensor);

    /* Set the power mode */
    rslt = bme680_set_sensor_mode(&gas_sensor);
}

/* Method to measure BME680 */
void method_bme680(void)
{
    char msg[MSG_MAXLEN];
    
    /* Perform the BME680 measurement */
    struct bme680_field_data data;
    
    int8_t rslt = bme680_get_sensor_data(&data, &gas_sensor);

    /* Print final results */
    PRINT("BME680 results (T, p, rh, gas):\r\n");
    sprintf(msg, "T: %.2f degC, P: %.2f hPa, H %.2f %%rH ", data.temperature / 100.0f,
      data.pressure / 100.0f, data.humidity / 1000.0f );
    /* Avoid using measurements from an unstable heating setup */
    if(data.status & BME680_GASM_VALID_MSK)
    {
      char msg1[MSG_MAXLEN];
      sprintf(msg1, ", G: %d ohms", data.gas_resistance);
      strcat(msg, msg1);
    }

    strcat(msg,"\n");
    PRINT(msg);
    timer_ms(100);
    
    /* shut it down */
    if (ADI_I2C_SUCCESS != adi_I2C_UnInit(hI2cDevice)) {
        FAIL("adi_I2C_UnInit");
    }
}

/* Function to calculate the absolute value of a float */
float fabsolute (float value_in)
{
  if (value_in < 0)
    return -value_in;
  else
    return value_in;
}

/* Function to approx wait for ms */
void timer_ms(uint32_t n_ms)
{
  for (int i = 0; i < (n_ms * 2035 * 16 / 50 + 1); i++)
    ;
}

int8_t user_i2c_read(uint8_t dev_id, uint8_t reg_addr, uint8_t *reg_data, uint16_t len)
{
    int8_t rslt = 0; /* Return 0 for Success, non-zero for failure */
    uint16_t bytesRemaining;
    /*
     * The parameter dev_id can be used as a variable to store the I2C address of the device
      * @brief           Read operation in either I2C or SPI
      *
      * param[in]        dev_addr        I2C or SPI device address
      * param[in]        reg_addr        register address
      * param[out]       reg_data_ptr    pointer to the memory to be used to store the read data
      * param[in]        data_len        number of bytes to be read
      *
      * @return          result of the bus communication function
     */
    adi_I2C_MasterReceive(hI2cDevice, dev_id, reg_addr, ADI_I2C_8_BIT_DATA_ADDRESS_WIDTH, reg_data, len, false);
    
    bytesRemaining = 1;                 //bytecount;
    while (bytesRemaining) {
        adi_I2C_GetNonBlockingStatus(hI2cDevice, &bytesRemaining);
    }
    /*
     * Data on the bus should be like
     * |------------+---------------------|
     * | I2C action | Data                |
     * |------------+---------------------|
     * | Start      | -                   |
     * | Write      | (reg_addr)          |
     * | Stop       | -                   |
     * | Start      | -                   |
     * | Read       | (reg_data[0])       |
     * | Read       | (....)              |
     * | Read       | (reg_data[len - 1]) |
     * | Stop       | -                   |
     * |------------+---------------------|
     */

    return rslt;
}

int8_t user_i2c_write(uint8_t dev_id, uint8_t reg_addr, uint8_t *reg_data, uint16_t len)
{
    int8_t rslt = 0; /* Return 0 for Success, non-zero for failure */

    /*
     * The parameter dev_id can be used as a variable to store the I2C address of the device
      * @brief           Write operation in either I2C or SPI
      *
      * param[in]        dev_addr        I2C or SPI device address
      * param[in]        reg_addr        register address
      * param[in]        reg_data_ptr    pointer to the data to be written
      * param[in]        data_len        number of bytes to be written
      *
      * @return          result of the bus communication function
     */
    
    adi_I2C_MasterTransmit(hI2cDevice, dev_id, reg_addr, ADI_I2C_8_BIT_DATA_ADDRESS_WIDTH, reg_data, len, false);

    
    /*
     * Data on the bus should be like
     * |------------+---------------------|
     * | I2C action | Data                |
     * |------------+---------------------|
     * | Start      | -                   |
     * | Write      | (reg_addr)          |
     * | Write      | (reg_data[0])       |
     * | Write      | (....)              |
     * | Write      | (reg_data[len - 1]) |
     * | Stop       | -                   |
     * |------------+---------------------|
     */

    return rslt;
}
