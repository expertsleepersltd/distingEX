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

#ifndef _I2C_H    /* Guard against multiple inclusion */
#define _I2C_H

enum {
    kI2C_set_controller         = 0x11,
    kI2C_load_preset            = 0x40,
    kI2C_save_preset            = 0x41,
    kI2C_reset_preset           = 0x42,
    kI2C_get_current_preset     = 0x43,
    kI2C_load_algorithm         = 0x44,
    kI2C_get_current_algorithm  = 0x45,
    kI2C_set_parameter_abs      = 0x46,
    kI2C_set_parameter_cont     = 0x47,
    kI2C_get_parameter_value    = 0x48,
    kI2C_get_parameter_min      = 0x49,
    kI2C_get_parameter_max      = 0x4A,
    kI2C_WAV_Recorder_record    = 0x4B,
    kI2C_WAV_Recorder_play      = 0x4C,
    kI2C_Augustus_Loop_set_pitch    = 0x4D,
    kI2C_Augustus_Loop_send_clock   = 0x4E,
    kI2C_send_MIDI_message          = 0x4F,
    kI2C_send_Select_Bus_message    = 0x50,
    kI2C_voice_pitch            = 0x51,
    kI2C_voice_note_on          = 0x52,
    kI2C_voice_note_off         = 0x53,
    kI2C_note_pitch             = 0x54,
    kI2C_note_on                = 0x55,
    kI2C_note_off               = 0x56,
    kI2C_all_notes_off          = 0x57,
    kI2C_Looper_clear           = 0x58,
    kI2C_Looper_get_state       = 0x59,
    kI2C_dual_get_parameter_value   = 0x5A,
    kI2C_dual_get_parameter_min     = 0x5B,
    kI2C_dual_get_parameter_max     = 0x5C,
    kI2C_dual_set_parameter_abs     = 0x5D,
    kI2C_dual_set_parameter_cont    = 0x5E,
    kI2C_dual_get_current_algorithm = 0x5F,
    kI2C_dual_load_algorithm        = 0x60,
    kI2C_dual_get_current_algorithms = 0x61,
    kI2C_dual_load_algorithms        = 0x62,
    kI2C_dual_load_preset       = 0x63,
    kI2C_dual_save_preset       = 0x64,
    kI2C_dual_takeover_z        = 0x65,
    kI2C_dual_get_z             = 0x66,
    kI2C_set_ES5                = 0x67,
};

extern BYTE i2cResponse[4];
extern BYTE i2cResponseIndex;
extern BYTE i2cResponseSize;

int i2cSendPacket( const UINT8* i2cData, int DataSz );
int i2cReceivePacket( UINT8 addrByte, UINT8* i2cData, int DataSz );

void ConfigureCodec(void);
void configureI2CSlave(void);

#endif /* _I2C_H */
