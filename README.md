# distingEX
An open source code framework for the Expert Sleepers® disting EX Eurorack module.

[https://expert-sleepers.co.uk/distingEX.html](https://expert-sleepers.co.uk/distingEX.html)

© 2023 Expert Sleepers Ltd

## Build environment
- Microchip MPLABX IDE v6 or higher: [https://www.microchip.com/mplabx](https://www.microchip.com/mplabx)
- Microchip XC32 compiler v4 or higher: [https://www.microchip.com/xc32](https://www.microchip.com/xc32)
- Microchip MPLAB Harmony SDK v2.06: [https://www.microchip.com/en-us/tools-resources/configure/mplab-harmony/version-2](https://www.microchip.com/en-us/tools-resources/configure/mplab-harmony/version-2)

The project files expect to find the Harmony SDK at

	../../../../../../../microchip/harmony/v2_06

relative to the Makefile.

For actual development and debugging work you will need a programming tool e.g. the [PICkit™ 4](https://www.microchip.com/en-us/development-tool/PG164140). This connects to the standard 6-pin ICSP header on the disting EX PCB.

## Build configurations
- **default_4_0**: build to run directly on the hardware.
- **for_bootloader**: build to generate a hex file to install via the bootloader.

The bootloader-compatible .hex file is

	distingEX.X/dist/for_bootloader/production/distingEX.X.production.hex

which will need to be renamed to

	distingEX_<something>.hex

for the bootloader to recognise it on the MicroSD card.
