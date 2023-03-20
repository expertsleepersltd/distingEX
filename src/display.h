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

#ifndef _DISPLAY_H
#define _DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned int screen[128];
extern unsigned int screen2[128];
extern unsigned int *screen2ptr;

extern int kTimeToBlank;
extern int displayBlankCountdown;
extern int displayIsOn;

extern char message4x16[4][17];

enum {
	kDisplayModeNormal,
    kDisplayModeMessage4x16,
};
extern char displayMode;

void startupSequence(void);

extern void copyToDisplay( const void* buffer );
extern void copyToDisplayWithAudioService( const void* buffer );
extern void copyScreenWithAudioService( unsigned int* dst, const unsigned int* src );
extern void clearScreenWithAudioService(void);
extern void clearPartialScreenWithAudioService( int x0, int x1 );
void clearScreenPtrWithAudioService( unsigned int* screen );
void clearPartialScreenPtrWithAudioService( unsigned int* screen, int x0, int x1 );
void orScreen( int x0, int x1, unsigned int mask );
void orScreenPtr( unsigned int* screen, int x0, int x1, unsigned int mask );
void xorScreen( int x0, int x1, unsigned int mask );
void xorScreenPtr( unsigned int* screen, int x0, int x1, unsigned int mask );
void andScreen( int x0, int x1, unsigned int mask );
void andScreenPtr( unsigned int* screen, int x0, int x1, unsigned int mask );
extern void copyToScreenWithAudioService( unsigned int* src );
extern void turnOffDisplay(void);
extern void turnOnDisplay(void);
extern void setDisplayContrast( BYTE contrast );
extern void setDisplayFlip( BYTE flip );
extern void displayMessage4x16( const char* msg1, const char* msg2, const char* msg3, const char* msg4 );
extern void forceUpdateDisplay(void);
extern void updateDisplay( void );
extern void displayLoop( void );

void allowDisplayWrite(void);
void flushDisplayWrite(void);
void flushDisplayWriteWithAudioService(void);
void sendDisplayByte( BYTE c );
void sendDisplay2Byte( BYTE c );

void drawString88( int x, int y, const char* str );

#ifdef __cplusplus
}
#endif

#endif /* _DISPLAY_H */
