# ADuCM350 Firmware for the Citizensensor project
> Code created by Matthias Steinmaßl

The Pi HAT equippes a M350 from Analog Devices. The Firmware can be compiled using IAR EWARM.

## Functions

It integrates readout of internal ADCs, basic communication functions with UART and BME680 (not tested).

Make sure that in the file `C:\Analog Devices\ADuCM350BBCZ\Eval-ADUCM350EBZ\inc\config\adi_i2c_config.h`
the settings are:
```
#define ADI_I2C_CFG_ENABLE_DMA_SUPPORT                     0
#define ADI_I2C_CFG_SLAVE_MODE_SUPPORT                     0
#define ADI_I2C_CFG_ENABLE_STATIC_CONFIG_SUPPORT           1
#define ADI_I2C_CFG_ENABLE_CALLBACK_SUPPORT                0
```

Open this directory: `C:\Analog Devices\ADuCM350BBCZ\` and clone the CitizenSensor repository here:
`git clone https://github.com/CitizenSensor/CitizenSensor`.

Open `C:\Analog Devices\ADuCM350BBCZ\CitizenSensor\1_RaspberryPi-HAT\Firmware\iar\Chronopotentiometrie.c`
with the IAR EWARM code-size limited IDE ([Link](https://www.iar.com/iar-embedded-workbench/#!?architecture=Arm)) and try to make it.

If it fails, check the following settings:
1. Project-Options-Build Actions: Correct path to ielftool.exe
2. Project-Options-C/C++ Compiler/Preprocessor: Check the correct paths to */examples, */inc and */inc/config
3. Check if all the files in Driver, System and Test Sources can be found, and if not re-add them.