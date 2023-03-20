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

#ifndef _APP_H
#define _APP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "system_config.h"
#include "system_definitions.h"

// DOM-IGNORE-BEGIN
#ifdef __cplusplus  // Provide C++ Compatibility

extern "C" {

#endif
// DOM-IGNORE-END 
    
#define SAMPLE_RATE (96000)

#define SLOW_RATE (600)

#define kSlowTimeRatio (SAMPLE_RATE/SLOW_RATE)

#define SPI1_IS_EXT_DISPLAY

typedef long long int64_t;
typedef unsigned long long uint64_t;

typedef unsigned char           BYTE;                           /* 8-bit unsigned  */
typedef unsigned short int      WORD;                           /* 16-bit unsigned */
typedef unsigned long           DWORD;                          /* 32-bit unsigned */

typedef unsigned char           UINT8;
typedef unsigned int            UINT32;
typedef int                     BOOL;
enum { FALSE = 0, TRUE = 1 };

/******************************************************************************
 * PORT Parameter values to be used with mPORTxxx macros
 *****************************************************************************/

 #define BIT_31                       (1 << 31)
 #define BIT_30                       (1 << 30)
 #define BIT_29                       (1 << 29)
 #define BIT_28                       (1 << 28)
 #define BIT_27                       (1 << 27)
 #define BIT_26                       (1 << 26)
 #define BIT_25                       (1 << 25)
 #define BIT_24                       (1 << 24)
 #define BIT_23                       (1 << 23)
 #define BIT_22                       (1 << 22)
 #define BIT_21                       (1 << 21)
 #define BIT_20                       (1 << 20)
 #define BIT_19                       (1 << 19)
 #define BIT_18                       (1 << 18)
 #define BIT_17                       (1 << 17)
 #define BIT_16                       (1 << 16)
 #define BIT_15                       (1 << 15)
 #define BIT_14                       (1 << 14)
 #define BIT_13                       (1 << 13)
 #define BIT_12                       (1 << 12)
 #define BIT_11                       (1 << 11)
 #define BIT_10                       (1 << 10)
 #define BIT_9                        (1 << 9)
 #define BIT_8                        (1 << 8)
 #define BIT_7                        (1 << 7)
 #define BIT_6                        (1 << 6)
 #define BIT_5                        (1 << 5)
 #define BIT_4                        (1 << 4)
 #define BIT_3                        (1 << 3)
 #define BIT_2                        (1 << 2)
 #define BIT_1                        (1 << 1)
 #define BIT_0                        (1 << 0)

typedef enum
{
	/* Application's state machine's initial state. */
	APP_STATE_INIT=0,
	APP_STATE_SERVICE_TASKS,

} APP_STATES;

typedef struct
{
    /* The application's current state */
    APP_STATES state;

    int errorCondition;

} APP_DATA;

extern APP_DATA appData;

void APP_Initialize ( void );

void APP_Tasks( void );

#define CLAMP( x )                  \
        if ( x < -0x800000 )        \
            x = -0x800000;          \
        else if ( x > 0x7fffff )    \
            x = 0x7fffff;

extern unsigned int time;

enum { kNumZsamples = 8 };
enum { kZbits = 15 };

typedef struct {
    int     value;
    short   pos;
    short   samples[kNumZsamples];
} _adcChannel;

typedef struct {
    short          CV;
    short          Gate;
    short          rawZ[2];
    _adcChannel    Z[2];
} _adcs;

extern _adcs adcs;

#define kI2CRxQueueSize (256)
extern short i2cRxQueueRead;
extern short i2cRxQueueWrite;
extern short i2cRxQueue[kI2CRxQueueSize];

typedef struct {
    int     Er[2];
    int     D[2];
    int     Br[3];
    
    int     A[3];
    float   Brf[3];
    float   Erf[2];
    float   Ddf[2];
    int     Dd[2];

    BYTE    encA;
    BYTE    encB;
    BYTE    encSW;
    BYTE    potSW;
    BYTE    lastEncA;
    
    BYTE    encoderCounter;
} _halfState;

typedef struct {
    float   Brf;
    float   mABrf;
} _input_calibration;

extern _input_calibration inputCalibrations[6];

typedef int (*MIDIMessageHandler)( BYTE status, BYTE channel, const BYTE* message );

extern _halfState halfState[2];

extern BYTE pageBuffer[0x4000];

#define AUDIO_INTERRUPT ( DCH5INT & BIT_5 )
#define CLEAR_AUDIO_INTERRUPT() {DCH5INTCLR = BIT_3 | BIT_4 | BIT_5;}

static inline __attribute__((always_inline)) void WaitForAudioInterrupt(void)
{
    while ( !AUDIO_INTERRUPT )
        ;
    CLEAR_AUDIO_INTERRUPT();
}

void serviceAudioSingle(void);
void serviceAudioInternalSingle(void);

#define CHECK_SERVICE_AUDIO CheckServiceAudio();

#define CHECK_SERVICE_AUDIO_INTERNAL CheckServiceAudioInternal();

static inline __attribute__((always_inline)) void CheckServiceAudioInternal(void)
{
    if ( AUDIO_INTERRUPT )
    {
        CLEAR_AUDIO_INTERRUPT();
        serviceAudioInternalSingle();
    }
}

static inline __attribute__((always_inline)) void CheckServiceAudio(void)
{
    if ( AUDIO_INTERRUPT )
    {
        CLEAR_AUDIO_INTERRUPT();
        serviceAudioSingle();
    }
}

void delayMs( unsigned int ms );

extern MIDIMessageHandler midiMessageHandler;

extern int DefaultMIDIMessageHandler( BYTE status, BYTE channel, const BYTE* message );
extern int ProcessMIDIIn( BYTE b );
extern int QueueMIDI3( UINT32 msg );
extern void BlockingQueueMIDI3( UINT32 msg );
extern void BlockingQueueMIDI1( BYTE b );
extern int QueueMIDI2( UINT32 msg );
extern void BlockingQueueMIDI2( UINT32 msg );
extern int HandleMIDIOut(void);
extern void FlushMIDIOut(void);
void FlushMIDIRx(void);
void sendBytes( int code, const BYTE* ptr, int count );
void sendSysExMsg( const char* str );

extern BYTE midiOutPending;
extern unsigned int masterMIDIClockCounter;

#define kSelectRxQueueSize (16)
extern char selectRxQueueRead;
extern char selectRxQueueWrite;
extern BYTE selectRxQueue[kSelectRxQueueSize];
extern volatile BYTE recallIgnoreBytes;

extern int Recall_ProcessMIDI( BYTE b );
extern int ProcessMIDI( BYTE b );

extern void ProcessNativeSysEx( const BYTE* sysex, int sysexCount );

int ProcessI2CIn( int b );

void configureDisplay(void);
void configureDisplay2(void);

#define SRAM_ADDR (0xC0000000)
#define SRAM_ADDR_UNCACHED (0xE0000000)
#define SRAM_SIZE (8*1024*1024)

void ReadCalibrationFromSettings(void);

#ifndef __LP64__
#define STATIC_ASSERT(X) ({ extern int __attribute__((error("assertion failure: '" #X "' not true"))) compile_time_check(); ((X)?0:compile_time_check()),0; })
#else
#define STATIC_ASSERT(X)
#endif

#define ARRAY_SIZE(X) (sizeof X/sizeof X[0])

#define APPLY_RANGE(X,M,N)  \
    { if ( (X) < (M) ) X = M; else if ( (X) > (N) ) X = N; }

//DOM-IGNORE-BEGIN
#ifdef __cplusplus
}
#endif
//DOM-IGNORE-END

#endif /* _APP_H */
