/********************************************************************************
This work presents a Firmware for the CitizenSensor ADuCM350 Pi HAT
for providing an electrochemical interface to Citizen Scientists.
It was developed by FabLab Munich and Fraunhofer EMFT and will be published 
under GPL 3.0 License. It bases on a Firmware example provided by ADI,
called "TrapezoidGen.c" and some others.
*********************************************************************************/

/*********************************************************************************

Copyright (c) 2014 Analog Devices, Inc. All Rights Reserved.

This software is proprietary to Analog Devices, Inc. and its licensors.  By using
this software you agree to the terms of the associated Analog Devices Software
License Agreement.

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

/* Macro to enable the returning of AFE data using the UART */
/*      1 = return AFE data on UART                         */
/*      0 = return AFE data on SW (Std Output)              */
#define USE_UART_FOR_DATA           (1)

/* Helper macro for printing strings to UART or Std. Output */
#define PRINT(s)                    test_print1(s)

 /* Size of Tx and Rx buffers */
#define RX_BUFFER_SIZE     2
#define TX_BUFFER_SIZE     100

/* RCAL value, in ohms                                              */
/* Default value on ADuCM350 Switch Mux Config Board Rev.0 is 1k    */
#define RCAL                        (1000)
/* RTIA value, in ohms                                              */
/* Default value on ADuCM350 Switch Mux Config Board Rev.0 is 5k  */
#define RTIA                        (1000)

/* DO NOT EDIT: DAC LSB size in mV, before attenuator (1.6V / (2^12 - 1)) */
#define DAC_LSB_SIZE                (0.39072)

/* If both real and imaginary result are within the interval (DFT_RESULTS_OPEN_MIN_THR, DFT_RESULTS_OPEN_MAX_THR),  */
/* it is considered an open circuit and results for both magnitude and phase will be 0.                             */
#define DFT_RESULTS_OPEN_MAX_THR    (10)
#define DFT_RESULTS_OPEN_MIN_THR    (-10)

/* The number of results expected from the DFT, in this case 8 for 4 complex results */
#define DFT_RESULTS_COUNT           (4)

/* Fractional LSB size for the fixed32_t type defined below, used for printing only. */
#define FIXED32_LSB_SIZE            (625)

/* Settings for OpenCircuiPotentiometry */
/* The duration (in us) to measure from the auxilliary channel */
#define DUR                        ((uint32_t)(11125))

/* Select Aux Channel: AN_A, AN_B, AN_C, AN_D */
#define ADC_MUX_SEL                 (ADI_AFE_ADC_MUX_SEL_AN_A)

/* Enable/Disable ANEXCITE switch (0 = disable, 1 = enable) */
#define ANEXCITESW_EN               (1)

/* Select ADC Gain and Offset (TIA, Aux, Temp Sense, Aux) */
#define ADC_GAIN_OFFS               (ADI_AFE_ADC_GAIN_OFFS_AUX)

/* DO NOT EDIT: macro to set the value of the AFE_ADC_CFG register      */
/*              based on inputs above.                                  */
#define ADC_CFG_REG_DATA            ((ADC_MUX_SEL << BITP_AFE_AFE_ADC_CFG_MUX_SEL) +\
                                     (ANEXCITESW_EN << BITP_AFE_AFE_ADC_CFG_ANEXCITESW_EN) +\
                                     (ADC_GAIN_OFFS << BITP_AFE_AFE_ADC_CFG_GAIN_OFFS_SEL))

/* DO NOT EDIT: Calculate number of samples for sequence size           */
/* SAMPLE_COUNT = (Duration)us * (160k/178)samples/s                    */
#define SAMPLE_COUNT                (uint32_t)((2 * DUR) / 2225)


/* Size limit for each DMA transfer (max 1024) */
#define DMA_BUFFER_SIZE             (5)

/* Size limit for each measurement */
#define RESULTS_BUFFER_SIZE             (16384)

/* DO NOT EDIT: Maximum printed message length. Used for printing only. */
#define MSG_MAXLEN                  (64)

#pragma location="volatile_ram"
uint16_t        dmaBuffer[DMA_BUFFER_SIZE * 2];
uint32_t samplecount;

/* Custom fixed-point type used for final results,              */
/* to keep track of the decimal point position.                 */
/* Signed number with 28 integer bits and 4 fractional bits.    */
typedef union {
    int32_t     full;
    struct {
        uint8_t fpart:4;
        int32_t ipart:28;
    } parts;
} fixed32_t;

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

/* Sequence for Cyclic Voltammetry */
uint32_t seq_afe_cyclovoltammetry[] = {
    20 << 16 | 0x43, /*  0 - Safety Word, Command Count = 15, CRC = xxx                                   */
    0x84007818, /*  1 - AFE_FIFO_CFG: DATA_FIFO_SOURCE_SEL = 0b11 (LPF)*/
    0x8A000037, /*  2 - AFE_WG_CFG: TYPE_SEL = 0b11, trapzoid wave, reset = off                           */
    0x88000F00, /*  3 - AFE_DAC_CFG: DAC_ATTEN_EN = 0 (disable DAC attenuator)                            */
    0x86006543, /*  4 - DMUX_STATE = 3, PMUX_STATE = 4, NMUX_STATE = 5, TMUX_STATE = 6                    */
    0x00000000, /*  5 - DC LEVEL 1*/
    0x00000000, /*  6 - DELAY 1*/
    0x00000C80, /*  7 - delay some time, because sequencer runs so fast*/
    0x00000000, /*  8 -  SLOPE 1*/
    0x00000000, /*  9 -  DC LEVEL 2*/
    0x00000000, /*  10 -  DELAY 2*/
    0x00000000, /*  11 -  SLOPE 2*/
    0xA0000002, /*  12 - AFE_ADC_CFG: MUX_SEL = 0b00010, GAIN_OFFS_SEL = 0b00 (TIA)                        */    
    0xA2000000, /*  13 - AFE_SUPPLY_LPF_CFG: BYPASS_SUPPLY_LPF = 0 (do not bypass)                         */
    0x00109A00, /*  14 - Wait: 68ms (based on load RC = 6.8kOhm * 10uF)                                    */
    0x80024EF0, /*  15 - AFE_CFG: WG_EN = 1                                                                */
    0x00000C80, /*  16 - Wait: 200us                                                                       */
    0x80034FF0, /*  17 - AFE_CFG: ADC_CONV_EN = 1, SUPPLY_LPF_EN = 1                                       */
    0x00000000, /*  18 - Wait: DELAY1 + SLOPE1 + DELAY2 + SLOPE2                                           */
    0x80020EF0, /*  19 - AFE_CFG: WAVEGEN_EN = 0, ADC_CONV_EN = 0, SUPPLY_LPF_EN = 0                       */
    0x82000002, /*  20 - AFE_SEQ_CFG: SEQ_EN = 0                                                           */
};

/* Sequence for Open Circuit Potentiometry */
uint32_t seq_afe_opencircuit[] = {
    0x000A0009,   /*  0 - Safety Word, Command Count = 15, CRC = xxx                        */
    0x84007818,   /*  1 - AFE_FIFO_CFG: DATA_FIFO_SOURCE_SEL = 0b11 (LPF)                   */
    0xA0000229,   /*  2 - AFE_ADC_CFG: MUX_SEL = 0b01001 (AN_B), GAIN_OFFS_SEL = 0b10 (AUX) */
    0xA2000000,   /*  3 - AFE_SUPPLY_LPF_CFG: BYPASS_SUPPLY_LPF = 0                         */
    0x00000640,   /*  4 - Wait: 100us                                                       */
    0x80034FF4,   /*  5 - AFE_CFG: ADC_CONV_EN = 1, SUPPLY_LPF_EN = 1                       */
    0x00090880,   /*  6 - Wait: 37ms for LPF settling                                       */
    0x00000000,   /*  7 - Wait: DUR (placeholder, user programmable)                        */
    0x80020EF4,   /*  8 - AFE_CFG: WAVEGEN_EN = 0, ADC_CONV_EN = 0, SUPPLY_LPF_EN = 0       */
    0xA0000200,   /*  9 - AFE_ADC_CFG: MUX_SEL = 0b00000, GAIN_OFFS_SEL = 0b10 (AUX)        */
    0x82000002,   /* 10 - AFE_SEQ_CFG: SEQ_EN = 0                                           */
};

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

/* Variables and functions needed for data output through UART */
ADI_UART_HANDLE     hUartDevice     = NULL;

/* Function prototypes */
void                    test_print1             (char *pBuffer)                                                         ;
ADI_UART_RESULT_TYPE    uart_Init               (void)                                                                  ;
ADI_UART_RESULT_TYPE    uart_UnInit             (void)                                                                  ;
extern int32_t          adi_initpinmux          (void)                                                                  ;
void                    RxDmaCB                 (void *hAfeDevice, uint32_t length, void *pBuffer)                      ;
float                   fabsolute               (float value_in)                                                        ;
void                    init_cyclovoltammetry   (void)                                                                  ;
void                    method_cyclovoltammetry (ADI_AFE_DEV_HANDLE hDevice, const uint32_t *const seq)                 ;
void                    init_opencircuit        (void)                                                                  ;
void                    method_opencircuit      (ADI_AFE_DEV_HANDLE hDevice, const uint32_t *const seq)                 ;
void                    init_impedance          (void)                                                                  ;
void                    method_impedance        (ADI_AFE_DEV_HANDLE hDevice, const uint32_t *const seq)                 ;
void                    init_bme680             (void)                                                                  ;
void                    method_bme680           (void)                                                                  ;
void                    timer_ms                (uint32_t n_ms)                                                         ;
q15_t                   arctan                  (q15_t imag, q15_t real)                                                ;
fixed32_t               calculate_magnitude     (q31_t magnitude_rcal, q31_t magnitude_z)                               ;
fixed32_t               calculate_phase         (q15_t phase_rcal, q15_t phase_z)                                       ;
void                    convert_dft_results     (int16_t *dft_results, q15_t *dft_results_q15, q31_t *dft_results_q31)  ;
void                    sprintf_fixed32         (char *out, fixed32_t in)                                               ;
void                    print_MagnitudePhase    (char *text, fixed32_t magnitude, fixed32_t phase)                      ;
int8_t                  user_i2c_read           (uint8_t dev_id, uint8_t reg_addr, uint8_t *reg_data, uint16_t len)     ;
int8_t                  user_i2c_write          (uint8_t dev_id, uint8_t reg_addr, uint8_t *reg_data, uint16_t len)     ;

ADI_AFE_DEV_HANDLE      hAfeDevice;

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

    /* Take HCLK/PCLK down to 1MHz for better power utilization */
    /* Need to set PCLK frequency first, because HCLK frequency */
    /* needs to be greater than or equal to the PCLK frequency  */
    /* at all times.                                            */
    //SetSystemClockDivider(ADI_SYS_CLOCK_PCLK, 32);
    //SetSystemClockDivider(ADI_SYS_CLOCK_CORE, 32);
    
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
    PRINT("Type:\n c for Cyclic Voltammetry\n p for Open Circuit Potentiometry\n");
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
           if(RxBuffer[0] == 'c' && RxBuffer[1] == '\r' )  // Cyclovoltammetry
           {
             mode = 1;
             PRINT("MODE 1: Cyclovoltammetry\n");
             timer_ms(100);
             init_cyclovoltammetry();
             /* Stop the program upon receiving carriage return */
             bStopFlag = true;      
           }
           else if(RxBuffer[0] == 'p' && RxBuffer[1] == '\r' )  // Open Circuit Potentiometry
           {
             mode = 2;
//             PRINT("MODE 2: Open Circuit Potentiometry\n");
//             timer_ms(100);
             init_opencircuit();
             /* Stop the program upon receiving carriage return */
             bStopFlag = true;      
           }
           else if(RxBuffer[0] == 'i' && RxBuffer[1] == '\r' )  // Open Circuit Potentiometry
           {
             mode = 3;
             PRINT("MODE 3: Impedance Measurement\n");
             timer_ms(100);
             init_impedance();
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
//             PRINT("Type:\n c for Cyclic Voltammetry\n p for Open Circuit Potentiometry\n i for Impedance Measurement\n e for BME680 measurement\n followed by a CR\n");
    }
           adi_UART_BufFlush(hUartDevice);
//           PRINT("Completed Initialization\n\n");
           timer_ms(100);
        break;
        
        case true:
           if (mode == 1) {  // CV
             method_cyclovoltammetry(hAfeDevice, seq_afe_cyclovoltammetry);
             bStopFlag = false;
           }
           else if(mode == 2) {  // OCP
             method_opencircuit(hAfeDevice, seq_afe_opencircuit);
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
//           PRINT("\n----------------------\n");    
//           PRINT("Press:\n a for Cyclic Voltammetry\n b for Open Circuit Potentiometry\n c for Impedance Measurement\n");
//           PRINT("----------------------\n");
//           timer_ms(100);
        break;
      }
    } 
}


/*!
 * @brief       AFE Rx DMA Callback Function.
 *
 * @param[in]   hAfeDevice  Device handle obtained from adi_AFE_Init()
 *              length      Number of U16 samples received from the DMA
 *              pBuffer     Pointer to the buffer containing the LPF results
 *              
 *
 * @details     16-bit results are converted to bytes and transferred using the UART
 *
 */
void RxDmaCB(void *hAfeDevice, uint32_t length, void *pBuffer)
{
#if (1 == USE_UART_FOR_DATA)
    char                    msg[MSG_MAXLEN];
    uint32_t                i;
    uint16_t                *ppBuffer = (uint16_t*)pBuffer;

    adi_AFE_ProgramRxDMA(hAfeDevice,dmaBuffer,DMA_BUFFER_SIZE);
    ADI_ENABLE_INT(DMA_AFE_RX_IRQn);
    /* Check if there are samples to be sent */
//    if (length)
//    {
//        for (i = 0; i < length; i++)
//        {
//            sprintf(msg, "%u\r\n", *ppBuffer++);
//            PRINT(msg);
//        }
//        timer_ms(100);
//    }
#elif (0 == USE_UART_FOR_DATA)
    FAIL("Std. Output is too slow for ADC/LPF data. Use UART instead.");
    
#endif /* USE_UART_FOR_DATA */
}
/* Helper function for printing a string to UART or Std. Output */
void test_print1 (char *pBuffer) {
#if (1 == USE_UART_FOR_DATA)
    int16_t size;
    /* Print to UART */
    size = strlen(pBuffer);
    adi_UART_BufTx(hUartDevice, pBuffer, &size);

#elif (0 == USE_UART_FOR_DATA)
    /* Print  to console */
    printf(pBuffer);

#endif /* USE_UART_FOR_DATA */
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

/* Uninitialize the UART */
ADI_UART_RESULT_TYPE uart_UnInit (void) {
    ADI_UART_RESULT_TYPE    result = ADI_UART_SUCCESS;

    /* Uninitialize the UART API */
    if (ADI_UART_SUCCESS != (result = adi_UART_UnInit(hUartDevice)))
    {
        return result;
    }

    return result;
}

/* Initialize the cyclovoltammetry measurement */
void init_cyclovoltammetry (void)
{
    
    /* Definitions for Cyclic Voltammetry                                         */
    /******************************************************************************/
    /*                       __________________            <-DC Level2            */
    /*                      /                  \                                  */
    /*                     /                    \                                 */
    /*                    /                      \                                */
    /*                   /                        \                               */
    /*  ________________/                          \__________________<-DC Level1 */
    /*                                                                            */
    /*  <--- Delay1 --><Slope1><-- Delay2 --><Slope2><--- Delay1 ---->            */
    /******************************************************************************/
  
    /* Variables for Cyclic Voltammetry */
    uint32_t dur1;               // Duration of one cycle
    uint32_t delay1 = 0;         // delay at voltage1 in us
    uint32_t delay2 = 0;         // delay at voltage2 in us
    float voltage1 = -600;       // Vertex 1 potential (+-800 mV max)
    float voltage2 = 600;        // Vertex 2 potential (+-800 mV max)
    uint32_t scanrate = 500;     // scanrate in mV/s
    
    uint32_t dacl1 = ((uint32_t)(((float)voltage1 / (float)DAC_LSB_SIZE) + 0x800));
    uint32_t dacl2 = ((uint32_t)(((float)voltage2 / (float)DAC_LSB_SIZE) + 0x800));
    uint32_t slopev1 = ((uint32_t)(((float)fabsolute(voltage1) * (float)320000) / (float)scanrate));
    uint32_t slopev2 = ((uint32_t)(((float)fabsolute(voltage2) * (float)320000) / (float)scanrate));
    
    /* Initialize the AFE API */
    if (ADI_AFE_SUCCESS != adi_AFE_Init(&hAfeDevice))
    {
        FAIL("Init");
    }

    /* Set RCAL Value */
    if (ADI_AFE_SUCCESS != adi_AFE_SetRcal(hAfeDevice, RCAL))
    {
        FAIL("Set RCAL");
    }

    /* Set RTIA Value */
    if (ADI_AFE_SUCCESS != adi_AFE_SetRtia(hAfeDevice, RTIA))
    {
        FAIL("Set RTIA");
    }

    /* AFE power up */
    if (ADI_AFE_SUCCESS != adi_AFE_PowerUp(hAfeDevice))
    {
        FAIL("PowerUp");
    }

    /* Excitation Channel Power-Up */
    if (ADI_AFE_SUCCESS != adi_AFE_ExciteChanPowerUp(hAfeDevice))
    {
        FAIL("ExciteChanCalAtten");
    }

    /* Excitation Channel (no attenuation) Calibration */
    if (ADI_AFE_SUCCESS != adi_AFE_ExciteChanCalNoAtten(hAfeDevice))
    {
        FAIL("adi_AFE_ExciteChanCalNoAtten");
    }

    seq_afe_cyclovoltammetry[5]  = SEQ_MMR_WRITE(REG_AFE_AFE_WG_DCLEVEL_1, dacl1);
    seq_afe_cyclovoltammetry[6] = SEQ_MMR_WRITE(REG_AFE_AFE_WG_DELAY_1, delay1);
    seq_afe_cyclovoltammetry[8] = SEQ_MMR_WRITE(REG_AFE_AFE_WG_SLOPE_1, slopev1);
    seq_afe_cyclovoltammetry[9]  = SEQ_MMR_WRITE(REG_AFE_AFE_WG_DCLEVEL_2, dacl2);
    seq_afe_cyclovoltammetry[10] = SEQ_MMR_WRITE(REG_AFE_AFE_WG_DELAY_2, delay2);
    seq_afe_cyclovoltammetry[11] = SEQ_MMR_WRITE(REG_AFE_AFE_WG_SLOPE_2, slopev2);
    
    /* Calculate time of wafeformgen in us */
    dur1 = ((uint32_t)( delay1 + delay2 + (((float)(fabsolute(voltage1) + fabsolute(voltage2)) * (float)1000000) / (float)scanrate)));
    seq_afe_cyclovoltammetry[18] = dur1 * 16;
    
//#if (ADI_AFE_CFG_ENABLE_RX_DMA_DUAL_BUFFER_SUPPORT == 1)   
//    /* Set the Rx DMA buffer sizes */
//    if (ADI_AFE_SUCCESS != adi_AFE_SetDmaRxBufferMaxSize(hAfeDevice, DMA_BUFFER_SIZE, DMA_BUFFER_SIZE))
//    {
//        FAIL("adi_AFE_SetDmaRxBufferMaxSize");
//    }
//#endif /* ADI_AFE_CFG_ENABLE_RX_DMA_DUAL_BUFFER_SUPPORT == 1 */
//    
//    /* Register Rx DMA Callback */
//    if (ADI_AFE_SUCCESS != adi_AFE_RegisterCallbackOnReceiveDMA(hAfeDevice, RxDmaCB, 0))
//    {
//        FAIL("adi_AFE_RegisterCallbackOnReceiveDMA");
//    }
        
    /* Recalculate CRC in software for the amperometric measurement */
    adi_AFE_EnableSoftwareCRC(hAfeDevice, true);
    
    samplecount = (uint32_t)((2 * (delay1 + delay2 + (((float)(fabsolute(voltage1) + fabsolute(voltage2)) * (float)1000000) / (float)scanrate))) / 2225);
    
}

/* Method to measure cyclovoltammetry */
void method_cyclovoltammetry (ADI_AFE_DEV_HANDLE hDevice, const uint32_t *const seq)
{
    uint16_t cv_results[RESULTS_BUFFER_SIZE];
    char msg[256];
    /* Perform the Amperometric measurement(s) */
    if (ADI_AFE_SUCCESS != adi_AFE_RunSequence(hDevice, seq, (uint16_t *) cv_results, samplecount))
    {
        FAIL("adi_AFE_RunSequence");
    }
    
    for (int i = 0; i < samplecount; i++)
    {
        sprintf(msg, "%i", cv_results[i]);
        strcat(msg,"\r");
        PRINT(msg);
    }
    
    PRINT("\n");
    timer_ms(100);

    /* AFE Power Down */
    if (adi_AFE_PowerDown(hAfeDevice)) 
    {
        FAIL("adi_AFE_PowerDown");
    }

    /* Uninitialize the AFE API */
    if (adi_AFE_UnInit(hAfeDevice)) 
    {
        FAIL("adi_AFE_UnInit");
    }
}

/* Initialization of Open Circuit Potentiometry */
void init_opencircuit(void)
{
     /* Initialize the AFE API */
    if (ADI_AFE_SUCCESS != adi_AFE_Init(&hAfeDevice)) 
    {
        FAIL("adi_AFE_Init");
    }

    /* AFE power up */
    if (ADI_AFE_SUCCESS != adi_AFE_PowerUp(hAfeDevice)) 
    {
        FAIL("adi_AFE_PowerUp");
    }

    /* Aux Channel Calibration */
    if (ADI_AFE_SUCCESS != adi_AFE_AuxChanCal(hAfeDevice)) 
    {
        FAIL("adi_AFE_AuxChanCal");
    }

//#if (ADI_AFE_CFG_ENABLE_RX_DMA_DUAL_BUFFER_SUPPORT == 1)   
//    /* Set the Rx DMA buffer sizes */
//    if (ADI_AFE_SUCCESS != adi_AFE_SetDmaRxBufferMaxSize(hAfeDevice, DMA_BUFFER_SIZE, DMA_BUFFER_SIZE))
//    {
//        FAIL("adi_AFE_SetDmaRxBufferMaxSize");
//    }
//#endif /* (ADI_AFE_CFG_ENABLE_RX_DMA_DUAL_BUFFER_SUPPORT == 1) */
//    
//    /* Register Rx DMA Callback */
//    if (ADI_AFE_SUCCESS != adi_AFE_RegisterCallbackOnReceiveDMA(hAfeDevice, RxDmaCB, 0))
//    {
//        FAIL("adi_AFE_RegisterCallbackOnReceiveDMA");
//    }
        
    /* Configure the AFE_ADC_CFG register */
    seq_afe_opencircuit[2] = SEQ_MMR_WRITE(REG_AFE_AFE_ADC_CFG, ADC_CFG_REG_DATA);
        
    /* Set the duration of the measurement in ACLK periods */
    seq_afe_opencircuit[7] = (DUR * 16);
    
    /* Recalculate CRC in software for the amperometric measurement */
    adi_AFE_EnableSoftwareCRC(hAfeDevice, true);

}

/* Method to measure Open Circuit Potentiometry */
void method_opencircuit (ADI_AFE_DEV_HANDLE hDevice, const uint32_t *const seq)
{
    char msg[MSG_MAXLEN];
    uint16_t oc_results;
    double value_V;
    int k;
    /* Perform the Aux Channel measurement(s) */
    if (ADI_AFE_SUCCESS != adi_AFE_RunSequence(hDevice, seq_afe_opencircuit, (uint16_t *) dmaBuffer, SAMPLE_COUNT)) 
    {
        FAIL("adi_AFE_RunSequence(seq_afe_opencircuit)");   
    }
    timer_ms(50);
    oc_results = dmaBuffer[0];
    /*for(k=1;k<10;k++)
    {
      oc_results += dmaBuffer[k];
      oc_results /= 2;
    }*/
    
    value_V = -1.79996 + 0.000054931 * (double) oc_results;
    sprintf(msg, "%i\t%2.4f\r\n", oc_results, value_V);
    
    PRINT(msg);
    /* Restore to using default CRC stored with the sequence */
    adi_AFE_EnableSoftwareCRC(hDevice, false);
    
    /* AFE Power Down */
    if (ADI_AFE_SUCCESS != adi_AFE_PowerDown(hDevice)) 
    {
        FAIL("adi_AFE_PowerDown");
    }

    /* Unregister Rx DMA Callback */
    if (ADI_AFE_SUCCESS != adi_AFE_RegisterCallbackOnReceiveDMA(hDevice, NULL, 0))
    {
        FAIL("adi_AFE_RegisterCallbackOnReceiveDMA (unregister)");
    }

    /* Uninitialize the AFE API */
    if (ADI_AFE_SUCCESS != adi_AFE_UnInit(hDevice)) 
    {
        FAIL("adi_AFE_UnInit");
    }
    
    //PASS();
}

/* Initialization of Impedance Measurement */
void init_impedance(void)
{
  uint32_t freq = 1000;        //Frequency in Hertz
  uint32_t vpeak = 600;         //Amplitude in mV
  uint32_t fcw = ((uint32_t)(((uint64_t)freq << 26) / 16000000 + 0.5)); /* FCW = FREQ * 2^26 / 16e6 */
  uint32_t sine_amp = ((uint16_t)((vpeak * 40) / DAC_LSB_SIZE + 0.5));  /* Sine amplitude in DAC codes */
  
    /* Initialize the AFE API */
  if (ADI_AFE_SUCCESS != adi_AFE_Init(&hAfeDevice))
  {
      FAIL("Init");
  }

  /* Set RCAL Value */
  if (ADI_AFE_SUCCESS != adi_AFE_SetRcal(hAfeDevice, RCAL))
  {
      FAIL("Set RCAL");
  }

  /* Set RTIA Value */
  if (ADI_AFE_SUCCESS != adi_AFE_SetRtia(hAfeDevice, RTIA))
  {
      FAIL("Set RTIA");
  }

  /* AFE power up */
  if (ADI_AFE_SUCCESS != adi_AFE_PowerUp(hAfeDevice))
  {
      FAIL("PowerUp");
  }

  /* Excitation Channel Power-Up */
  if (ADI_AFE_SUCCESS != adi_AFE_ExciteChanPowerUp(hAfeDevice))
  {
      FAIL("ExciteChanCalAtten");
  }

  /* Excitation Channel (no attenuation) Calibration */
  if (ADI_AFE_SUCCESS != adi_AFE_ExciteChanCalNoAtten(hAfeDevice))
  {
      FAIL("adi_AFE_ExciteChanCalNoAtten");
  }

  /* TIA Channel Calibration */
  if (adi_AFE_TiaChanCal(hAfeDevice)) 
  {
      FAIL("adi_AFE_TiaChanCal");
  }
  
  /* Update FCW in the sequence */
  seq_afe_impedance[3] = SEQ_MMR_WRITE(REG_AFE_AFE_WG_FCW, fcw);
  /* Update sine amplitude in the sequence */
  seq_afe_impedance[4] = SEQ_MMR_WRITE(REG_AFE_AFE_WG_AMPLITUDE, sine_amp);
  
  /* Recalculate CRC in software for the AC measurement, because we changed   */
  /* FCW and sine amplitude settings                                          */
  adi_AFE_EnableSoftwareCRC(hAfeDevice, true);
}

/* Method to measure Impedance */
void method_impedance(ADI_AFE_DEV_HANDLE hDevice, const uint32_t *const seq)
{
    int16_t             dft_results[DFT_RESULTS_COUNT];
    q15_t               dft_results_q15[DFT_RESULTS_COUNT];
    q31_t               dft_results_q31[DFT_RESULTS_COUNT];
    q31_t               magnitude[DFT_RESULTS_COUNT / 2];
    q15_t               phase[DFT_RESULTS_COUNT / 2];
    fixed32_t           magnitude_result[DFT_RESULTS_COUNT / 2 - 1];
    fixed32_t           phase_result[DFT_RESULTS_COUNT / 2 - 1];
    char                msg[MSG_MAXLEN];
    int8_t              i;  
    
    /* Perform the Impedance measurement */
    if (adi_AFE_RunSequence(hDevice, seq, (uint16_t *)dft_results, DFT_RESULTS_COUNT)) 
    {
        FAIL("Impedance Measurement");
    }

    /* Restore to using default CRC stored with the sequence */
    adi_AFE_EnableSoftwareCRC(hDevice, false);
    
    /* Print DFT complex results */
    sprintf(msg, "DFT results (real, imaginary):\r\n");
    PRINT(msg);
    sprintf(msg, "    RCAL        = (%6d, %6d)\r\n", dft_results[0], dft_results[1]);
    PRINT(msg);
    sprintf(msg, "    4-WIRE      = (%6d, %6d)\r\n", dft_results[2], dft_results[3]);
    PRINT(msg);
    timer_ms(100);
    /* Convert DFT results to 1.15 and 1.31 formats.  */
    convert_dft_results(dft_results, dft_results_q15, dft_results_q31);

    /* Magnitude calculation */
    /* Use CMSIS function */
    arm_cmplx_mag_q31(dft_results_q31, magnitude, DFT_RESULTS_COUNT / 2);

    /* Calculate final magnitude values, calibrated with RCAL. */
    for (i = 0; i < DFT_RESULTS_COUNT / 2 - 1; i++) 
    {
        magnitude_result[i] = calculate_magnitude(magnitude[0], magnitude[i + 1]);
    }

    /* Phase calculation */
    /* RCAL first */
    phase[0] = arctan(dft_results[1], dft_results[0]);
    for (i = 0; i < DFT_RESULTS_COUNT / 2 - 1; i++) 
    {
        /* No need to calculate the phase if magnitude is 0 (open circuit) */
        if (magnitude_result[i].full) 
        {
            /* First the measured phase. */
            phase[i + 1]         = arctan(dft_results[2 * (i + 1) + 1], dft_results[2 * (i + 1)]);
            /* Then the phase calibrated with RCAL. */
            phase_result[i]      = calculate_phase(phase[0], phase[i + 1]);
        }
        else 
        {
            phase[i + 1]         = 0;
            phase_result[i].full = 0;
        }
    }

    /* Print final results */
    PRINT("Final results (magnitude, phase):\r\n");
    print_MagnitudePhase("4-WIRE", magnitude_result[0], phase_result[0]);
    timer_ms(100);
    /* AFE Power Down */
    if (adi_AFE_PowerDown(hDevice)) 
    {
        FAIL("adi_AFE_PowerDown");
    }

    /* Uninitialize the AFE API */
    if (adi_AFE_UnInit(hDevice)) 
    {
        FAIL("adi_AFE_UnInit");
    }
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

/* Arctan Implementation                                                                                    */
/* =====================                                                                                    */
/* Arctan is calculated using the formula:                                                                  */
/*                                                                                                          */
/*      y = arctan(x) = 0.318253 * x + 0.003314 * x^2 - 0.130908 * x^3 + 0.068542 * x^4 - 0.009159 * x^5    */
/*                                                                                                          */
/* The angle in radians is given by (y * pi)                                                                */
/*                                                                                                          */
/* For the fixed-point implementation below, the coefficients are quantized to 16-bit and                   */
/* represented as 1.15                                                                                      */
/* The input vector is rotated until positioned between 0 and pi/4. After the arctan                        */
/* is calculated for the rotated vector, the initial angle is restored.                                     */
/* The format of the output is 1.15 and scaled by PI. To find the angle value in radians from the output    */
/* of this function, a multiplication by PI is needed.                                                      */

const q15_t coeff[5] = {
    (q15_t)0x28BD,     /*  0.318253 */
    (q15_t)0x006D,     /*  0.003314 */
    (q15_t)0xEF3E,     /* -0.130908 */
    (q15_t)0x08C6,     /*  0.068542 */
    (q15_t)0xFED4,     /* -0.009159 */
};

q15_t arctan(q15_t imag, q15_t real) {
    q15_t       t;
    q15_t       out;
    uint8_t     rotation; /* Clockwise, multiples of PI/4 */
    int8_t      i;

    if ((q15_t)0 == imag) {
        /* Check the sign*/
        if (real & (q15_t)0x8000) {
            /* Negative, return -PI */
            return (q15_t)0x8000;
        }
        else {
            return (q15_t)0;
        }
    }
    else {

        rotation = 0;
        /* Rotate the vector until it's placed in the first octant (0..PI/4) */
        if (imag < 0) {
            imag      = -imag;
            real      = -real;
            rotation += 4;
        }
        if (real <= 0) {
            /* Using 't' as temporary storage before its normal usage */
            t         = real;
            real      = imag;
            imag      = -t;
            rotation += 2;
        }
        if (real <= imag) {
            /* The addition below may overflow, drop 1 LSB precision if needed. */
            /* The subtraction cannot underflow.                                */
            t = real + imag;
            if (t < 0) {
                /* Overflow */
                t         = imag - real;
                real      = (q15_t)(((q31_t)real + (q31_t)imag) >> 1);
                imag      = t >> 1;
            }
            else {
                t         = imag - real;
                real      = (real + imag);
                imag      = t;              
            }
            rotation += 1;
        }

        /* Calculate tangent value */
        t = (q15_t)((q31_t)(imag << 15) / real);

        out = (q15_t)0;

        for (i = 4; i >=0; i--) {
            out += coeff[i];
            arm_mult_q15(&out, &t, &out, 1);
        }
        
        /* Rotate back to original position, in multiples of pi/4 */
        /* We're using 1.15 representation, scaled by pi, so pi/4 = 0x2000 */
        out += (rotation << 13);

        return out;
    }
}

/* This function performs dual functionality:                                           */
/* - open circuit check: the real and imaginary parts can be non-zero but very small    */
/*   due to noise. If they are within the defined thresholds, overwrite them with 0s,   */
/*   this will indicate an open.                                                        */
/* - convert the int16_t to q15_t and q31_t formats, needed for the magnitude and phase */
/*   calculations. */
void convert_dft_results(int16_t *dft_results, q15_t *dft_results_q15, q31_t *dft_results_q31) {
    int8_t      i;

    for (i = 0; i < (DFT_RESULTS_COUNT / 2); i++) {
        if ((dft_results[i] < DFT_RESULTS_OPEN_MAX_THR) &&
            (dft_results[i] > DFT_RESULTS_OPEN_MIN_THR) &&               /* real part */
            (dft_results[2 * i + 1] < DFT_RESULTS_OPEN_MAX_THR) &&
            (dft_results[2 * i + 1] > DFT_RESULTS_OPEN_MIN_THR)) {       /* imaginary part */

            /* Open circuit, force both real and imaginary parts to 0 */
            dft_results[i]           = 0;
            dft_results[2 * i + 1]   = 0;
        }
    }

    /*  Convert to 1.15 format */
    for (i = 0; i < DFT_RESULTS_COUNT; i++) {
        dft_results_q15[i] = (q15_t)dft_results[i];
    }

    /*  Convert to 1.31 format */
    arm_q15_to_q31(dft_results_q15, dft_results_q31, DFT_RESULTS_COUNT);

}

/* Calculates calibrated magnitude.                                     */
/* The input values are the measured RCAL magnitude (magnitude_rcal)    */
/* and the measured magnitude of the unknown impedance (magnitude_z).   */
/* Performs the calculation:                                            */
/*      magnitude = magnitude_rcal / magnitude_z * RCAL                 */
/* Output in custom fixed-point format (28.4).                          */
fixed32_t calculate_magnitude(q31_t magnitude_rcal, q31_t magnitude_z) {
    q63_t       magnitude;
    fixed32_t   out;

    magnitude = (q63_t)0;
    if ((q63_t)0 != magnitude_z) {
        magnitude = (q63_t)magnitude_rcal * (q63_t)RCAL;
        /* Shift up for additional precision and rounding */
        magnitude = (magnitude << 5) / (q63_t)magnitude_z;
        /* Rounding */
        magnitude = (magnitude + 1) >> 1;
    }

    /* Saturate if needed */
    if (magnitude &  0xFFFFFFFF00000000) {
        /* Cannot be negative */
        out.full = 0x7FFFFFFF;
    }
    else {
        out.full = magnitude & 0xFFFFFFFF;
    }
        
    return out;
}

/* Calculates calibrated phase.                                     */
/* The input values are the measured RCAL phase (phase_rcal)        */
/* and the measured phase of the unknown impedance (magnitude_z).   */
/* Performs the calculation:                                        */
/*      phase = (phase_z - phase_rcal) * PI / (2 * PI) * 180        */
/*            = (phase_z - phase_rcal) * 180                        */
/* Output in custom fixed-point format (28.4).                      */
fixed32_t calculate_phase(q15_t phase_rcal, q15_t phase_z) {
    q63_t       phase;
    fixed32_t   out;

    /* Multiply by 180 to convert to degrees */
    phase = ((q63_t)(phase_z - phase_rcal) * (q63_t)180);
    /* Round and convert to fixed32_t */
    out.full = ((phase + (q63_t)0x400) >> 11) & 0xFFFFFFFF;

    return out;
}


/* Simple conversion of a fixed32_t variable to string format. */
void sprintf_fixed32(char *out, fixed32_t in) {
    fixed32_t   tmp;
    
    if (in.full < 0) {
        tmp.parts.fpart = (16 - in.parts.fpart) & 0x0F;
        tmp.parts.ipart = in.parts.ipart;
        if (0 != in.parts.fpart) {
            tmp.parts.ipart++;
        }
        if (0 == tmp.parts.ipart) {
            sprintf(out, "      -0.%04d", tmp.parts.fpart * FIXED32_LSB_SIZE);
        }
        else {
            sprintf(out, "%8d.%04d", tmp.parts.ipart, tmp.parts.fpart * FIXED32_LSB_SIZE);
        }
    }
    else {
        sprintf(out, "%8d.%04d", in.parts.ipart, in.parts.fpart * FIXED32_LSB_SIZE);
    }

}

/* Helper function for printing fixed32_t (magnitude & phase) results */
void print_MagnitudePhase(char *text, fixed32_t magnitude, fixed32_t phase) {
    char                msg[MSG_MAXLEN];
    char                tmp[MSG_MAXLEN];

    sprintf(msg, "    %s = (", text);
    /* Magnitude */
    sprintf_fixed32(tmp, magnitude);
    strcat(msg, tmp);
    strcat(msg, ", ");
    /* Phase */
    sprintf_fixed32(tmp, phase);
    strcat(msg, tmp);
    strcat(msg, ")\r\n");

    PRINT(msg);
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
