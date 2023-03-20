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

#include "app.h"
#include "display.h"

extern const BYTE sysExIDES[];

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
#define kMaxSysex (20)
static BYTE sysex[kMaxSysex];
static int sysexCount = 0;

char saveMode = 0;

int Recall_ProcessStatus( BYTE b )
{
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
            if ( sChannel == 0x4 )          // select bus save
                state = kWantByte1of1;
            else if ( sChannel == 0x0 )
            {
                state = kWantSysex;
                sysexCount = 0;
                sysex[sysexCount++] = b;
            }
            else
            {
                return midiMessageHandler( sStatus, sChannel, sMessage );
            }
            break;
    }
    if ( !( b & 0x80 ) && ( state != kIdle ) )
    {
        return Recall_ProcessMIDI( b );
    }
    return 0;
}

int Recall_Save( int slot )
{
    slot &= 63;
    
    // SavePreset( slot );
    
    return 1;
}

int Recall_Load( int slot )
{
    slot &= 63;
    
    // InstigateLoadPreset( slot, 0 );

    return 0;
}

int Recall_ProcessMessage(void)
{
    int ret = 0;
    int handled = 0;
    if ( sChannel == 0 )
    {
        switch ( sStatus )
        {
            default:
                break;
            case 0xb0:       // control change
                if ( sMessage[0] == 16 )
                {
                    saveMode = ( sMessage[1] == 127 );
                    handled = 1;
                }
                break;
            case 0xc0:       // program change
                if ( saveMode )
                    ret = Recall_Save( sMessage[0] );
                else
                    ret = Recall_Load( sMessage[0] );
                handled = 1;
                break;
        }
    }
    else if ( ( sStatus | sChannel ) == 0xF4 )
    {
        // 0xF4 0x40 is 'save all' which we don't support
        if ( sMessage[0] < 0x40 )
            ret = Recall_Save( sMessage[0] );
        handled = 1;
    }
    if ( !handled )
    {
        ret = midiMessageHandler( sStatus, sChannel, sMessage );
    }
    return ret;
}

int Recall_ProcessMIDI( BYTE b )
{
    int ret = 0;
    switch ( state )
    {
        default:
        case kIdle:
            ret = Recall_ProcessStatus( b );
            break;
        case kWantByte1of1:
            sMessage[0] = b;
            ret = Recall_ProcessMessage();
            state = kIdle;
            break;
        case kWantByte1of2:
            sMessage[0] = b;
            state = kWantByte2;
            break;
        case kWantByte2:
            sMessage[1] = b;
            ret = Recall_ProcessMessage();
            state = kIdle;
            break;
        case kWantSysex:
            if ( sysexCount < kMaxSysex )
                sysex[sysexCount++] = b;
            if ( b & 0x80 )
            {
                state = kIdle;
                if ( b != 0xf7 )
                    ret = Recall_ProcessStatus( b );
                else
                {
                    // end of sysex
                    if ( sysexCount > 4 && memcmp( sysex, sysExIDES, 4 ) == 0 )
                    {
                        // Expert Sleepers sysex
                        ProcessNativeSysEx( sysex, sysexCount );
                    }
                }
            }
            break;
    }
    return ret;
}
