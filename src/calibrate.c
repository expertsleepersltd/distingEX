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

#include "app.h"
#include "display.h"

typedef struct {
    int zeroIn;
    int threeVolt;
} InputCalibrationData;

typedef struct {
    int zeroOut;
    int halfOut;
} OutputCalibrationData;

typedef struct Settings {

    UINT32          m_version;

    int         m_editable[16];
	
    BYTE		m_editableBytes[16];

    InputCalibrationData    inData[6];      // X, Y, Z  x2
    OutputCalibrationData   outData[4];     // A, B     x2

} Settings;

#define SETTINGS_BASE 0xBD008000
#define nvm_settings ( (Settings*)SETTINGS_BASE )

Settings settings __attribute__((aligned(16))) __attribute__((coherent));

    /*
     * V = voltage
     * C = code
     *
     * Ci = A + B * Vi
     * Vo = D + E * Co
     *
     * zeroIn    = A
     * threeVolt = A + B * 3
     * zeroOut   = A + B * ( D )
     * halfOut   = A + B * ( D + E * 0x400000 )
     *           = zeroOut + B * E * 0x400000
     *
     * A = zeroIn
     * B = ( threeVolt - zeroIn )/3
     * D = ( zeroOut - zeroIn )/B
     * E = ( halfOut - zeroOut )/( B * 0x400000 )
     *
     * Vi = ( Ci - A )/B
     * Co = ( Vo - D )/E
     * or
     * Co = ( Vo / E ) - ( D / E )
     */
    
int checkValidRange( int v, int min, int max )
{
    if ( v < min || v > max )
        return 0;
    return 1;
}

int checkValidRanges( int zeroIn, int zeroOut, int halfOut, int threeVolt )
{
    if ( !checkValidRange( zeroIn,      -0x100000, 0x100000 ) ||
            !checkValidRange( zeroOut,  -0x100000, 0x100000 ) ||
            !checkValidRange( halfOut,   0x200000, 0x600000 ) ||
            !checkValidRange( threeVolt, 0x100000, 0x380000 ) )
        return 0;
    return 1;
}

int CheckValidCalibration( int fix )
{
    int bad = 0;
    int i;
    for ( i=0; i<6; ++i )
    {
        if ( !checkValidRange( settings.inData[i].zeroIn, -0x100000, 0x100000 ) )
        {
            bad = 1;
            if ( fix )
                settings.inData[i].zeroIn = 0;
        }
        if ( !checkValidRange( settings.inData[i].threeVolt, 0x100000, 0x380000 ) )
        {
            bad = 1;
            if ( fix )
                settings.inData[i].threeVolt = 0x266666;
        }
    }
    for ( i=0; i<4; ++i )
    {
        if ( !checkValidRange( settings.outData[i].zeroOut, -0x100000, 0x100000 ) )
        {
            bad = 1;
            if ( fix )
                settings.outData[i].zeroOut = 0;
        }
        if ( !checkValidRange( settings.outData[i].halfOut, 0x200000, 0x600000 ) )
        {
            bad = 1;
            if ( fix )
                settings.outData[i].halfOut = 0x400000;
        }
    }
    return !bad;
}

void ReadCalibrationFromSettings(void)
{
    memcpy( &settings, nvm_settings, sizeof(Settings) );

    if ( settings.inData[0].threeVolt == 0 || settings.inData[0].threeVolt == 0xffffffff )
    {
        // default calibration - settings have been wiped
        memset( settings.inData, 0, sizeof settings.inData );
        memset( settings.outData, 0, sizeof settings.outData );
        int i;
        for ( i=0; i<6; ++i )
            settings.inData[i].threeVolt = 0x266666;
        for ( i=0; i<4; ++i )
            settings.outData[i].halfOut = 0x400000;
    }

    if ( !CheckValidCalibration( 1 ) )
        displayMessage4x16( "Bad calibration data", "Using defaults", "", "" );

    static const BYTE inputMap[2][3] = { 0, 1, 4, 2, 3, 5 };
    
    int A[6];
    
    int d;
    for ( d=0; d<2; ++d )
    {
        int i;
        for ( i=0; i<3; ++i )
        {
            int zeroIn = settings.inData[3*d+i].zeroIn;
            int threeVolt = settings.inData[3*d+i].threeVolt;
            
            halfState[d].A[i] = zeroIn;
            A[ inputMap[d][i] ] = zeroIn;
            
            double Bf = threeVolt - zeroIn;
            if ( Bf == 0.0 )
                Bf = (3<<23)/10.0;
            Bf = Bf / 3.0;
            halfState[d].Brf[i] = 1.0 / Bf;
            inputCalibrations[ inputMap[d][i] ].Brf = halfState[d].Brf[i];

            int B = ( threeVolt - zeroIn ) / 3;
            // avoid a divide by zero
            if ( B == 0 )
                B = 0xCCCCC;
            int64_t Bri = 0x80000000000LL / B;
            halfState[d].Br[i] = Bri;

            if ( i < 2 )
            {
                int zeroOut = settings.outData[2*d+i].zeroOut;
                int halfOut = settings.outData[2*d+i].halfOut;

                halfState[d].D[i] = ( ( zeroOut - zeroIn ) * Bri ) >> 24;
                // 0x400000 << 5 = 0x8000000
                int64_t Bb = ((int64_t)B) << 27;
                int n = halfOut - zeroOut;
                // avoid a divide by zero
                if ( n == 0 )
                    n = 0x399999;
                halfState[d].Er[i] = Bb / n;

                double Df = ( zeroOut - zeroIn )/Bf;
                double Ef = n / ( Bf * 0x400000 );
                double Dd = Df / Ef;
                halfState[d].Dd[i] = Dd;
                halfState[d].Ddf[i] = Dd;
                halfState[d].Erf[i] = 1.0 / Ef;
            }
        }
    }

    int w;
    for ( w=0; w<6; ++w )
    {
        static const BYTE half[6] = { 0, 0, 1, 1, 0, 1 };
        static const BYTE in[6] = { 0, 1, 0, 1, 2, 2 };

        int d = half[w];
        int i = in[w];

        inputCalibrations[ w ].Brf = halfState[d].Brf[i];
        inputCalibrations[ w ].mABrf = ( -A[ w ] ) * inputCalibrations[ w ].Brf;
    }
}
