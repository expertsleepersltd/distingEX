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

#include "app.h"

#include "peripheral/i2c/plib_i2c.h"
#include "peripheral/int/plib_int.h"
#include "i2c.h"
#include "display.h"

#define GetSystemClock()           (SYS_CLK_FREQ)
#define GetPeripheralClock()       (SYS_CLK_BUS_PERIPHERAL_2)
#define I2C_CLOCK_FREQ             100000

enum I2CState
{
    kI2CIdle,
    kWantByte1,
    kWantByte2of4,
    kWantByte3of4,
    kWantByte4of4,
    kWantByte2of2,
    kWantByte2of3,
    kWantByte3of3,
    kWantByte2ofN,
};

static enum I2CState i2cState = kI2CIdle;
static BYTE i2cMsg[4];

BYTE i2cResponse[4] = { 0 };
BYTE i2cResponseIndex = 0;
BYTE i2cResponseSize = 0;

/*******************************************************************************
  Function:
    BOOL StartTransfer( BOOL restart )

  Summary:
    Starts (or restarts) a transfer to/from the EEPROM.

  Description:
    This routine starts (or restarts) a transfer to/from the EEPROM, waiting (in
    a blocking loop) until the start (or re-start) condition has completed.

  Precondition:
    The I2C module must have been initialized.

  Parameters:
    restart - If FALSE, send a "Start" condition
            - If TRUE, send a "Restart" condition

  Returns:
    TRUE    - If successful
    FALSE   - If a collision occured during Start signaling

  Example:
    <code>
    StartTransfer(FALSE);
    </code>

  Remarks:
    This is a blocking routine that waits for the bus to be idle and the Start
    (or Restart) signal to complete.
  *****************************************************************************/

BOOL StartTransfer( BOOL restart )
{
    // Send the Start (or Restart) signal
    if(restart)
    {
        PLIB_I2C_MasterStartRepeat(I2C_ID_2);
    }
    else
    {
        // Wait for the bus to be idle, then start the transfer
        while( !PLIB_I2C_BusIsIdle(I2C_ID_2) );

        PLIB_I2C_MasterStart(I2C_ID_2);
    }

    // Wait for the signal to complete
    while ( I2C2CONbits.SEN )
        ;
    
    if ( PLIB_I2C_ArbitrationLossHasOccurred(I2C_ID_2) )
        return FALSE;

    return TRUE;
}


/*******************************************************************************
  Function:
    BOOL TransmitOneByte( UINT8 data )

  Summary:
    This transmits one byte to the EEPROM.

  Description:
    This transmits one byte to the EEPROM, and reports errors for any bus
    collisions.

  Precondition:
    The transfer must have been previously started.

  Parameters:
    data    - Data byte to transmit

  Returns:
    TRUE    - Data was sent successfully
    FALSE   - A bus collision occured

  Example:
    <code>
    TransmitOneByte(0xAA);
    </code>

  Remarks:
    This is a blocking routine that waits for the transmission to complete.
  *****************************************************************************/

BOOL TransmitOneByte( UINT8 data )
{
    // Wait for the transmitter to be ready
    while(!PLIB_I2C_TransmitterIsReady(I2C_ID_2));

    // Transmit the byte
    PLIB_I2C_TransmitterByteSend(I2C_ID_2, data);
    if ( PLIB_I2C_TransmitterOverflowHasOccurred(I2C_ID_2) )
    {
        DBPRINTF("Error: I2C Master Bus Collision\n");
        return FALSE;
    }

    // Wait for the transmission to finish
    while(!PLIB_I2C_TransmitterByteHasCompleted(I2C_ID_2));

    return TRUE;
}


/*******************************************************************************
  Function:
    void StopTransfer( void )

  Summary:
    Stops a transfer to/from the EEPROM.

  Description:
    This routine Stops a transfer to/from the EEPROM, waiting (in a
    blocking loop) until the Stop condition has completed.

  Precondition:
    The I2C module must have been initialized & a transfer started.

  Parameters:
    None.

  Returns:
    None.

  Example:
    <code>
    StopTransfer();
    </code>

  Remarks:
    This is a blocking routine that waits for the Stop signal to complete.
  *****************************************************************************/

void StopTransfer( void )
{
    // Send the Stop signal
    PLIB_I2C_MasterStop(I2C_ID_2);

    // Wait for the signal to complete
    while ( I2C2CONbits.PEN )
        ;
}


void ErrorHalt()
{
//    openTimerForScroll();
    for ( ;; )
    {
//        scrollMessageOnceAndWait( "I2C error" );
    }
}

void SendPacket( UINT8* i2cData, int DataSz )
{
    int                 Index;
    UINT32              actualClock;
    BOOL                Acknowledged;
    BOOL                Success = TRUE;
    UINT8               i2cbyte;

    // Start the transfer to write data to the EEPROM
    if( !StartTransfer(FALSE) )
    {
        while(1);
    }

    // Transmit all data
    Index = 0;
    while( Success && (Index < DataSz) )
    {
        // Transmit a byte
        if (TransmitOneByte(i2cData[Index]))
        {
            // Advance to the next byte
            Index++;

            // Verify that the byte was acknowledged
            if(!PLIB_I2C_TransmitterByteWasAcknowledged(I2C_ID_2))
            {
                DBPRINTF("Error: Sent byte was not acknowledged\n");
                Success = FALSE;
            }
        }
        else
        {
            Success = FALSE;
        }
    }

    // End the transfer (hang here if an error occured)
    StopTransfer();
    if(!Success)
    {
        ErrorHalt();
    }

#if 0
    // Wait for EEPROM to complete write process, by polling the ack status.
    Acknowledged = FALSE;
    do
    {
        // Start the transfer to address the EEPROM
        if( !StartTransfer(FALSE) )
        {
            while(1);
        }

        // Transmit just the EEPROM's address
        if (TransmitOneByte(i2cData[0]))
        {
            // Check to see if the byte was acknowledged
            Acknowledged = PLIB_I2C_TransmitterByteWasAcknowledged(I2C_ID_2);
        }
        else
        {
            Success = FALSE;
        }

        // End the transfer (stop here if an error occured)
        StopTransfer();
        if(!Success)
        {
            ErrorHalt();
        }

    } while (Acknowledged != TRUE);

    // End the transfer (stop here if an error occured)
    StopTransfer();
    if(!Success)
    {
        ErrorHalt();
    }
#endif
}

void ConfigureCodec(void)
{
    I2C2BRG = ( GetPeripheralClock() / ( 2*I2C_CLOCK_FREQ ) ) - 1;
    // I2CEN
    I2C2CON = BIT_15;
    //
    UINT8 i2cData[3];

    // Initialize the data buffer
    PLIB_I2C_SlaveAddress7BitSet( I2C_ID_2, 0x44 );
    i2cData[0] = ( 0x44 << 1 ) | I2C_WRITE;

    // set LJ outputs
    i2cData[1] = 65;
    i2cData[2] = 0x01;
    SendPacket( i2cData, 3 );

    // set LJ inputs & master mode
    i2cData[1] = 81;
    i2cData[2] = 0x01 | ( 4 << 4 );
    SendPacket( i2cData, 3 );

    // disable HPF
    i2cData[1] = 82;
    i2cData[2] = 0x07;
    SendPacket( i2cData, 3 );

    // set ADC phase
    i2cData[1] = 84;
    i2cData[2] = 0x11;
    SendPacket( i2cData, 3 );

    // set DAC phase
    i2cData[1] = 67;
    i2cData[2] = 0x05;
    SendPacket( i2cData, 3 );

    // set DAC power save
    i2cData[1] = 66;
    i2cData[2] = 0xc0;
    SendPacket( i2cData, 3 );

    // disable
    I2C2CONCLR = BIT_15;
}

BOOL i2cStartTransfer( BOOL restart )
{
    // Send the Start (or Restart) signal
    if(restart)
    {
        PLIB_I2C_MasterStartRepeat(I2C_ID_4);
    }
    else
    {
        // Wait for the bus to be idle, then start the transfer
        while( !PLIB_I2C_BusIsIdle(I2C_ID_4) );

        PLIB_I2C_MasterStart(I2C_ID_4);
    }

    // Wait for the signal to complete
    while ( I2C4CONbits.SEN )
        ;
    
    if ( PLIB_I2C_ArbitrationLossHasOccurred(I2C_ID_4) )
        return FALSE;

    return TRUE;
}

BOOL i2cTransmitOneByte( UINT8 data )
{
    // Wait for the transmitter to be ready
    while(!PLIB_I2C_TransmitterIsReady(I2C_ID_4));

    // Transmit the byte
    PLIB_I2C_TransmitterByteSend(I2C_ID_4, data);
    if ( PLIB_I2C_TransmitterOverflowHasOccurred(I2C_ID_4) )
    {
        DBPRINTF("Error: I2C Master Bus Collision\n");
        return FALSE;
    }

    // Wait for the transmission to finish
    while(!PLIB_I2C_TransmitterByteHasCompleted(I2C_ID_4));

    return TRUE;
}

void i2cStopTransfer( void )
{
    // Send the Stop signal
    PLIB_I2C_MasterStop(I2C_ID_4);

    // Wait for the signal to complete
    while ( I2C4CONbits.PEN )
        ;
}

int i2cSendPacket( const UINT8* i2cData, int DataSz )
{
    int                 Index;
    BOOL                Success = TRUE;

    // Start the transfer to write data to the EEPROM
    if( !i2cStartTransfer(FALSE) )
    {
        return 0;
    }

    // Transmit all data
    Index = 0;
    while( Success && (Index < DataSz) )
    {
        // Transmit a byte
        if (i2cTransmitOneByte(i2cData[Index]))
        {
            // Advance to the next byte
            Index++;

            // Verify that the byte was acknowledged
            if(!PLIB_I2C_TransmitterByteWasAcknowledged(I2C_ID_4))
            {
                DBPRINTF("Error: Sent byte was not acknowledged\n");
                Success = FALSE;
            }
        }
        else
        {
            Success = FALSE;
        }
    }

    // End the transfer (hang here if an error occured)
    i2cStopTransfer();
    if(!Success)
    {
        return 0;
    }
    
    return 1;
}

int i2cReceivePacket( UINT8 addrByte, UINT8* i2cData, int DataSz )
{
    int                 Index;
    BOOL                Success = TRUE;

    if( !i2cStartTransfer(FALSE) )
    {
        return 0;
    }

    if ( i2cTransmitOneByte(addrByte) )
    {
        // Verify that the byte was acknowledged
        if(!PLIB_I2C_TransmitterByteWasAcknowledged(I2C_ID_4))
        {
            DBPRINTF("Error: Sent byte was not acknowledged\n");
            Success = FALSE;
        }
    }
    else
    {
        Success = FALSE;
    }
    
    Index = 0;
    while( Success && (Index < DataSz) )
    {
        while ( I2C4CON & 0x1f )
            ;
        I2C4CONSET = BIT_3;         // RCEN
        while ( I2C4CON & 0x1f )
            ;
        while ( !I2C4STATbits.RBF )
            ;
        I2C4CONbits.ACKDT = Index >= (DataSz-1);
        while ( I2C4CON & 0x1f )
            ;
        I2C4CONSET = BIT_4;         // ACKEN
//        PLIB_I2C_ReceivedByteAcknowledge( I2C_ID_2, Index < (DataSz-1) );
        i2cData[ Index++ ] = I2C4RCV;
    }

    i2cStopTransfer();
    if(!Success)
    {
        return 0;
    }
    return 1;
}

void configureI2CSlave(void)
{
    I2C4CONCLR = BIT_15;

	PLIB_INT_VectorPrioritySet( INT_ID_0, INT_VECTOR_I2C4_SLAVE, INT_PRIORITY_LEVEL3 );
    PLIB_INT_VectorSubPrioritySet( INT_ID_0, INT_VECTOR_I2C4_SLAVE, INT_SUBPRIORITY_LEVEL0 );
    PLIB_INT_SourceEnable( INT_ID_0, INT_SOURCE_I2C_4_SLAVE );

    PLIB_I2C_SlaveAddress7BitSet( I2C_ID_4, 0x31 );
    I2C4BRG = ( GetPeripheralClock() / ( 2*I2C_CLOCK_FREQ ) ) - 1;
    // I2CEN
    I2C4CON = BIT_15;
}

void __ISR(_I2C4_SLAVE_VECTOR, ipl3srs) I2C4SlaveInterruptHandler(void)
{
    if ( PLIB_INT_SourceFlagGet( INT_ID_0, INT_SOURCE_I2C_4_SLAVE ) )
    {
        if ( I2C4STATbits.R_W )
        {
            // read, not write
            if ( i2cResponseIndex < i2cResponseSize )
            {
                I2C4TRN = i2cResponse[ i2cResponseIndex++ ];
                I2C4CONSET = BIT_12;        // SCLREL
            }
            else
            {
                I2C4CONCLR = BIT_15;
                asm volatile ( "nop" );
                asm volatile ( "nop" );
                asm volatile ( "nop" );
                asm volatile ( "nop" );
                asm volatile ( "nop" );
                I2C4CONSET = BIT_15;
            }

            if ( i2cRxQueueRead == i2cRxQueueWrite )
            {
                // queue overflow
            }
            else
            {
                i2cRxQueue[ i2cRxQueueWrite ] = -0x100;
                i2cRxQueueWrite = ( i2cRxQueueWrite + 1 ) & ( kI2CRxQueueSize-1 );
            }
        }
        else
        {
            BYTE data = I2C4RCV;
            if ( !I2C4STATbits.D_A )
            {
                // address received
                {
                    // byte received
                    if ( i2cRxQueueRead == i2cRxQueueWrite )
                    {
                        // queue overflow
                    }
                    else
                    {
                        i2cRxQueue[ i2cRxQueueWrite ] = -(short)data;
                        i2cRxQueueWrite = ( i2cRxQueueWrite + 1 ) & ( kI2CRxQueueSize-1 );
                    }
                }
            }
            else
            {
                {
                    // byte received
                    if ( i2cRxQueueRead == i2cRxQueueWrite )
                    {
                        // queue overflow
                    }
                    else
                    {
                        i2cRxQueue[ i2cRxQueueWrite ] = data;
                        i2cRxQueueWrite = ( i2cRxQueueWrite + 1 ) & ( kI2CRxQueueSize-1 );
                    }
                }
            }
        }
        
        if ( I2C4STATbits.I2COV )
            I2C4STATbits.I2COV = 0;

        PLIB_INT_SourceFlagClear( INT_ID_0, INT_SOURCE_I2C_4_SLAVE );
    }
}

void ProcessI2CMIDICommand(void)
{
    int cmd = i2cMsg[0];
    int numBytes = 1;
    switch ( i2cMsg[1] & 0xf0 )
    {
        case 0x80:       // note off
        case 0x90:       // note on
        case 0xa0:       // poly pressure
        case 0xb0:       // control change
        case 0xe0:       // pitch bend
            numBytes = 3;
            break;
        case 0xc0:       // program change
        case 0xd0:       // channel pressure
            numBytes = 2;
            break;
        case 0xf0:
            switch ( i2cMsg[1] & 0x0f )
            {
                case 0x0:                   // sysex
                    return;
                case 0x1:                   // MTC
                case 0x3:                   // song select
                    numBytes = 2;
                    break;
                case 0x2:                   // SPP
                    numBytes = 3;
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
    
    if ( cmd == kI2C_send_MIDI_message )
    {
        int i;
        for ( i=0; i<numBytes; ++i )
            BlockingQueueMIDI1( i2cMsg[1+i] );
    }
    else if ( cmd == kI2C_send_Select_Bus_message )
    {
        int i;
        for ( i=0; i<numBytes; ++i )
        {
            while ( U2STAbits.UTXBF )
                ;
            U2TXREG = i2cMsg[1+i];
        }
        recallIgnoreBytes += numBytes;
    }
}

void ProcessI2C4byteCommand(void)
{
    int cmd = i2cMsg[0];
    if ( cmd == kI2C_set_controller )
    {
        int ctrl = i2cMsg[1];

        int v = ( ( (int)i2cMsg[2] << 24 ) | ( (int)i2cMsg[3] << 16 ) ) >> 16;
    }
    else if ( cmd >= kI2C_set_parameter_abs && cmd <= kI2C_set_parameter_cont )
    {
    }
    else if ( ( cmd >= kI2C_voice_pitch && cmd <= kI2C_voice_note_on )
             || ( cmd >= kI2C_note_pitch && cmd <= kI2C_note_on ) )
    {
    }
    else if ( cmd >= kI2C_send_MIDI_message && cmd <= kI2C_send_Select_Bus_message )
    {
        ProcessI2CMIDICommand();
    }
    else if ( cmd == kI2C_dual_set_parameter_cont )
    {
    }
}

void ProcessI2C3byteCommand(void)
{
    int cmd = i2cMsg[0];
    int value = ( ( (int)i2cMsg[1] << 24 ) | ( (int)i2cMsg[2] << 16 ) ) >> 16;
    if ( cmd == kI2C_Augustus_Loop_set_pitch )
    {
    }
    else if ( cmd == kI2C_load_preset )
    {
    }
    else if ( cmd == kI2C_save_preset )
    {
    }
    else if ( cmd >= kI2C_send_MIDI_message && cmd <= kI2C_send_Select_Bus_message )
    {
        ProcessI2CMIDICommand();
    }
    else if ( cmd == kI2C_dual_set_parameter_abs )
    {
    }
    else if ( cmd == kI2C_dual_load_algorithm )
    {
    }
    else if ( cmd == kI2C_dual_load_algorithms )
    {
    }
    else if ( cmd >= kI2C_dual_load_preset && cmd <= kI2C_dual_takeover_z )
    {
    }
    else if ( cmd == kI2C_set_ES5 )
    {
    }
}

void ProcessI2C2byteCommand(void)
{
    int cmd = i2cMsg[0];
    if ( ( cmd == kI2C_voice_note_off )
        || ( cmd == kI2C_note_off )
        || ( cmd >= kI2C_WAV_Recorder_record && cmd <= kI2C_WAV_Recorder_play )
        || ( cmd == kI2C_Looper_get_state ) ) 
    {
    }
    else if ( cmd == kI2C_load_algorithm )
    {
    }
    else if ( cmd >= kI2C_get_parameter_value && cmd <= kI2C_get_parameter_max )
    {
        unsigned int p = i2cMsg[1] - 1;
        int v = 0;
        i2cResponse[0] = v >> 8;
        i2cResponse[1] = v >> 0;
        i2cResponseIndex = 0;
        i2cResponseSize = 2;
    }
    else if ( cmd >= kI2C_dual_get_parameter_value && cmd <= kI2C_dual_get_parameter_max )
    {
    }
    else if ( cmd == kI2C_dual_get_current_algorithm )
    {
    }
}

void ProcessI2C1byteCommand(void)
{
    int cmd = i2cMsg[0];
    if ( ( cmd == kI2C_all_notes_off )
        || ( cmd == kI2C_Augustus_Loop_send_clock )
        || ( cmd == kI2C_Looper_clear ) )
    {
    }    
    else if ( cmd == kI2C_reset_preset )
    {
    }
    else if ( cmd == kI2C_get_current_preset )
    {
        i2cResponse[0] = 0 >> 8;
        i2cResponse[1] = 0 >> 0;
        i2cResponseIndex = 0;
        i2cResponseSize = 2;
    }
    else if ( cmd == kI2C_get_current_algorithm )
    {
        i2cResponse[0] = 1;
        i2cResponseIndex = 0;
        i2cResponseSize = 1;
    }
    else if ( cmd == kI2C_dual_get_current_algorithms )
    {
        {
            i2cResponse[0] = 0;
            i2cResponse[1] = 0;
            i2cResponseIndex = 0;
            i2cResponseSize = 2;
        }
    }
    else if ( cmd == kI2C_dual_get_z )
    {
    }
}

int ProcessI2CIn( int b )
{
    displayBlankCountdown = kTimeToBlank;

    if ( b < 0 )
    {
        i2cState = kWantByte1;
        return 0;
    }
    
    switch ( i2cState )
    {
        default:
        case kI2CIdle:
            break;
        case kWantByte1:
            i2cMsg[0] = b;
            switch ( b )
            {
                case kI2C_set_controller:
                case kI2C_set_parameter_abs:
                case kI2C_set_parameter_cont:
                case kI2C_voice_pitch:
                case kI2C_voice_note_on:
                case kI2C_note_pitch:
                case kI2C_note_on:
                case kI2C_dual_set_parameter_cont:
                    i2cState = kWantByte2of4;
                    break;
                case kI2C_send_MIDI_message:
                case kI2C_send_Select_Bus_message:
                    i2cState = kWantByte2ofN;
                    break;
                case kI2C_load_preset:
                case kI2C_save_preset:
                case kI2C_Augustus_Loop_set_pitch:
                case kI2C_dual_set_parameter_abs:
                case kI2C_dual_load_algorithm:
                case kI2C_dual_load_algorithms:
                case kI2C_dual_load_preset:
                case kI2C_dual_save_preset:
                case kI2C_dual_takeover_z:
                case kI2C_set_ES5:
                    i2cState = kWantByte2of3;
                    break;
                case kI2C_load_algorithm:
                case kI2C_get_parameter_value:
                case kI2C_get_parameter_min:
                case kI2C_get_parameter_max:
                case kI2C_WAV_Recorder_record:
                case kI2C_WAV_Recorder_play:
                case kI2C_voice_note_off:
                case kI2C_note_off:
                case kI2C_Looper_get_state:
                case kI2C_dual_get_parameter_value:
                case kI2C_dual_get_parameter_min:
                case kI2C_dual_get_parameter_max:
                case kI2C_dual_get_current_algorithm:
                    i2cState = kWantByte2of2;
                    break;
                case kI2C_reset_preset:
                case kI2C_get_current_preset:
                case kI2C_get_current_algorithm:
                case kI2C_Augustus_Loop_send_clock:
                case kI2C_all_notes_off:
                case kI2C_Looper_clear:
                case kI2C_dual_get_current_algorithms:
                case kI2C_dual_get_z:
                    i2cState = kI2CIdle;
                    ProcessI2C1byteCommand();
                    break;
                default:
                    i2cState = kI2CIdle;
                    break;
            }
            break;
        case kWantByte2of4:
            i2cMsg[1] = b;
            i2cState = kWantByte3of4;
            break;
        case kWantByte3of4:
            i2cMsg[2] = b;
            i2cState = kWantByte4of4;
            break;
        case kWantByte4of4:
            i2cMsg[3] = b;
            i2cState = kI2CIdle;
            ProcessI2C4byteCommand();
            break;
        case kWantByte2ofN:
            i2cMsg[1] = b;
            if ( i2cMsg[0] >= kI2C_send_MIDI_message && i2cMsg[0] <= kI2C_send_Select_Bus_message )
            {
                switch ( i2cMsg[1] & 0xf0 )
                {
                    case 0x80:       // note off
                    case 0x90:       // note on
                    case 0xa0:       // poly pressure
                    case 0xb0:       // control change
                    case 0xe0:       // pitch bend
                        i2cState = kWantByte3of4;
                        break;
                    case 0xc0:       // program change
                    case 0xd0:       // channel pressure
                        i2cState = kWantByte3of3;
                        break;
                    case 0xf0:
                        switch ( i2cMsg[1] & 0x0f )
                        {
                            case 0x0:                   // sysex
                                i2cState = kI2CIdle;
                                break;
                            case 0x1:                   // MTC
                            case 0x3:                   // song select
                                i2cState = kWantByte3of3;
                                break;
                            case 0x2:                   // SPP
                                i2cState = kWantByte3of4;
                                break;
                            default:
                                i2cState = kI2CIdle;
                                ProcessI2CMIDICommand();
                                break;
                        }
                        break;
                    default:
                        i2cState = kI2CIdle;
                        break;
                }
            }
            else
                i2cState = kI2CIdle;
            break;
        case kWantByte2of2:
            i2cMsg[1] = b;
            i2cState = kI2CIdle;
            ProcessI2C2byteCommand();
            break;
        case kWantByte2of3:
            i2cMsg[1] = b;
            i2cState = kWantByte3of3;
            break;
        case kWantByte3of3:
            i2cMsg[2] = b;
            ProcessI2C3byteCommand();
            break;
    }

    return 0;
}
