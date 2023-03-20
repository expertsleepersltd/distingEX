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
#include <string.h>
#include <math.h>
#include <xc.h>
#include "system_config.h"
#include "system/common/sys_common.h"
#include "system/debug/sys_debug.h"
#include "peripheral/int/plib_int.h"
#include "peripheral/ports/plib_ports.h"
#include "peripheral/spi/plib_spi.h"
#include "peripheral/tmr/plib_tmr.h"
#include "peripheral/dma/plib_dma.h"

#include "app.h"
#include "display.h"

#define kDisplayRefreshCount (SAMPLE_RATE/30)

char displayMode = kDisplayModeNormal;

int kTimeToBlank = 15 * 60 * SAMPLE_RATE;
int displayBlankCountdown = 15 * 60 * SAMPLE_RATE;
int displayIsOn = 1;

char message4x16[4][17] = { 0 };

const BYTE font_8x8[96][8] = {
#include "fonts/codeman38_deluxefont/dlxfont.ttf.h"
};

void drawChar88( int x, int y, char c )
{
	int index = c - 32;
	if ( index < 0 || index >= 96 )
		return;
	int i;
	for ( i=0; i<8; ++i )
	{
		unsigned int xx = x + i;
        if ( xx < 128 )
        {
    		unsigned int f = font_8x8[index][i];
        	screen[xx] |= ( f << y );
        }
	}
}

void drawString88( int x, int y, const char* str )
{
	for ( ;; )
	{
		char c = *str++;
		if ( !c )
			break;
		int index = c - 32;
		if ( index < 0 || index >= 96 )
			continue;
		int i;
		for ( i=0; i<8; ++i )
		{
			unsigned int xx = x + i;
            if ( xx < 128 )
            {
                unsigned int f = font_8x8[index][i];
                screen[xx] |= ( f << y );
            }
            CHECK_SERVICE_AUDIO
		}
		x += 8;
	}
}

int displayBytesToSend = -1;

void displayLoop( void )
{
    int refreshCount = 0;
    unsigned int displayLastTime = time;
    for ( ;; )
    {
        CHECK_SERVICE_AUDIO
        
        if ( displayBytesToSend > 0 )
        {
            if ( SPI5STATbits.SPITBF )
                continue;
#ifdef SPI1_IS_EXT_DISPLAY
            if ( SPI1STATbits.SPITBF )
                continue;
#endif
            SPI5BUF = ((BYTE*)screen)[512-displayBytesToSend];
#ifdef SPI1_IS_EXT_DISPLAY
            SPI1BUF = ((BYTE*)screen2ptr)[512-displayBytesToSend];
#endif
            displayBytesToSend -= 1;
            continue;
        }
        else if ( displayBytesToSend == 0 )
        {
            if ( SPI5STATbits.SPIBUSY )
                continue;
#ifdef SPI1_IS_EXT_DISPLAY
            if ( SPI1STATbits.SPIBUSY )
                continue;
#endif
            // CS high
            PORTASET = BIT_0;
#ifdef SPI1_IS_EXT_DISPLAY
            PORTJSET = BIT_3;
#endif
            displayBytesToSend = -1;
        }
        
        unsigned int thisTime = time;
        int dt = thisTime - displayLastTime;
        refreshCount -= dt;
        if ( refreshCount <= 0 )
        {
            updateDisplay();
            
            displayBytesToSend = 512;
            // CS low
            PORTACLR = BIT_0;
#ifdef SPI1_IS_EXT_DISPLAY
            PORTJCLR = BIT_3;
#endif
            refreshCount = kDisplayRefreshCount;
        }
        
        displayLastTime = thisTime;
    }
}

void clearScreenWithAudioService(void)
{
    clearPartialScreenWithAudioService( 0, 127 );
}

void clearPartialScreenWithAudioService( int x0, int x1 )
{
    int i;
    for ( i=x0; i<=x1; ++i )
    {
        screen[i] = 0;
        CHECK_SERVICE_AUDIO
    }
}

void clearScreenPtrWithAudioService( unsigned int* screen )
{
    clearPartialScreenPtrWithAudioService( screen, 0, 127 );
}

void clearPartialScreenPtrWithAudioService( unsigned int* screen, int x0, int x1 )
{
    int i;
    for ( i=x0; i<=x1; ++i )
    {
        screen[i] = 0;
        CHECK_SERVICE_AUDIO
    }
}

void copyToScreenWithAudioService( unsigned int* src )
{
    int i;
    for ( i=0; i<128; ++i )
    {
        screen[i] = src[i];
        CHECK_SERVICE_AUDIO
    }
}

void orScreen( int x0, int x1, unsigned int mask )
{
    orScreenPtr( screen, x0, x1, mask );
}

void orScreenPtr( unsigned int* screen, int x0, int x1, unsigned int mask )
{
    int i;
    for ( i=x0; i<=x1; ++i )
    {
        screen[i] |= mask;
        CHECK_SERVICE_AUDIO
    }
}

void xorScreen( int x0, int x1, unsigned int mask )
{
    xorScreenPtr( screen, x0, x1, mask );
}

void xorScreenPtr( unsigned int* screen, int x0, int x1, unsigned int mask )
{
    int i;
    for ( i=x0; i<=x1; ++i )
    {
        screen[i] ^= mask;
        CHECK_SERVICE_AUDIO
    }
}

void andScreen( int x0, int x1, unsigned int mask )
{
    andScreenPtr( screen, x0, x1, mask );
}

void andScreenPtr( unsigned int* screen, int x0, int x1, unsigned int mask )
{
    int i;
    for ( i=x0; i<=x1; ++i )
    {
        screen[i] &= mask;
        CHECK_SERVICE_AUDIO
    }
}

void copyScreenWithAudioService( unsigned int* dst, const unsigned int* src )
{
    int i;
    for ( i=0; i<=127; ++i )
    {
        dst[i] = src[i];
        CHECK_SERVICE_AUDIO
    }
}

void updateDisplay( void )
{
    if ( displayIsOn )
    {
        displayBlankCountdown -= kDisplayRefreshCount;
        if ( displayBlankCountdown <= 0 )
        {
            // turn it off
            turnOffDisplay();
            displayIsOn = 0;
            displayBlankCountdown = 0;
        }
    }
    else
    {
        // display is off
        if ( displayBlankCountdown )
        {
            // turn it back on
            turnOnDisplay();
            displayIsOn = 1;
        }
    }
    
    int i;

    screen2ptr = screen;

    unsigned int clear = 0;
    for ( i=0; i<128; ++i )
    {
        screen[i] = clear;
        CHECK_SERVICE_AUDIO
    }
    
    switch ( displayMode )
    {
        default:
        {
            static int x = 0, y = 0, dx = 1, dy = 1;
            x += dx;
            y += dy;
            if ( x > 128-10*8 )
                dx = -1;
            else if ( x < 1 )
                dx = 1;
            if ( y > 24 )
                dy = -1;
            else if ( y < 1 )
                dy = 1;
            drawString88( x, y, "disting EX" );
            
            char buff[16];
            sprintf( buff, "%3d", halfState[0].encoderCounter );
            drawString88( 1, 0, buff );
            sprintf( buff, "%3d", halfState[1].encoderCounter );
            drawString88( 103, 0, buff );
            sprintf( buff, "%4d", adcs.Z[0].value >> 3 );
            drawString88( 1, 24, buff );
            sprintf( buff, "%4d", adcs.Z[1].value >> 3 );
            drawString88( 95, 24, buff );
            
            if ( !halfState[0].encSW )
                xorScreen( 0, 63, 0x000000ff );
            if ( !halfState[1].encSW )
                xorScreen( 64, 127, 0x000000ff );
            if ( !halfState[0].potSW )
                xorScreen( 0, 63, 0xff000000 );
            if ( !halfState[1].potSW )
                xorScreen( 64, 127, 0xff000000 );
        }
            break;
        case kDisplayModeMessage4x16:
            for ( i=0; i<4; ++i )
                drawString88( 0, i*8, message4x16[i] );
            break;
    }            
}

void startupSequence()
{
    memset( screen, 0, sizeof screen );
    copyToDisplay( screen );
    delayMs( 17 );
}

void displayMessage4x16( const char* msg1, const char* msg2, const char* msg3, const char* msg4 )
{
    displayMode = kDisplayModeMessage4x16;
    strncpy( message4x16[0], msg1, 16 );
    strncpy( message4x16[1], msg2, 16 );
    strncpy( message4x16[2], msg3, 16 );
    strncpy( message4x16[3], msg4, 16 );
}

void allowDisplayWrite(void)
{
    if ( displayBytesToSend > 0 )
    {
        while ( SPI5STATbits.SPITBF )
            ;
        SPI5BUF = ((const BYTE*)screen)[512-displayBytesToSend];
#ifdef SPI1_IS_EXT_DISPLAY
        while ( SPI1STATbits.SPITBF )
            ;
        SPI1BUF = ((const BYTE*)screen2ptr)[512-displayBytesToSend];
#endif
        displayBytesToSend -= 1;
    }
}

void flushDisplayWrite(void)
{
    if ( displayBytesToSend >= 0 )
    {
        while ( displayBytesToSend > 0 )
        {
            while ( SPI5STATbits.SPITBF )
                ;
            SPI5BUF = ((const BYTE*)screen)[512-displayBytesToSend];
#ifdef SPI1_IS_EXT_DISPLAY
            while ( SPI1STATbits.SPITBF )
                ;
            SPI1BUF = ((const BYTE*)screen2ptr)[512-displayBytesToSend];
#endif
            displayBytesToSend -= 1;
        }
        while ( SPI5STATbits.SPIBUSY )
            ;
        // CS high
        PORTASET = BIT_0;
#ifdef SPI1_IS_EXT_DISPLAY
        while ( SPI1STATbits.SPIBUSY )
            ;
        // CS high
        PORTJSET = BIT_3;
#endif
        displayBytesToSend = -1;
    }
}

void flushDisplayWriteWithAudioService(void)
{
    if ( displayBytesToSend >= 0 )
    {
        while ( displayBytesToSend > 0 )
        {
#ifdef SPI1_IS_EXT_DISPLAY
            while ( SPI5STATbits.SPITBF | SPI1STATbits.SPITBF )
                CHECK_SERVICE_AUDIO_INTERNAL
#else
            while ( SPI5STATbits.SPITBF )
                CHECK_SERVICE_AUDIO_INTERNAL
#endif
            SPI5BUF = ((const BYTE*)screen)[512-displayBytesToSend];
#ifdef SPI1_IS_EXT_DISPLAY
            SPI1BUF = ((const BYTE*)screen2ptr)[512-displayBytesToSend];
#endif
            displayBytesToSend -= 1;
            CHECK_SERVICE_AUDIO_INTERNAL
        }
#ifdef SPI1_IS_EXT_DISPLAY
        while ( SPI5STATbits.SPIBUSY | SPI1STATbits.SPIBUSY )
            CHECK_SERVICE_AUDIO_INTERNAL
#else
        while ( SPI5STATbits.SPIBUSY )
            CHECK_SERVICE_AUDIO_INTERNAL
#endif
        // CS high
        PORTASET = BIT_0;
#ifdef SPI1_IS_EXT_DISPLAY
        PORTJSET = BIT_3;
#endif
        displayBytesToSend = -1;
    }
}
