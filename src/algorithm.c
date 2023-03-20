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

#include "algorithm.h"

void    algorithm_init(void)
{
}

void    algorithm_step( _algorithm_blocks* blocks, int ping )
{
    int i;

    unsigned int t0 = __builtin_mfc0( _CP0_COUNT, _CP0_COUNT_SELECT );
    
    float inputVoltages[6][k_framesPerBlock];
    float outputVoltages[4][k_framesPerBlock];
    
    // calculate input voltages
    for ( i=0; i<k_framesPerBlock; ++i )
    {
        inputVoltages[0][i] = blocks->in[0][ ping + 2*i + 0 ] * inputCalibrations[0].Brf + inputCalibrations[0].mABrf;
        inputVoltages[1][i] = blocks->in[0][ ping + 2*i + 1 ] * inputCalibrations[1].Brf + inputCalibrations[1].mABrf;
        inputVoltages[2][i] = blocks->in[1][ ping + 2*i + 0 ] * inputCalibrations[2].Brf + inputCalibrations[2].mABrf;
        inputVoltages[3][i] = blocks->in[1][ ping + 2*i + 1 ] * inputCalibrations[3].Brf + inputCalibrations[3].mABrf;
        inputVoltages[4][i] = blocks->in[2][ ping + 2*i + 1 ] * inputCalibrations[4].Brf + inputCalibrations[4].mABrf;
        inputVoltages[5][i] = blocks->in[2][ ping + 2*i + 0 ] * inputCalibrations[5].Brf + inputCalibrations[5].mABrf;
    }

    // run the algorithm
    for ( i=0; i<k_framesPerBlock; ++i )
    {
        outputVoltages[0][i] = inputVoltages[0][i];
        outputVoltages[1][i] = inputVoltages[1][i];
        outputVoltages[2][i] = inputVoltages[2][i] + inputVoltages[4][i];
        outputVoltages[3][i] = inputVoltages[3][i] + inputVoltages[5][i];
    }

    // calculate output frames
    for ( i=0; i<k_framesPerBlock; ++i )
    {
        int c1 = ( (int)( outputVoltages[0][i] * halfState[0].Erf[0] ) ) - halfState[0].Dd[0];
        int c2 = ( (int)( outputVoltages[1][i] * halfState[0].Erf[1] ) ) - halfState[0].Dd[1];
        int c3 = ( (int)( outputVoltages[2][i] * halfState[1].Erf[0] ) ) - halfState[1].Dd[0];
        int c4 = ( (int)( outputVoltages[3][i] * halfState[1].Erf[1] ) ) - halfState[1].Dd[1];
        CLAMP( c1 );
        CLAMP( c2 );
        CLAMP( c3 );
        CLAMP( c4 );
        blocks->out[0][ping+2*i+1] = c1;
        blocks->out[0][ping+2*i+0] = c2;
        blocks->out[1][ping+2*i+1] = c3;
        blocks->out[1][ping+2*i+0] = c4;
    }

    for ( ;; )
    {
        unsigned int t1 = __builtin_mfc0( _CP0_COUNT, _CP0_COUNT_SELECT );
        unsigned int t = t1 - t0;
        int cpuLoad = ( t * 100 ) / ( ( k_framesPerBlock * (uint64_t)SYS_CLK_FREQ/2 ) / SAMPLE_RATE );
        if ( cpuLoad > 50 )
            break;

        int x = time;
        for ( i=0; i<2; ++i )
        {
            int j;
            for ( j=0; j<200; ++j )
                x += time;
        }
        pageBuffer[0] = x;
    }
}
