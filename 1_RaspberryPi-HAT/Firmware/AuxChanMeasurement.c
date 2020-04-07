/*********************************************************************************

Copyright (c) 2014 Analog Devices, Inc. All Rights Reserved.

This software is proprietary to Analog Devices, Inc. and its licensors.  By using 
this software you agree to the terms of the associated Analog Devices Software 
License Agreement.

*********************************************************************************/

/*****************************************************************************
 * @file:    AuxChanMeasurement.c
 * @brief:   Auxilliary channel measurement example.
 *****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "test_common.h"

#include "afe.h"
#include "afe_lib.h"
#include "uart.h"

/* Macro to enable the returning of AFE data using the UART */
/*      1 = return AFE data on UART                         */
/*      0 = return AFE data on SW (Std Output)              */
#define USE_UART_FOR_DATA           (1)

/* Helper macro for printing strings to UART or Std. Output */
#define PRINT(s)                    test_print(s)
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

/* Number of samples per DMA transfer. Max of 1024. */
// #define DMA_BUFFER_SIZE            (300u)#
#define DMA_BUFFER_SIZE            (5) //Changed by Citizensensor

/* DO NOT EDIT: Maximum printed message length. Used for printing only. */
//#define MSG_MAXLEN                  (50)
#define MSG_MAXLEN                  (64) //Changed by Citizensensor

#pragma location="volatile_ram"
uint16_t        dmaBuffer[DMA_BUFFER_SIZE * 2];

/* Sequence for Auxilliary Channel measurement */
uint32_t seq_afe_auxmeas[] = {
    0x000A00F3,   /*  0 - Safety Word, Command Count = 10, CRC = 0xF3                       */
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

/* Variables and functions needed for data output through UART */
//ADI_UART_HANDLE     hUartDevice     = NULL;
extern ADI_UART_HANDLE     hUartDevice; //Changed by Citizensensor
extern void timer_ms(uint32_t n_ms); //Changed by Citizensensor
extern void             test_print              (char *pBuffer); //Changed by Citizensensor
extern int32_t          adi_initpinmux              (void);
void        RxDmaCB         (void *hAfeDevice, 
                             uint32_t length, 
                             void *pBuffer);

// int main(void) { //Changed by Citizensensor
void init_opencircuit(ADI_AFE_DEV_HANDLE hAfeDevice) {

    // ADI_AFE_DEV_HANDLE  hAfeDevice; //Changed by Citizensensor
    
    /* Initialize system */
    // SystemInit(); //Changed by Citizensensor

    /* Change the system clock source to HFXTAL and change clock frequency to 16MHz     */
    /* Requirement for AFE (ACLK)                                                       */
    // SystemTransitionClocks(ADI_SYS_CLOCK_TRIGGER_MEASUREMENT_ON); //Changed by Citizensensor
    
    /* SPLL with 32MHz used, need to divide by 2 */
    // SetSystemClockDivider(ADI_SYS_CLOCK_UART, 2); //Changed by Citizensensor
    
    /* Test initialization */
    // test_Init(); //Changed by Citizensensor

    /* Initialize static pinmuxing */
    // adi_initpinmux(); //Changed by Citizensensor

    /* Initialize the UART for transferring measurement data out */
    // if (ADI_UART_SUCCESS != uart_Init()) //Changed by Citizensensor
    // {
        // FAIL("uart_Init");
    // }
    
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

// #if (ADI_AFE_CFG_ENABLE_RX_DMA_DUAL_BUFFER_SUPPORT == 1)   
    // /* Set the Rx DMA buffer sizes */
    // if (ADI_AFE_SUCCESS != adi_AFE_SetDmaRxBufferMaxSize(hAfeDevice, DMA_BUFFER_SIZE, DMA_BUFFER_SIZE))
    // {
        // FAIL("adi_AFE_SetDmaRxBufferMaxSize");
    // }
// #endif /* (ADI_AFE_CFG_ENABLE_RX_DMA_DUAL_BUFFER_SUPPORT == 1) */
    
    // /* Register Rx DMA Callback */
    // if (ADI_AFE_SUCCESS != adi_AFE_RegisterCallbackOnReceiveDMA(hAfeDevice, RxDmaCB, 0))
    // {
        // FAIL("adi_AFE_RegisterCallbackOnReceiveDMA");
    // } //Changed by Citizensensor
        
    /* Configure the AFE_ADC_CFG register */
    seq_afe_auxmeas[2] = SEQ_MMR_WRITE(REG_AFE_AFE_ADC_CFG, ADC_CFG_REG_DATA);
        
    /* Set the duration of the measurement in ACLK periods */
    seq_afe_auxmeas[7] = (DUR * 16);
    
    /* Recalculate CRC in software for the amperometric measurement */
    adi_AFE_EnableSoftwareCRC(hAfeDevice, true);
} //Changed by Citizensensor

void method_opencircuit (ADI_AFE_DEV_HANDLE hAfeDevice, const uint32_t *const seq_afe_auxmeas) { //Changed by Citizensensor
    char msg[MSG_MAXLEN]; //Changed by Citizensensor
    uint16_t oc_results; //Changed by Citizensensor
    double value_V; //Changed by Citizensensor
    
    /* Perform the Aux Channel measurement(s) */
    if (ADI_AFE_SUCCESS != adi_AFE_RunSequence(hAfeDevice, seq_afe_auxmeas, (uint16_t *) dmaBuffer, SAMPLE_COUNT)) 
    {
        FAIL("adi_AFE_RunSequence(seq_afe_auxmeas)");   
    }
    
    timer_ms(50); //Changed by Citizensensor
    oc_results = dmaBuffer[0]; //Changed by Citizensensor
    value_V = -1.79996 + 0.000054931 * (double) oc_results; //Changed by Citizensensor
    sprintf(msg, "%i\t%2.4f\r\n", oc_results, value_V); //Changed by Citizensensor
    
    PRINT(msg); //Changed by Citizensensor
    
    /* Restore to using default CRC stored with the sequence */
    adi_AFE_EnableSoftwareCRC(hAfeDevice, false);
    
    /* AFE Power Down */
    if (ADI_AFE_SUCCESS != adi_AFE_PowerDown(hAfeDevice)) 
    {
        FAIL("adi_AFE_PowerDown");
    }

    /* Unregister Rx DMA Callback */
    if (ADI_AFE_SUCCESS != adi_AFE_RegisterCallbackOnReceiveDMA(hAfeDevice, NULL, 0))
    {
        FAIL("adi_AFE_RegisterCallbackOnReceiveDMA (unregister)");
    }

    /* Uninitialize the AFE API */
    if (ADI_AFE_SUCCESS != adi_AFE_UnInit(hAfeDevice)) 
    {
        FAIL("adi_AFE_UnInit");
    }
    
    /* Uninitialize the UART */
    // adi_UART_UnInit(hUartDevice); //Changed by Citizensensor
    
    PASS();
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

    /* Check if there are samples to be sent */
    // if (length)
    // {
        // for (i = 0; i < length; i++)
        // {
            // sprintf(msg, "%u\r\n", *ppBuffer++);
            // PRINT(msg);
        // }
    // } //Changed by Citizensensor

#elif (0 == USE_UART_FOR_DATA)
    FAIL("Std. Output is too slow for ADC/LPF data. Use UART instead.");
    
#endif /* USE_UART_FOR_DATA */
}

// /* Helper function for printing a string to UART or Std. Output */
// void test_print (char *pBuffer) {
// #if (1 == USE_UART_FOR_DATA)
    // int16_t size;
    // /* Print to UART */
    // size = strlen(pBuffer);
    // adi_UART_BufTx(hUartDevice, pBuffer, &size);

// #elif (0 == USE_UART_FOR_DATA)
    // /* Print  to console */
    // printf(pBuffer);

// #endif /* USE_UART_FOR_DATA */
// } //Changed by Citizensensor

/* Initialize the UART, set the baud rate and enable */
// ADI_UART_RESULT_TYPE uart_Init (void) {
    // ADI_UART_RESULT_TYPE    result = ADI_UART_SUCCESS;
    
    // /* Open UART in blocking, non-intrrpt mode by supplying no internal buffs */
    // if (ADI_UART_SUCCESS != (result = adi_UART_Init(ADI_UART_DEVID_0, &hUartDevice, NULL)))
    // {
        // return result;
    // }

    // /* Set UART baud rate to 115200 */
    // if (ADI_UART_SUCCESS != (result = adi_UART_SetBaudRate(hUartDevice, ADI_UART_BAUD_115200)))
    // {
        // return result;
    // }
    
    // /* Enable UART */
    // if (ADI_UART_SUCCESS != (result = adi_UART_Enable(hUartDevice,true)))
    // {
        // return result;
    // }
    
    // return re //Changed by Citizensensorsult;
// }

/* Uninitialize the UART */
// ADI_UART_RESULT_TYPE uart_UnInit (void) {
    // ADI_UART_RESULT_TYPE    result = ADI_UART_SUCCESS;
    
  // /* Uninitialize the UART API */
    // if (ADI_UART_SUCCESS != (result = adi_UART_UnInit(hUartDevice)))
    // {
        // return result;
    // }
    
    // return result;
// } //Changed by Citizensensor