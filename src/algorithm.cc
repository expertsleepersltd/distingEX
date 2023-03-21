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
#include "display.h"
#include "nvm.h"

#include "peaks/processors.h"
#include "peaks/io_buffer.h"

#define PEAKS_DRIVERS_GATE_INPUT_H_
#define PEAKS_DRIVERS_SWITCHES_H_
enum { kNumSwitches = 2 };
struct Switches {};
#include "peaks/ui.h"

void SetFunction(uint8_t index, peaks::Function f);

#define PEAKS_NVM_BASE 0xBD100000

enum { kPeaksMagic = 0xbeefbeac };

struct {
    peaks::GateFlags    gate_flags[2];
    bool                schmittTrigger[2];
    peaks::Settings     settings;
    bool                lastEncSw[2];
    int                 holdCounter[2];
    int                 encoderValue[2];
    int                 potValue[2];
    uint16_t            processorParams[2][4];
    bool                writeToFlash;
} algorithmData;

void    algorithm_init(void)
{
    memset( &algorithmData, 0, sizeof algorithmData );
    
    algorithmData.lastEncSw[0] = true;
    algorithmData.lastEncSw[1] = true;
    
    // peaks.cc Init()
    peaks::processors[0].Init(0);
    peaks::processors[1].Init(1);

    // attempt to load state from flash
    const int* ptr = (const int*)PEAKS_NVM_BASE;
    if ( ptr[0] == kPeaksMagic )
    {
        algorithmData.settings.edit_mode = ptr[1];
        SetFunction( 0, (peaks::Function)ptr[2] );
        SetFunction( 1, (peaks::Function)ptr[3] );
    }
    else
    {
        algorithmData.settings.edit_mode = peaks::EDIT_MODE_TWIN;
        algorithmData.settings.function[0] = algorithmData.settings.function[1] = peaks::FUNCTION_ENVELOPE;
    }
}

void    algorithm_step( _algorithm_blocks* blocks, int ping )
{
    int i, j;

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
    
    // disting EX runs at 96kHz, peaks at 48kHz
    STATIC_ASSERT( k_framesPerBlock == 2 * peaks::kBlockSize, block_size_error );

    peaks::GateFlags input[2][peaks::kBlockSize];
    
    // peaks.cc TIM1_UP_IRQHandler()
    for ( j=0; j<peaks::kBlockSize; ++j )
    {
        for ( i=0; i<2; ++i )
        {
            if ( algorithmData.schmittTrigger[i] )
            {
                if ( inputVoltages[i][2*j+0] < 0.5f )
                    algorithmData.schmittTrigger[i] = false;
            }
            else
            {
                if ( inputVoltages[i][2*j+0] > 1.0f )
                    algorithmData.schmittTrigger[i] = true;
            }
        }
        
        uint32_t external_gate_inputs = 0;
        if ( algorithmData.schmittTrigger[0] )
            external_gate_inputs |= 1;
        if ( algorithmData.schmittTrigger[1] )
            external_gate_inputs |= 2;
        uint32_t buttons = 0;
        if ( !halfState[0].potSW )
            buttons |= 1;
        if ( !halfState[1].potSW )
            buttons |= 2;
        uint32_t gate_inputs = external_gate_inputs | buttons;

        for (size_t i = 0; i < 2; ++i) {
          algorithmData.gate_flags[i] = peaks::ExtractGateFlags(
              algorithmData.gate_flags[i],
              gate_inputs & (1 << i));
        }

        // A hack to make channel 1 aware of what's going on in channel 2. Used to
        // reset the sequencer.
        input[0][j] = algorithmData.gate_flags[0] \
            | (algorithmData.gate_flags[1] << 4) \
            | (buttons & 1 ? peaks::GATE_FLAG_FROM_BUTTON : 0);

        input[1][j] = algorithmData.gate_flags[1] \
            | (buttons & 2 ? peaks::GATE_FLAG_FROM_BUTTON : 0);
    }
    
    // peaks.cc Process()
    for ( j=0; j<2; ++j )
    {
        int16_t output_buffer[peaks::kBlockSize];
        peaks::processors[j].Process( input[j], output_buffer, peaks::kBlockSize );
        
        // convert to output voltage with naive sample rate conversion
        for ( i=0; i<peaks::kBlockSize; ++i )
        {
            float v = output_buffer[i] * (8.0f/0x7fff);
            outputVoltages[j][2*i+0] = v;
            outputVoltages[j][2*i+1] = v;
        }
    }

    // peaks only has two outputs, so do something simple with the others
    for ( i=0; i<k_framesPerBlock; ++i )
    {
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

    // sometimes it's useful to have a measure of CPU load,
    // and to guarantee a minimum CPU load to avoid issues with the half-DMA interrupt
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

// from ui.cc
const peaks::ProcessorFunction function_table_[peaks::FUNCTION_LAST][2] = {
  { peaks::PROCESSOR_FUNCTION_ENVELOPE, peaks::PROCESSOR_FUNCTION_ENVELOPE },
  { peaks::PROCESSOR_FUNCTION_LFO, peaks::PROCESSOR_FUNCTION_LFO },
  { peaks::PROCESSOR_FUNCTION_TAP_LFO, peaks::PROCESSOR_FUNCTION_TAP_LFO },
  { peaks::PROCESSOR_FUNCTION_BASS_DRUM, peaks::PROCESSOR_FUNCTION_SNARE_DRUM },

  { peaks::PROCESSOR_FUNCTION_MINI_SEQUENCER, peaks::PROCESSOR_FUNCTION_MINI_SEQUENCER },
  { peaks::PROCESSOR_FUNCTION_PULSE_SHAPER, peaks::PROCESSOR_FUNCTION_PULSE_SHAPER },
  { peaks::PROCESSOR_FUNCTION_PULSE_RANDOMIZER, peaks::PROCESSOR_FUNCTION_PULSE_RANDOMIZER },
  { peaks::PROCESSOR_FUNCTION_FM_DRUM, peaks::PROCESSOR_FUNCTION_FM_DRUM },
};

// from ui.cc
void SetFunction(uint8_t index, peaks::Function f) {
  if (algorithmData.settings.edit_mode == peaks::EDIT_MODE_SPLIT || algorithmData.settings.edit_mode == peaks::EDIT_MODE_TWIN) {
    algorithmData.settings.function[0] = algorithmData.settings.function[1] = f;
    peaks::processors[0].set_function(function_table_[f][0]);
    peaks::processors[1].set_function(function_table_[f][1]);
  } else {
    algorithmData.settings.function[index] = f;
    peaks::processors[index].set_function(function_table_[f][index]);
  }
}

void    algorithm_UI( const int* enc )
{
    // left encoder button
    if ( halfState[0].encSW && !algorithmData.lastEncSw[0] )
    {
        if ( algorithmData.holdCounter[0] < SLOW_RATE )
        {
            algorithmData.settings.edit_mode ^= 1;
            algorithmData.writeToFlash = true;
        }
    }
    else if ( !halfState[0].encSW && algorithmData.lastEncSw[0] )
    {
        algorithmData.holdCounter[0] = 0;
    }
    if ( !halfState[0].encSW )
    {
        algorithmData.holdCounter[0] += 1;
        if ( algorithmData.holdCounter[0] == SLOW_RATE )
        {
            algorithmData.settings.edit_mode = ( algorithmData.settings.edit_mode & 2 ) ? peaks::EDIT_MODE_TWIN : peaks::EDIT_MODE_FIRST;
            algorithmData.writeToFlash = true;
        }
    }
    algorithmData.lastEncSw[0] = halfState[0].encSW;

    // right encoder button
    uint8_t f = algorithmData.settings.edit_mode == peaks::EDIT_MODE_SECOND ? algorithmData.settings.function[1] : algorithmData.settings.function[0];
    if ( halfState[1].encSW && !algorithmData.lastEncSw[1] )
    {
        if ( algorithmData.holdCounter[1] < SLOW_RATE )
        {
            f = ( f & 4 ) | ( ( ( f & 3 ) + 1 ) & 3 );
            SetFunction( algorithmData.settings.edit_mode - peaks::EDIT_MODE_FIRST, (peaks::Function)f );
            algorithmData.writeToFlash = true;
        }
    }
    else if ( !halfState[1].encSW && algorithmData.lastEncSw[1] )
    {
        algorithmData.holdCounter[1] = 0;
    }
    if ( !halfState[1].encSW )
    {
        algorithmData.holdCounter[1] += 1;
        if ( algorithmData.holdCounter[1] == SLOW_RATE )
        {
            SetFunction( algorithmData.settings.edit_mode - peaks::EDIT_MODE_FIRST, (peaks::Function)( f ^ 4 ) );
            algorithmData.writeToFlash = true;
        }
    }
    algorithmData.lastEncSw[1] = halfState[1].encSW;
    
    // encoders (peaks itself has pots)
    int i;
    for ( i=0; i<2; ++i )
    {
        if ( enc[i] )
        {
            static const int kEncoderScale = 1000;
            algorithmData.encoderValue[i] += kEncoderScale * enc[i];
            APPLY_RANGE( algorithmData.encoderValue[i], 0, 65535 );
            switch ( algorithmData.settings.edit_mode )
            {
                case peaks::EDIT_MODE_TWIN:
                    peaks::processors[0].set_parameter(i, algorithmData.encoderValue[i]);
                    peaks::processors[1].set_parameter(i, algorithmData.encoderValue[i]);
                    algorithmData.processorParams[0][i] = algorithmData.encoderValue[i];
                    algorithmData.processorParams[1][i] = algorithmData.encoderValue[i];
                    break;
                case peaks::EDIT_MODE_SPLIT:
                    peaks::processors[0].set_parameter(i, algorithmData.encoderValue[i]);
                    algorithmData.processorParams[0][i] = algorithmData.encoderValue[i];
                    break;
                case peaks::EDIT_MODE_FIRST:
                case peaks::EDIT_MODE_SECOND:
                {
                    int which = algorithmData.settings.edit_mode - peaks::EDIT_MODE_FIRST;
                    int v = algorithmData.processorParams[which][i] + kEncoderScale * enc[i];
                    APPLY_RANGE( v, 0, 65535 );
                    peaks::processors[ which ].set_parameter( i, v );
                    algorithmData.processorParams[which][i] = v;
                }
                    break;
            }
        }
    }
    
    // pots
    int pot[2] = { adcs.Z[0].value << 1, adcs.Z[1].value << 1 };
    for ( i=0; i<2; ++i )
    {
        // this is pretty basic pot handling
        // ideally there would be smoothing, 
        // and the threshold for movement would be adaptive
        // like in the actual peaks code
        int delta = pot[i] - algorithmData.potValue[i];
        if ( delta < 0 )
            delta = -delta;
        if ( delta < 1024 )
            continue;
        algorithmData.potValue[i] = pot[i];
        switch ( algorithmData.settings.edit_mode )
        {
            case peaks::EDIT_MODE_TWIN:
                peaks::processors[0].set_parameter(2+i, pot[i]);
                peaks::processors[1].set_parameter(2+i, pot[i]);
                algorithmData.processorParams[0][2+i] = pot[i];
                algorithmData.processorParams[1][2+i] = pot[i];
                break;
            case peaks::EDIT_MODE_SPLIT:
                peaks::processors[1].set_parameter(i, pot[i]);
                algorithmData.processorParams[1][i] = pot[i];
                break;
            case peaks::EDIT_MODE_FIRST:
            case peaks::EDIT_MODE_SECOND:
            {
                int which = algorithmData.settings.edit_mode - peaks::EDIT_MODE_FIRST;
                peaks::processors[ which ].set_parameter( 2+i, pot[i] );
                algorithmData.processorParams[which][2+i] = pot[i];
            }
                break;
        }
    }
}

void    algorithm_idle(void)
{
    if ( algorithmData.writeToFlash )
    {
        algorithmData.writeToFlash = false;
        
        // erase page
        NVMADDR = PEAKS_NVM_BASE & 0x1FFFFFFF;
        NVMOpWithAudioService( 0x4004 );
        
        // prepare data
        int* ptr = (int*)pageBuffer;
        ptr[0] = kPeaksMagic;
        ptr[1] = algorithmData.settings.edit_mode;
        ptr[2] = algorithmData.settings.function[0];
        ptr[3] = algorithmData.settings.function[1];
    
        // write row
        NVMSRCADDR = (unsigned int)pageBuffer & 0x1FFFFFFF;
        NVMADDR = ( PEAKS_NVM_BASE & 0x1FFFFFFF );
        NVMOpWithAudioService( 0x4003 );
    }
}

void    algorithm_display(void)
{
    static char const * const editModeStrings[] = {
        "TWIN",
        "SPLIT",
        "EXPERT 1",
        "EXPERT 2",
    };
    drawString88( 0, 0, editModeStrings[ algorithmData.settings.edit_mode ] );

    static char const * const functionStrings[] = {
        "ENVELOPE",
        "LFO",
        "TAP_LFO",
        "BASSDRUM",
        "SNARE",
        "HIGHHAT",
        "FM DRUM",
        "SHAPER",
        "RANDOMIZ",
        "BOUNCING",
        "MINISEQ",
        "NUMBERS",
    };

    char buff[8];
    int i;
    for ( i=0; i<2; ++i )
    {
        drawString88( 64, i*8, functionStrings[ peaks::processors[i].function() ] );
        
        int j;
        for ( j=0; j<4; ++j )
        {
            sprintf( buff, "%04X", algorithmData.processorParams[i][j] );
            drawString88( j*32, 16+i*8, buff );
        }
    }
}
