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

#include <stdio.h>
#include <stdlib.h>
#include <xc.h>
#include "system/common/sys_common.h"
#include "peripheral/int/plib_int.h"
#include "peripheral/ports/plib_ports.h"
#include "peripheral/spi/plib_spi.h"
#include "peripheral/tmr/plib_tmr.h"
#include "peripheral/dma/plib_dma.h"

#include "app.h"
#include "display.h"

void turnOnDisplay()
{
    // instruction (not data)
#ifdef SPI1_IS_EXT_DISPLAY
    PORTJCLR = BIT_9 | BIT_2;
#else
    PORTJCLR = BIT_9;
#endif

    sendDisplayByte(0x22); // page start and end
    sendDisplayByte(0x00); // start 0
    sendDisplayByte(0x03); // end 3
    
	sendDisplayByte(0xaf);//--turn on oled panel

#ifdef SPI1_IS_EXT_DISPLAY
	sendDisplay2Byte(0xaf);//--turn on oled panel
#endif

    // data (not instruction)
#ifdef SPI1_IS_EXT_DISPLAY
    PORTJSET = BIT_9 | BIT_2;
#else
    PORTJSET = BIT_9;
#endif
}

void turnOffDisplay()
{
    // instruction (not data)
#ifdef SPI1_IS_EXT_DISPLAY
    PORTJCLR = BIT_9 | BIT_2;
#else
    PORTJCLR = BIT_9;
#endif
    
	sendDisplayByte(0xae);//--turn on oled panel

#ifdef SPI1_IS_EXT_DISPLAY
	sendDisplay2Byte(0xae);//--turn on oled panel
#endif

    // data (not instruction)
#ifdef SPI1_IS_EXT_DISPLAY
    PORTJSET = BIT_9 | BIT_2;
#else
    PORTJSET = BIT_9;
#endif
}

void setDisplayContrast( BYTE contrast )
{
    // instruction (not data)
#ifdef SPI1_IS_EXT_DISPLAY
    PORTJCLR = BIT_9 | BIT_2;
#else
    PORTJCLR = BIT_9;
#endif
    
	sendDisplayByte( 0x81 );
    sendDisplayByte( contrast );

#ifdef SPI1_IS_EXT_DISPLAY
    delayMs( 1 );
	sendDisplay2Byte( 0x81 );
    sendDisplay2Byte( contrast );
    delayMs( 1 );
#endif

    // data (not instruction)
#ifdef SPI1_IS_EXT_DISPLAY
    PORTJSET = BIT_9 | BIT_2;
#else
    PORTJSET = BIT_9;
#endif
}

void setDisplayFlip( BYTE flip )
{
    // instruction (not data)
    PORTJCLR = BIT_9;

    if ( flip )
    {
        sendDisplayByte( 0xa0 );
        sendDisplayByte( 0xc0 );
    }
    else
    {
        sendDisplayByte( 0xa1 );
        sendDisplayByte( 0xc8 );
    }

    // data (not instruction)
    PORTJSET = BIT_9;
}
