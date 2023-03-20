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
#include <math.h>

#include "app.h"
#include "display.h"

int ProcessMIDI( BYTE b );

const BYTE sysExIDES[] = { 0xF0, 0x00, 0x21, 0x27 };

#define kMidiQueueSize (736)

int midiQueueWritePos = 0;
int midiQueueReadPos = -1;
BYTE midiQueue[ kMidiQueueSize ];

enum State
{
    kIdle,
    kWantByte1of1,
    kWantByte1of2,
    kWantByte2,
    kWantSysex,
};

static enum State state = kIdle;
static BYTE sStatus, sChannel;
static BYTE sMessage[2];

#define kMaxSysex (4096)
static BYTE sysex[kMaxSysex];
static int sysexCount = 0;

int ProcessStatus( BYTE b )
{
    int ret = 0;
    if ( b & 0x80 )
    {
        sStatus  = b & 0xf0;
        sChannel = b & 0x0f;
    }
    switch ( sStatus )
    {
        default:
            break;
        case 0x80:       // note off
        case 0x90:       // note on
        case 0xa0:       // poly pressure
        case 0xb0:       // control change
        case 0xe0:       // pitch bend
            state = kWantByte1of2;
            break;
        case 0xc0:       // program change
        case 0xd0:       // channel pressure
            state = kWantByte1of1;
            break;
        case 0xf0:
            switch ( sChannel )
            {
                case 0x0:
                    state = kWantSysex;
                    sysexCount = 0;
                    sysex[sysexCount++] = b;
                    break;
                default:
                    break;
            }
            break;
    }
    if ( !( b & 0x80 ) && ( state != kIdle ) )
    {
        ret |= ProcessMIDI( b );
    }
    return ret;
}

int DefaultProcessChannelPressure( int channel, BYTE message0 )
{
    return 0;
}

int DefaultProcessCC( int channel, BYTE message0, BYTE message1 )
{
    return 0;
}

int DefaultDualProcessChannelPressure( int channel, BYTE message0 )
{
    return 0;
}

int DefaultDualProcessCC( int channel, BYTE message0, BYTE message1 )
{
    return 0;
}

int DefaultProcessPGM( int channel, BYTE message0 )
{
    return 0;
}

int firstMidiClock = 0;
unsigned int masterMIDIClockCounter = 0;

int DefaultProcessRealTime( int channel )
{
    switch ( channel )
    {
        case 0xA:
            // start
            firstMidiClock = 1;
            break;
        case 0xB:
            // continue
            break;
        case 0xC:
            // stop
            break;
        case 0x8:
            // clock
            if ( firstMidiClock )
            {
                firstMidiClock = 0;
                masterMIDIClockCounter = 0;
            }
            else
            {
                masterMIDIClockCounter += 1;
                if ( masterMIDIClockCounter >= 96*4 )
                    masterMIDIClockCounter = 0;
            }
            break;
    }
    return 0;
}

int DefaultMIDIMessageHandler( BYTE status, BYTE channel, const BYTE* message )
{
    int ret = 0;
    switch ( status )
    {
        default:
            break;
        case 0xb0:       // control change
            ret = DefaultProcessCC( channel, message[0], message[1] );
            break;
        case 0xc0:      // program change
            ret = DefaultProcessPGM( channel, message[0] );
            break;
        case 0xd0:      // channel pressure
            ret = DefaultProcessChannelPressure( channel, message[0] );
            break;
        case 0xf0:      // real time
            if ( channel >= 0x8 )
                ret = DefaultProcessRealTime( channel );
            break;
    }
    return ret;
}

void sendBytes( int code, const BYTE* ptr, int count )
{
    flushDisplayWrite();
    
    const BYTE header[] = { 0xF0, 0x00, 0x21, 0x27, 0x5D, 0 /* id */, code };
    int i;
    for ( i=0; i<sizeof header; ++i )
        BlockingQueueMIDI1( header[i] );
    
    for ( i=0; i<count; ++i )
    {
        BlockingQueueMIDI1( ptr[i] & 0x7f );
    }
    
    BlockingQueueMIDI1( 0xF7 );
}

void sendSysExMsg( const char* str )
{
    sendBytes( 0x32, str, strlen(str) );
}

void ProcessNativeSysEx( const BYTE* sysex, int sysexCount )
{
    // sysex 0-3 has been matched against our Manufacturer ID
    
    // match hardware ID
    if ( !( sysexCount > 5 && sysex[4] == 0x5D ) )
        return;
    
    // check sysex message type
    if ( !( sysexCount > 7 ) )
        return;
    int id = sysex[5];
    if ( id != 0x7f && id != 0 /* id */ )
        return;
    
    char buff[3072];
    unsigned int p;
    
    const BYTE* msg = &sysex[7];
    switch ( sysex[6] )
    {
        default:
            break;
        case 0x01:
            // take screenshot
            break;
        case 0x02:
            // display message
            break;
        case 0x11:
            // install scl
            break;
        case 0x12:
            // install kbm
            break;
        case 0x22:
            // version string
            break;
        case 0x40:
            // request algorithm
            break;
        case 0x41:
            // request preset name
            break;
        case 0x42:
            // request num parameters
            break;
        case 0x43:
            // request parameter info
            break;
        case 0x44:
            // request all parameter values
            break;
        case 0x45:
            // get parameter value
            break;
        case 0x46:
            // set parameter value
            break;
        case 0x47:
            // set preset name
            break;
        case 0x48:
            // get unit strings
            break;
        case 0x49:
            // get enum strings
            break;
        case 0x4A:
            // set focus
            break;
        case 0x4B:
            // request mapping
            break;
        case 0x4C:
            // set mapping name
            break;
        case 0x4D:
            // set mapping
            break;
        case 0x4E:
            // set midi mapping
            break;
        case 0x4F:
            // set i2c mapping
            break;
        case 0x50:
            // get parameter value string
            break;
        case 0x60:
            // algorithm specific message
            break;
        case 0x71:
            // request screen as chars
            break;
        case 0x73:
            // request parameter name
            break;
        case 0x74:
            // request parameter value
            break;
        case 0x77:
            // text entry
            break;
        case 0x78:
            // remote control
            break;
    }
}

void ProcessNonRealTimeSystemExclusive( const BYTE* sysex, int sysexCount )
{
}

int ProcessMIDI( BYTE b )
{
    if ( b >= 0xF8 )
    {
        // System Real-Time Messages need to be dealt with immediately
        return midiMessageHandler( b & 0xf0, b & 0x0f, NULL );
    }

    int ret = 0;
    switch ( state )
    {
        default:
        case kIdle:
            ret = ProcessStatus( b );
            break;
        case kWantByte1of1:
            sMessage[0] = b;
            ret = midiMessageHandler( sStatus, sChannel, sMessage );
            state = kIdle;
            break;
        case kWantByte1of2:
            sMessage[0] = b;
            state = kWantByte2;
            break;
        case kWantByte2:
            sMessage[1] = b;
            ret = midiMessageHandler( sStatus, sChannel, sMessage );
            state = kIdle;
            break;
        case kWantSysex:
            if ( sysexCount < kMaxSysex )
                sysex[sysexCount++] = b;
            if ( b & 0x80 )
            {
                state = kIdle;
                if ( b != 0xf7 )
                    ret = ProcessStatus( b );
                else
                {
                    // end of sysex
                    if ( sysexCount > 4 && memcmp( sysex, sysExIDES, 4 ) == 0 )
                    {
                        // Expert Sleepers sysex
                        ProcessNativeSysEx( sysex, sysexCount );
                    }
                    else if ( sysexCount > 2 && sysex[1] == 0x7E )
                    {
                        ProcessNonRealTimeSystemExclusive( sysex, sysexCount );
                    }
                }
            }
            break;
    }
    return ret;
}

int ProcessMIDIIn( BYTE b )
{
    displayBlankCountdown = kTimeToBlank;
    
    return ProcessMIDI( b );
}

int QueueMIDI1( BYTE b )
{
    int w = midiQueueWritePos;
    if ( midiQueueReadPos == w )
        return 0;
    midiQueue[ w ] = b;
    w += 1;
    if ( w >= kMidiQueueSize )
        w = 0;
    midiQueueWritePos = w;
    midiOutPending = 1;
    return 1;
}

void BlockingQueueMIDI1( BYTE b )
{
    for ( ;; )
    {
        if ( QueueMIDI1( b ) )
            return;
        while ( U4STAbits.UTXBF )
            ;
        HandleMIDIOut();
    }
}

int QueueMIDI3( UINT32 msg )
{
    int w = midiQueueWritePos;
    const int r = midiQueueReadPos;
    int i;
    for ( i=0; i<3; ++i )
    {
        if ( r == w )
            return 0;
        midiQueue[ w ] = ( msg >> 16 ) & 0xff;
        msg <<= 8;
        w += 1;
        if ( w >= kMidiQueueSize )
            w = 0;
    }
    midiQueueWritePos = w;
    midiOutPending = 1;
    return 1;
}

void BlockingQueueMIDI3( UINT32 msg )
{
    for ( ;; )
    {
        if ( QueueMIDI3( msg ) )
            return;
        while ( U4STAbits.UTXBF )
            ;
        HandleMIDIOut();
    }
}

int QueueMIDI2( UINT32 msg )
{
    int w = midiQueueWritePos;
    const int r = midiQueueReadPos;
    int i;
    for ( i=0; i<2; ++i )
    {
        if ( r == w )
            return 0;
        midiQueue[ w ] = ( msg >> 8 ) & 0xff;
        msg <<= 8;
        w += 1;
        if ( w >= kMidiQueueSize )
            w = 0;
    }
    midiQueueWritePos = w;
    midiOutPending = 1;
    return 1;
}

void BlockingQueueMIDI2( UINT32 msg )
{
    for ( ;; )
    {
        if ( QueueMIDI2( msg ) )
            return;
        while ( U4STAbits.UTXBF )
            ;
        HandleMIDIOut();
    }
}

int HandleMIDIOut()
{
    if ( U4STAbits.UTXBF )
        return 1;               // busy
    
	int nextRead = midiQueueReadPos + 1;
	if ( nextRead >= kMidiQueueSize )
		nextRead = 0;
	if ( nextRead == midiQueueWritePos )
    {
        midiOutPending = 0;
		return 0;				// queue empty
    }

	midiQueueReadPos = nextRead;
	BYTE b = midiQueue[ nextRead ];

    U4TXREG = b;
    
    return 1;
}

void FlushMIDIOut()
{
    while ( HandleMIDIOut() )
        ;
}
