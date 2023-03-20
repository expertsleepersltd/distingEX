/*
MIT License

Copyright (c) 2023 Expert Sleepers Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <xc.h>
#include "system/common/sys_common.h"
#include "peripheral/int/plib_int.h"
#include "peripheral/ports/plib_ports.h"
#include "peripheral/spi/plib_spi.h"
#include "peripheral/tmr/plib_tmr.h"
#include "peripheral/dma/plib_dma.h"

#include "app.h"
#include "display.h"

unsigned int screen[128] __attribute__((coherent)) __attribute__((aligned(16))) = { 0 };
unsigned int screen2[128] __attribute__((coherent)) __attribute__((aligned(16))) = { 0 };
unsigned int *screen2ptr = screen2;

void sendDisplayByte( BYTE c )
{
    // CS low
    PORTACLR = BIT_0;
    // send byte
    while ( !SPI5STATbits.SPITBE )
        ;
    SPI5BUF = c;
    // wait for byte to go out
    while ( SPI5STATbits.SPIBUSY )
        ;
    // CS high
    PORTASET = BIT_0;
}

#ifdef SPI1_IS_EXT_DISPLAY
void sendDisplay2Byte( BYTE c )
{
    // CS low
    PORTJCLR = BIT_3;
    // send byte
    while ( !SPI1STATbits.SPITBE )
        ;
    SPI1BUF = c;
    // wait for byte to go out
    while ( SPI1STATbits.SPIBUSY )
        ;
    // CS high
    PORTJSET = BIT_3;
}
#endif

void configureDisplay(void)
{
    // instruction (not data)
    PORTJCLR = BIT_9;

 	sendDisplayByte(0xae);//--turn off oled panel

	sendDisplayByte(0xd5);//--set display clock divide ratio/oscillator frequency
	sendDisplayByte(0x80);//--set divide ratio

	sendDisplayByte(0xa8);//--set multiplex ratio(1 to 64)
	sendDisplayByte(0x1f);//--1/32 duty

	sendDisplayByte(0xd3);//-set display offset
	sendDisplayByte(0x00);//-not offset
    
    sendDisplayByte(0x20); // addressing mode
    sendDisplayByte(0x01); // vertical
    sendDisplayByte(0x22); // page start and end
    sendDisplayByte(0x00); // start 0
    sendDisplayByte(0x03); // end 3

	sendDisplayByte(0x8d);//--set Charge Pump enable/disable
	sendDisplayByte(0x14);//--set(0x10) disable

	sendDisplayByte(0x40);//--set start line address

	sendDisplayByte(0xa6);//--set normal display

	sendDisplayByte(0xa4);//Disable Entire Display On

	sendDisplayByte(0xa1);//--set segment re-map 128 to 0

	sendDisplayByte(0xC8);//--Set COM Output Scan Direction 64 to 0

	sendDisplayByte(0xda);//--set com pins hardware configuration
	sendDisplayByte(0x42);

	sendDisplayByte(0x81);//--set contrast control register
	sendDisplayByte(0xcf);

	sendDisplayByte(0xd9);//--set pre-charge period
	sendDisplayByte(0xf1);

	sendDisplayByte(0xdb);//--set vcomh
	sendDisplayByte(0x40);
    
    // clear GDRAM
    PORTJSET = BIT_9;
    int i;
    for ( i=0; i<512; ++i )
        sendDisplayByte( 0 );
    PORTJCLR = BIT_9;

    // power up
    PORTGCLR = BIT_15;
    
    // wait 100ms
    delayMs( 100 );
    
	sendDisplayByte(0xaf);//--turn on oled panel

    // data (not instruction)
    PORTJSET = BIT_9;
}

#ifdef SPI1_IS_EXT_DISPLAY
void configureDisplay2(void)
{
    // instruction (not data)
    PORTJCLR = BIT_2;

    sendDisplay2Byte(0xae);//--turn off oled panel
	
    sendDisplay2Byte(0xd5);//--set display clock divide ratio/oscillator frequency
    sendDisplay2Byte(0x80);//--set divide ratio

    sendDisplay2Byte(0xa8);//--set multiplex ratio
    sendDisplay2Byte(0x1f);//--1/32 duty

    sendDisplay2Byte(0xd3);//-set display offset
    sendDisplay2Byte(0x00);//-not offset
    
    sendDisplay2Byte(0x20); // addressing mode
    sendDisplay2Byte(0x01); // vertical
    sendDisplay2Byte(0x21); // column start and end
    sendDisplay2Byte(4); // start 4
    sendDisplay2Byte(131); // end 131
    sendDisplay2Byte(0x22); // page start and end
    sendDisplay2Byte(0x00); // start 0
    sendDisplay2Byte(0x03); // end 3

    sendDisplay2Byte(0xad);//--Set Master Configuration
    sendDisplay2Byte(0x8e);//--Select external VCC supply

    sendDisplay2Byte(0x40);//--set start line address

    sendDisplay2Byte(0xa6);//--set normal display

    sendDisplay2Byte(0xa4);//Disable Entire Display On

    sendDisplay2Byte(0xa1);//--set segment re-map 128 to 0

    sendDisplay2Byte(0xC8);//--Set COM Output Scan Direction 64 to 0

    sendDisplay2Byte(0xda);//--set com pins hardware configuration
    sendDisplay2Byte(0x12);

    sendDisplay2Byte(0x81);//--set contrast control register
    sendDisplay2Byte(0x80);

    sendDisplay2Byte(0xd9);//--set pre-charge period
    sendDisplay2Byte(0xd2);

    sendDisplay2Byte(0xdb);//--set vcomh
    sendDisplay2Byte(0x34);

    sendDisplay2Byte(0xaf);//--turn on oled panel

    // data (not instruction)
    PORTJSET = BIT_2;
}
#endif

void copyToDisplay( const void* buffer )
{
    // CS low
    PORTACLR = BIT_0;
#ifdef SPI1_IS_EXT_DISPLAY
    PORTJCLR = BIT_3;
#endif

    int i;
    for ( i=0; i<512; ++i )
    {
#ifndef SPI1_IS_EXT_DISPLAY
        while ( SPI5STATbits.SPITBF )
            ;
        SPI5BUF = ((const BYTE*)buffer)[i];
#else
        while ( SPI5STATbits.SPITBF | SPI1STATbits.SPITBF )
            ;
        SPI5BUF = ((const BYTE*)buffer)[i];
        SPI1BUF = ((const BYTE*)buffer)[i];
#endif
    }

    while ( SPI5STATbits.SPIBUSY )
        ;
#ifdef SPI1_IS_EXT_DISPLAY
    while ( SPI1STATbits.SPIBUSY )
        ;
#endif
    
    // CS high
    PORTASET = BIT_0;
#ifdef SPI1_IS_EXT_DISPLAY
    PORTJSET = BIT_3;
#endif
}
