
This is an example for an amperometric measurement.

After the initialization and calibration steps, a DC voltage is applied across 
the load (AFE5-AFE6) and ADC measurements are performed. After a specified time, 
the DC voltage level is changed. The outputs of the LPF (sinc2hf, 50/60Hz 
rejection) are then returned to the UART.

The following parameters are programmable through macros:
 - DC level 1 (mV)
 - DC level 2 (mV)
 - Duration of DC level 1 (us)
 - Duration of DC level 2 (us)
 - IVS switch closed time (DC level 1) (us)
 - IVS switch closed time (DC level 2) (us)
 - DAC attenuator (enabled/disabled)
 - LPF (sinc2hf) bypass (bypass/do not bypass)
 - Switching current shunting (required/not required)
 - Number of samples to be printed to the terminal/UART

Note 1: there are no checks in the code that the values are within admissible ranges,
which needs to be ensured by the user.

Note 2: the wait of 6.8ms in line 8 of the measurement sequence is based on the 
default components between AFE5 and AFE6 on the ADuCM350 Switch Mux Config Board; 
a 6.8k Ohm resistor and a 1uF capacitor. A minimum wait of 1x RC is recommended here
to allow the RC to settle once the switches are closed.

Macros which are preceeded by a "DO NOT EDIT" comment should not be changed.

When using the Eval-ADuCM350EBZ board, the test needs a daughter board attached
to the evaluation board, with the relevant impedances (AFE5-AFE6) populated. The 
example will report mid-scale values if an open circuit is measured.

Once the test has finished, it sends a result message string to STDIO;
"PASS" for success and "FAIL" (plus failure message) for failures.

For Eval-ADUCM350EBZ boards, the results are returned to the PC/host via the 
UART-on-USB interface on the USB-SWD/UART-EMUZ (Rev.C) board to a listening 
PC based terminal application tuned to the corresponding virtual serial port. 
To ensure that data is returned using the UART, set the macro USE_UART_FOR_DATA = 1. 
Failure to do so will result in a failure, as the standard output is too slow to 
return ADC/LPF data. See the ADuCM350 Device Drivers Getting Started Guide for
information on drivers and configuring the PC based terminal application.
