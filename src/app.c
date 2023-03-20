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
#include "display.h"
#include "i2c.h"
#include "algorithm.h"

#include "peripheral/spi/plib_spi.h"
#include "peripheral/tmr/plib_tmr.h"
#include "peripheral/usart/plib_usart.h"
#include "peripheral/dma/plib_dma.h"
#include "system_config/default/framework/system/devcon/src/sys_devcon_local.h"

#include <math.h>
#include <stdio.h>

#define VirtToPhys( addr )  ( 0x1FFFFFFF & (UINT32)(addr) )

const int magic
__attribute__((address(0xBD00C370)))
 = 0xbadabef5;

unsigned int time = 0;
int slowTimeCountdown = kSlowTimeRatio;

_adcs adcs __attribute__((aligned(16))) = { 0 };

_halfState halfState[2] = { 0 };
_input_calibration inputCalibrations[6];
BYTE pageBuffer[0x4000] __attribute__((aligned(16))) __attribute__((coherent)) = { 0 };

_algorithm_blocks blocks  __attribute__((aligned(16))) __attribute__((coherent)) = { 0 };

// keep these close for cache locality
short i2cRxQueueRead = -1;
short i2cRxQueueWrite = 0;
char selectRxQueueRead = -1;
char selectRxQueueWrite = 0;
short midiRxQueueRead = -1;
short midiRxQueueWrite = 0;

volatile BYTE recallIgnoreBytes = 0;
BYTE midiOutPending = 0;

BYTE selectRxQueue[kSelectRxQueueSize] = { 0 };

#define kMIDIRxQueueSize (1024)

BYTE midiRxQueue[kMIDIRxQueueSize] = { 0 };

short i2cRxQueue[kI2CRxQueueSize] = { 0 };

MIDIMessageHandler midiMessageHandler = DefaultMIDIMessageHandler;

BYTE doServiceAudio = 0;

APP_DATA appData;

void delayMs( unsigned int ms )
{
    while ( ms )
    {
        ms -= 1;
        
        T3CONCLR = TxCON_ON_MASK;
        IFS0CLR = _IFS0_T3IF_MASK;
        TMR3 = 0;
        T3CONSET = TxCON_ON_MASK;

        while ( !IFS0bits.T3IF )
            ;
    }

    T3CONCLR = TxCON_ON_MASK;
}

void configureADC()
{
    ADC7CFG = DEVADC7;
    
    ADCCON1 = ( 3 << _ADCCON1_SELRES_POSITION ) | ( 1 << _ADCCON1_STRGSRC_POSITION );
    ADCCON2 = ( 14 << _ADCCON2_SAMC_POSITION ) | ( 1 << _ADCCON2_ADCDIV_POSITION ) | ( 1 << _ADCCON2_EOSIEN_POSITION );
    ADCANCON = ( 0xA << _ADCANCON_WKUPCLKCNT_POSITION );
    ADCCON3 = ( 8 << _ADCCON3_CONCLKDIV_POSITION ) | ( 1 << _ADCCON3_ADCSEL_POSITION );
    ADCIMCON2 = 0;
    ADCIMCON3 = 0;
    ADCTRGSNS = 0;
    ADCCSS1 = _ADCCSS1_CSS27_MASK | _ADCCSS1_CSS28_MASK;
    ADCCSS2 = _ADCCSS2_CSS38_MASK | _ADCCSS2_CSS39_MASK;
    ADCCON1bits.ON = 1;
    while ( !ADCCON2bits.BGVRRDY )
        ;
    while ( ADCCON2bits.REFFLT )
        ;
    ADCANCONbits.ANEN7 = 1;
    while ( !ADCANCONbits.WKRDY7 )
        ;
    ADCCON3bits.DIGEN7 = 1;

    delayMs( 1 );
    
    // trigger a conversion
    ADCCON3bits.GSWTRG = 1;
}

static inline __attribute__((always_inline)) void processADCZs(void)
{
    int rawZ0 = adcs.rawZ[0];
    adcs.Z[0].value = adcs.Z[0].value - adcs.Z[0].samples[ adcs.Z[0].pos ] + rawZ0;
    adcs.Z[0].samples[ adcs.Z[0].pos ] = rawZ0;
    adcs.Z[0].pos = ( adcs.Z[0].pos + 1 ) & ( kNumZsamples-1 );

    int rawZ1 = adcs.rawZ[1];
    adcs.Z[1].value = adcs.Z[1].value - adcs.Z[1].samples[ adcs.Z[1].pos ] + rawZ1;
    adcs.Z[1].samples[ adcs.Z[1].pos ] = rawZ1;
    adcs.Z[1].pos = ( adcs.Z[1].pos + 1 ) & ( kNumZsamples-1 );
}

static inline __attribute__((always_inline)) void readAndTriggerADCs(void)
{
    if ( ADCDSTAT2bits.ARDY39 )
    {
        adcs.CV = ADCDATA27;
        adcs.Gate = ADCDATA28;
        adcs.rawZ[0] = ADCDATA38;
        adcs.rawZ[1] = ADCDATA39;
        ADCCON3bits.GSWTRG = 1;
        
        processADCZs();
    }
}

void setupAudioDMAs( int framesPerBlock, int* blocks )
{
    int framesPerDMA = 2 * framesPerBlock;
    
    // abort DMAs
    DCH6ECONSET = BIT_6;
    DCH0ECONSET = BIT_6;
    DCH7ECONSET = BIT_6;
    DCH3ECONSET = BIT_6;
    DCH5ECONSET = BIT_6;
    DCH4ECONSET = BIT_6;
    while ( DCH6CONbits.CHBUSY | DCH0CONbits.CHBUSY | DCH7CONbits.CHBUSY | DCH3CONbits.CHBUSY | DCH5CONbits.CHBUSY | DCH4CONbits.CHBUSY )
        ;

    // clear memory
    memset( blocks, 0, 6 * framesPerDMA * 8 );

    // stop SPI which triggers DMA
    SPI6CONCLR = BIT_15;
    
    // set up new DMAs
    DCH5DSA = VirtToPhys( blocks );
    DCH5DSIZ = framesPerDMA * 8;
    blocks += framesPerDMA * 2;
    DCH6DSA = VirtToPhys( blocks );
    DCH6DSIZ = framesPerDMA * 8;
    blocks += framesPerDMA * 2;
    DCH7DSA = VirtToPhys( blocks );
    DCH7DSIZ = framesPerDMA * 8;
    blocks += framesPerDMA * 2;
    DCH3SSA = VirtToPhys( blocks );
    DCH3SSIZ = framesPerDMA * 8;
    blocks += framesPerDMA * 2;
    DCH0SSA = VirtToPhys( blocks );
    DCH0SSIZ = framesPerDMA * 8;
    blocks += framesPerDMA * 2;
    DCH4SSA = VirtToPhys( blocks );
    DCH4SSIZ = framesPerDMA * 8;
    
    // enable DMAs
    DCH6CONSET = BIT_7;
    DCH0CONSET = BIT_7;
    DCH7CONSET = BIT_7;
    DCH3CONSET = BIT_7;
    DCH5CONSET = BIT_7;
    DCH4CONSET = BIT_7;

    // restart SPI
    SPI6CONSET = BIT_15;
}

void APP_Initialize ( void )
{
    /* Place the App state machine in its initial state. */
    appData.state = APP_STATE_INIT;
    
    // undo bootloader settings that we no longer use
    PLIB_INT_SourceDisable( INT_ID_0, INT_SOURCE_DMA_0 );
    _pic32_flush_dcache();

    // EBI
    
    CFGEBIA = 0x803FFFFF;
    // EBIWEEN | EBIOEEN | EBIBSEN1 | EBIBSEN0 | EBICSEN0 | EBIDEN1 | EBIDEN0;
    CFGEBIC = BIT_13 | BIT_12 | BIT_9 | BIT_8 | BIT_4 | BIT_1 | BIT_0;
    
    EBICS0 = 0x20000000;
    // REGSEL=EBISMT0, MEMTYPE=SRAM, MEMSIZE=8MB
    EBIMSK0 = ( 0 << 8 ) | ( 1 << 5 ) | ( 8 << 0 );
    //        PAGESIZE          TPRC          TBTA          TWP           TWR          TAS          TRC
    EBISMT0 = ( 2 << 24 ) | 0 | ( 2 << 19 ) | ( 0 << 16 ) | ( 3 << 10 ) | ( 1 << 8 ) | ( 1 << 6 ) | ( 6 << 0 );
    EBISMCON = 0;
    
    // write to configuration register to enable page mode
    int sram_config = BIT_7 | ( 3 << 5 ) | BIT_4;
    // software access sequence
    pageBuffer[0] = *(volatile int16_t*)( SRAM_ADDR_UNCACHED + SRAM_SIZE - 2 );
    pageBuffer[1] = *(volatile int16_t*)( SRAM_ADDR_UNCACHED + SRAM_SIZE - 2 );
    *(volatile int16_t*)( SRAM_ADDR_UNCACHED + SRAM_SIZE - 2 ) = 0;
    *(volatile int16_t*)( SRAM_ADDR_UNCACHED + SRAM_SIZE - 2 ) = sram_config;
    
    EBISMT0bits.PAGEMODE = 1;

    PLIB_DMA_Enable( DMA_ID_0 );

    // SPI1 - FHX expansion
    
    PLIB_PORTS_RemapOutput( PORTS_ID_0, OUTPUT_FUNC_SDO1, OUTPUT_PIN_RPD7 );
    PLIB_PORTS_RemapOutput( PORTS_ID_0, OUTPUT_FUNC_SS1, OUTPUT_PIN_RPD4 );
    PLIB_PORTS_RemapInput( PORTS_ID_0, INPUT_FUNC_SDI1, INPUT_PIN_RPD6 );

    PLIB_SPI_MasterEnable( SPI_ID_1 );
    PLIB_SPI_PinEnable( SPI_ID_1, SPI_PIN_DATA_IN );
    PLIB_SPI_PinEnable( SPI_ID_1, SPI_PIN_SLAVE_SELECT );
    PLIB_SPI_CommunicationWidthSelect( SPI_ID_1, SPI_COMMUNICATION_WIDTH_32BITS );
    PLIB_SPI_SlaveSelectEnable( SPI_ID_1 );
    PLIB_SPI_BaudRateSet( SPI_ID_1, SYS_CLK_BUS_PERIPHERAL_2, 3000000 );    
    PLIB_SPI_Enable( SPI_ID_1 );
#ifdef SPI1_IS_EXT_DISPLAY
    // ext display reset - EXP0 RJ1
    // CS high - EXP2 RJ3
    PORTJSET = BIT_1 | BIT_3;
    PORTDCLR = BIT_4;
    PLIB_SPI_Disable( SPI_ID_1 );
    PLIB_SPI_BaudRateSet( SPI_ID_1, SYS_CLK_BUS_PERIPHERAL_2, 800000 );    
    SPI1STATCLR = 0x40;
    // ENHBUF | ON | CKE | MSTEN | DISSDI
    SPI1CON = 0x18130;
#endif
    
    // SPI5 - display
    
    PLIB_PORTS_RemapOutput( PORTS_ID_0, OUTPUT_FUNC_SDO5, OUTPUT_PIN_RPB10 );

    IEC5CLR = (_IEC5_SPI5EIE_MASK | _IEC5_SPI5TXIE_MASK | _IEC5_SPI5RXIE_MASK);
    IFS5CLR = (_IFS5_SPI5EIF_MASK | _IFS5_SPI5TXIF_MASK | _IFS5_SPI5RXIF_MASK);
    SPI5CON = 0;
    (void)SPI5BUF;
    // datasheet max is 10MHz
    // data througput is 128x32x30 bits/second = 122880
    PLIB_SPI_BaudRateSet( SPI_ID_5, SYS_CLK_BUS_PERIPHERAL_2, 800000 );
    SPI5STATCLR = 0x40;
    // ENHBUF | ON | CKE | MSTEN | DISSDI
    SPI5CON = 0x18130;
    
    // SPI3 - audio

    // CHSIRQ | SIRQEN
    DCH6ECON = ( DMA_TRIGGER_SPI_6_RECEIVE << 8 ) | BIT_4;
    DCH6INT = 0;
    DCH6SSA = VirtToPhys( &SPI3BUF );
    DCH6SSIZ = 4;
    DCH6DSIZ = 8;
    DCH6CSIZ = 8;
    // CHAEN | priority 1
    DCH6CON = BIT_4 | 1;

    // CHSIRQ | SIRQEN
    DCH0ECON = ( DMA_TRIGGER_SPI_6_RECEIVE << 8 ) | BIT_4;
    DCH0INT = 0;
    DCH0DSA = VirtToPhys( &SPI3BUF );
    DCH0SSIZ = 8;
    DCH0DSIZ = 4;
    DCH0CSIZ = 8;
    // CHAEN | priority 1
    DCH0CON = BIT_4 | 1;

    PLIB_PORTS_RemapOutput( PORTS_ID_0, OUTPUT_FUNC_SDO3, OUTPUT_PIN_RPC13 );
    PLIB_PORTS_RemapInput( PORTS_ID_0, INPUT_FUNC_SDI3, INPUT_PIN_RPA14 );
    PLIB_PORTS_RemapInput( PORTS_ID_0, INPUT_FUNC_SS3, INPUT_PIN_RPF12 );

    // SPISGNEXT | IGNROV | IGNTUR | AUDEN | AUDMOD;
    SPI3CON2 = BIT_15 | BIT_9 | BIT_8 | BIT_7 | (1<<0);
    // SSEN | CKP | MODE16 | MODE32 | FRMPOL | ON | ENHBUF | STXISEL=3 | SRXISEL=3;
    SPI3CON = BIT_7 | BIT_6 | BIT_11 | BIT_10 | BIT_29 | BIT_15 | BIT_16 | (3<<2) | (3<<0);
    
    // SPI4 - audio

    // CHSIRQ | SIRQEN
    DCH7ECON = ( DMA_TRIGGER_SPI_6_RECEIVE << 8 ) | BIT_4;
    DCH7INT = 0;
    DCH7SSA = VirtToPhys( &SPI4BUF );
    DCH7SSIZ = 4;
    DCH7DSIZ = 8;
    DCH7CSIZ = 8;
    // CHAEN | priority 1
    DCH7CON = BIT_4 | 1;

    // CHSIRQ | SIRQEN
    DCH3ECON = ( DMA_TRIGGER_SPI_6_RECEIVE << 8 ) | BIT_4;
    DCH3INT = 0;
    DCH3DSA = VirtToPhys( &SPI4BUF );
    DCH3SSIZ = 8;
    DCH3DSIZ = 4;
    DCH3CSIZ = 8;
    // CHAEN | priority 1
    DCH3CON = BIT_4 | 1;

    PLIB_PORTS_RemapOutput( PORTS_ID_0, OUTPUT_FUNC_SDO4, OUTPUT_PIN_RPD11 );
    PLIB_PORTS_RemapInput( PORTS_ID_0, INPUT_FUNC_SDI4, INPUT_PIN_RPA15 );
    PLIB_PORTS_RemapInput( PORTS_ID_0, INPUT_FUNC_SS4, INPUT_PIN_RPB15 );

    // SPISGNEXT | IGNROV | IGNTUR | AUDEN | AUDMOD;
    SPI4CON2 = BIT_15 | BIT_9 | BIT_8 | BIT_7 | (1<<0);
    // SSEN | CKP | MODE16 | MODE32 | FRMPOL | ON | ENHBUF | STXISEL=3 | SRXISEL=3;
    SPI4CON = BIT_7 | BIT_6 | BIT_11 | BIT_10 | BIT_29 | BIT_15 | BIT_16 | (3<<2) | (3<<0);
    
    // SPI2 - MicroSD

    PLIB_PORTS_RemapOutput( PORTS_ID_0, OUTPUT_FUNC_SDO2, OUTPUT_PIN_RPB5 );
    PLIB_PORTS_RemapInput( PORTS_ID_0, INPUT_FUNC_SDI2, INPUT_PIN_RPB3 );
    
    // SPI6 - audio

    // CHSIRQ | SIRQEN
    DCH5ECON = ( DMA_TRIGGER_SPI_6_RECEIVE << 8 ) | BIT_4;
    DCH5INT = 0;
    DCH5SSA = VirtToPhys( &SPI6BUF );
    DCH5SSIZ = 4;
    DCH5DSIZ = 8;
    DCH5CSIZ = 8;
    // CHAEN | priority 1
    DCH5CON = BIT_4 | 1;

    // CHSIRQ | SIRQEN
    DCH4ECON = ( DMA_TRIGGER_SPI_6_RECEIVE << 8 ) | BIT_4;
    DCH4INT = 0;
    DCH4DSA = VirtToPhys( &SPI6BUF );
    DCH4SSIZ = 8;
    DCH4DSIZ = 4;
    DCH4CSIZ = 8;
    // CHAEN | priority 1
    DCH4CON = BIT_4 | 1;

    PLIB_PORTS_RemapOutput( PORTS_ID_0, OUTPUT_FUNC_SDO6, OUTPUT_PIN_RPD5 );
    PLIB_PORTS_RemapInput( PORTS_ID_0, INPUT_FUNC_SDI6, INPUT_PIN_RPD0 );
    PLIB_PORTS_RemapInput( PORTS_ID_0, INPUT_FUNC_SS6, INPUT_PIN_RPC14 );

    // SPISGNEXT | IGNROV | IGNTUR | AUDEN | AUDMOD;
    SPI6CON2 = BIT_15 | BIT_9 | BIT_8 | BIT_7 | (1<<0);
    // SSEN | CKP | MODE16 | MODE32 | FRMPOL | ON | ENHBUF | STXISEL=3 | SRXISEL=3;
    SPI6CON = BIT_7 | BIT_6 | BIT_11 | BIT_10 | BIT_29 | BIT_15 | BIT_16 | (3<<2) | (3<<0);
    
    setupAudioDMAs( k_framesPerBlock, blocks.f );

    // UART2 - select

    PLIB_PORTS_RemapInput( PORTS_ID_0, INPUT_FUNC_U2RX, INPUT_PIN_RPB7 );
    PLIB_PORTS_RemapOutput( PORTS_ID_0, OUTPUT_FUNC_U2TX, OUTPUT_PIN_RPB6 );

	PLIB_INT_VectorPrioritySet( INT_ID_0, INT_VECTOR_UART2_RX, INT_PRIORITY_LEVEL4 );
    PLIB_INT_VectorSubPrioritySet( INT_ID_0, INT_VECTOR_UART2_RX, INT_SUBPRIORITY_LEVEL0 );
    PLIB_INT_SourceEnable( INT_ID_0, INT_SOURCE_USART_2_RECEIVE );
    
    PLIB_USART_InitializeOperation( USART_ID_2, USART_RECEIVE_FIFO_ONE_CHAR, USART_TRANSMIT_FIFO_NOT_FULL, USART_ENABLE_TX_RX_USED );
    PLIB_USART_TransmitterEnable( USART_ID_2 );
    PLIB_USART_ReceiverEnable( USART_ID_2 );
    PLIB_USART_BaudSetAndEnable( USART_ID_2, SYS_CLK_BUS_PERIPHERAL_2, 31250 );

    // UART4 - MIDI

    PLIB_PORTS_RemapInput( PORTS_ID_0, INPUT_FUNC_U4RX, INPUT_PIN_RPE8 );
    PLIB_PORTS_RemapOutput( PORTS_ID_0, OUTPUT_FUNC_U4TX, OUTPUT_PIN_RPE9 );

	PLIB_INT_VectorPrioritySet( INT_ID_0, INT_VECTOR_UART4_RX, INT_PRIORITY_LEVEL1 );
    PLIB_INT_VectorSubPrioritySet( INT_ID_0, INT_VECTOR_UART4_RX, INT_SUBPRIORITY_LEVEL0 );
    PLIB_INT_SourceEnable( INT_ID_0, INT_SOURCE_USART_4_RECEIVE );
    
    PLIB_USART_InitializeOperation( USART_ID_4, USART_RECEIVE_FIFO_ONE_CHAR, USART_TRANSMIT_FIFO_NOT_FULL, USART_ENABLE_TX_RX_USED );
    PLIB_USART_TransmitterEnable( USART_ID_4 );
    PLIB_USART_ReceiverEnable( USART_ID_4 );
    PLIB_USART_BaudSetAndEnable( USART_ID_4, SYS_CLK_BUS_PERIPHERAL_2, 31250 );

    // timers are based on PBCLK3 - 83968000 Hz
    
    // timer 2 for OC
    PLIB_TMR_Mode16BitEnable( TMR_ID_2 );
    PLIB_TMR_PrescaleSelect( TMR_ID_2, TMR_PRESCALE_VALUE_4 );
    PLIB_TMR_Period16BitSet( TMR_ID_2, 1023 );
    PLIB_TMR_Start( TMR_ID_2 );

    // timer 3 for 1ms
    PLIB_TMR_Mode16BitEnable( TMR_ID_3 );
    PLIB_TMR_PrescaleSelect( TMR_ID_3, TMR_PRESCALE_VALUE_64 );
    PLIB_TMR_Period16BitSet( TMR_ID_3, (SYS_CLK_BUS_PERIPHERAL_3/64)/1000 );

    // timer 5 for expander DAC comms gap
    // ( 83968000 / 1 ) / 980 = 85681
    PLIB_TMR_Mode16BitEnable( TMR_ID_5 );
    PLIB_TMR_PrescaleSelect( TMR_ID_5, TMR_PRESCALE_VALUE_1 );
    PLIB_TMR_Period16BitSet( TMR_ID_5, 980 );
    PLIB_TMR_Start( TMR_ID_5 );

    // OC8 & OC9 - Z LED
    
    PLIB_PORTS_RemapOutput( PORTS_ID_0, OUTPUT_FUNC_OC8, OUTPUT_PIN_RPF8 );
    PLIB_PORTS_RemapOutput( PORTS_ID_0, OUTPUT_FUNC_OC9, OUTPUT_PIN_RPB2 );
    OC8RS = 100;
    OC8R = 100;
    OC8CON = 0x8006;        // ON | PWM
    OC9RS = 100;
    OC9R = 100;
    OC9CON = 0x8006;        // ON | PWM

    // REFCLKO4
    
    PLIB_PORTS_RemapOutput( PORTS_ID_0, OUTPUT_FUNC_REFCLKO4, OUTPUT_PIN_RPD14 );
    // RC15 low to enable REFCLKO4
    //PORTCCLR = BIT_15;
    // disable the clock if we're not using it
    PLIB_OSC_ReferenceOscDisable( OSC_ID_0, OSC_REFERENCE_4 );
    
    configureADC();

    // bring OLED out of reset
    delayMs( 10 );
    PORTJSET = BIT_8;
    
#ifdef SPI1_IS_EXT_DISPLAY
    // ext display reset - EXP0 RJ1
    delayMs( 10 );
    PORTJCLR = BIT_1;
    delayMs( 10 );
    PORTJSET = BIT_1;
#endif    
    
    readAndTriggerADCs();

    configureDisplay();

#ifdef SPI1_IS_EXT_DISPLAY
    configureDisplay2();
#endif    

    startupSequence();

    setDisplayContrast( 128 );
    setDisplayFlip( 0 );
    
    ReadCalibrationFromSettings();

#ifdef SPI1_IS_EXT_DISPLAY
    if ( 1 )
        PLIB_SPI_Disable( SPI_ID_1 );
#endif
    
	// bring codec out of reset
	PORTHSET = BIT_11;
	// wait 3846 SCKI cycles
    delayMs( 1 );
    
    ConfigureCodec();
    
    configureI2CSlave();
    
    // SD card CS
    LATBbits.LATB11 = 1;
    TRISBbits.TRISB11 = 0;
    
#ifndef SPI1_IS_EXT_DISPLAY
    setupFHX();
#endif

    halfState[0].encA = 1;
    halfState[0].encB = 1;
    halfState[0].encSW = 1;
    halfState[0].potSW = 1;
    halfState[0].lastEncA = 1;
    halfState[1].encA = 1;
    halfState[1].encB = 1;
    halfState[1].encSW = 1;
    halfState[1].potSW = 1;
    halfState[1].lastEncA = 1;
}

void __ISR(_UART2_RX_VECTOR, ipl4srs) UART2RXInterruptHandler(void)
// select bus
{
    if ( PLIB_INT_SourceFlagGet( INT_ID_0, INT_SOURCE_USART_2_RECEIVE ) )
    {
        if ( U2STAbits.URXDA )
        {
            // byte received
            BYTE data = U2RXREG;
            if ( recallIgnoreBytes > 0 )
                recallIgnoreBytes -= 1;
            else
            {
                if ( selectRxQueueRead == selectRxQueueWrite )
                {
                    // queue overflow
                }
                else
                {
                    selectRxQueue[ selectRxQueueWrite ] = data;
                    selectRxQueueWrite = ( selectRxQueueWrite + 1 ) & ( kSelectRxQueueSize-1 );
                }
            }
        }
        PLIB_INT_SourceFlagClear( INT_ID_0, INT_SOURCE_USART_2_RECEIVE );
    }
}

void __ISR(_UART4_RX_VECTOR, ipl1srs) UART4RXInterruptHandler(void)
// midi
{
    if ( PLIB_INT_SourceFlagGet( INT_ID_0, INT_SOURCE_USART_4_RECEIVE ) )
    {
        if ( U4STAbits.URXDA )
        {
            // byte received
            BYTE data = U4RXREG;
            
            // handle thru
            if ( 0 )
            {
                while ( U4STAbits.UTXBF )
                    ;
                U4TXREG = data;
            }
            if ( 0 )
            {
                while ( U2STAbits.UTXBF )
                    ;
                U2TXREG = data;
                recallIgnoreBytes += 1;
            }
            
            if ( midiRxQueueRead == midiRxQueueWrite )
            {
                // queue overflow
            }
            else
            {
                midiRxQueue[ midiRxQueueWrite ] = data;
                midiRxQueueWrite = ( midiRxQueueWrite + 1 ) & ( kMIDIRxQueueSize-1 );
            }
        }
        PLIB_INT_SourceFlagClear( INT_ID_0, INT_SOURCE_USART_4_RECEIVE );
    }
}

int quickSRAMtest(void)
{
    int32_t* sram = (int32_t*)SRAM_ADDR_UNCACHED;
    
    int i;
    for ( i=0; i<64; ++i )
        sram[i] = i | 0x1337BB00;
    
    sram[500000] = 0x5555aaaa;
    sram[1000000] = 0xfeedbeef;
    sram[1500000] = 0x5555aaaa;
    sram[2000000] = 0xfeedbeef;
    
    for ( i=0; i<64; ++i )
    {
        if ( sram[i] != ( i | 0x1337BB00 ) )
            return 0;
    }
    if ( sram[500000] != 0x5555aaaa )
        return 0;
    if ( sram[1000000] != 0xfeedbeef )
        return 0;
    if ( sram[1500000] != 0x5555aaaa )
        return 0;
    if ( sram[2000000] != 0xfeedbeef )
        return 0;

    sram[0] = 0;
    sram[500000] = 0;
    sram[1000000] = 0;
    sram[1500000] = 0;
    sram[2000000] = 0;
    
    return 1;
}

void APP_Tasks ( void )
{

    /* Check the application's current state. */
    switch ( appData.state )
    {
        /* Application's initial state. */
        case APP_STATE_INIT:
        {
            bool appInitialized = true;
       
        
            if (appInitialized)
            {
            
                appData.state = APP_STATE_SERVICE_TASKS;
            }
            break;
        }

        case APP_STATE_SERVICE_TASKS:
        {
            if ( !quickSRAMtest() )
            {
                displayMessage4x16( "Non-recoverable", "error - restart", "or proceed", "and run tests" );
            }
            algorithm_init();
            doServiceAudio = 1;
            displayLoop();
            break;
        }

        /* The default state should never be executed. */
        default:
        {
            break;
        }
    }
}

void serviceAudioSingle(void)
{
    // check and change algorithm
    if ( 0 )
    {
        doServiceAudio = 0;
        algorithm_init();
        doServiceAudio = 1;
    }
    
    serviceAudioInternalSingle();
}

void serviceAudioInternalSingle(void)
// can do anything but change algorithm
{
    PORTJINV = BIT_11;
    readAndTriggerADCs();
    PORTJINV = BIT_11;

    if ( doServiceAudio )
    {
        int i;
        for ( i=0; i<2; ++i )
        {
            time += k_framesPerBlock;

            PORTBSET = BIT_4;

            algorithm_step( &blocks, i ? 0 : (k_framesPerBlock*2) );

            PORTBCLR = BIT_4;
            
            FlushMIDIRx();
            
            if ( i )
                break;

            DCH5INTCLR = BIT_4 | BIT_5;
            for ( ;; )
            {
                int bits = DCH5INT;
                if ( bits & BIT_4 )
                {
                    // half buffer done
                    break;
                }
            }
        }
    }

    // update Z LEDs
    int oc = 512 + ( (  blocks.in[2][0] - halfState[1].A[2] ) >> 13 );
    APPLY_RANGE( oc, 0, 1023 );
    OC9RS = oc;
    oc = 512 + ( ( blocks.in[2][1] - halfState[0].A[2] ) >> 13 );
    APPLY_RANGE( oc, 0, 1023 );
    OC8RS = oc;
    
    if ( midiOutPending )
        HandleMIDIOut();
    
    int ph = PORTH;
    int pb = PORTB;
    halfState[0].potSW = ( pb >> 12 ) & 1;
    halfState[1].potSW = ( ph >> 15 ) & 1;

    slowTimeCountdown -= 2 * k_framesPerBlock;
    if ( slowTimeCountdown <= 0 )
    {
        slowTimeCountdown = kSlowTimeRatio;
        
        halfState[0].encA = ( ph >> 4 ) & 1;
        halfState[0].encB = ( ph >> 5 ) & 1;
        halfState[0].encSW = ( ph >> 6 ) & 1;
        halfState[1].encA = ( ph >> 12 ) & 1;
        halfState[1].encB = ( ph >> 13 ) & 1;
        halfState[1].encSW = ( ph >> 14 ) & 1;
        
        int i;
        for ( i=0; i<2; ++i )
        {
            if ( !halfState[i].encB )
            {
                if ( !halfState[i].encA && halfState[i].lastEncA )
                    halfState[i].encoderCounter += 1;
                else if ( halfState[i].encA && !halfState[i].lastEncA )
                    halfState[i].encoderCounter -= 1;
            }
            halfState[i].lastEncA = halfState[i].encA;
        }
    }
}

void FlushMIDIRx(void)
{
    for ( ;; )
    {
        // check I2C RX
        int nextRead = ( i2cRxQueueRead + 1 ) & ( kI2CRxQueueSize-1 );
        if ( nextRead == i2cRxQueueWrite )
            break;
        {
            i2cRxQueueRead = nextRead;
            int data = i2cRxQueue[ nextRead ];
            ProcessI2CIn( data );
        }
    }

    for ( ;; )
    {
        // check Select RX
        int nextRead = ( selectRxQueueRead + 1 ) & ( kSelectRxQueueSize-1 );
        if ( nextRead == selectRxQueueWrite )
            break;
        {
            selectRxQueueRead = nextRead;
            BYTE data = selectRxQueue[ nextRead ];
            Recall_ProcessMIDI( data );
        }
    }
    
    for ( ;; )
    {
        // check MIDI RX
        int nextRead = ( midiRxQueueRead + 1 ) & ( kMIDIRxQueueSize-1 );
        if ( nextRead == midiRxQueueWrite )
            break;
        {
            midiRxQueueRead = nextRead;
            BYTE data = midiRxQueue[ nextRead ];
            ProcessMIDIIn( data );
        }
    }
}

void __attribute__((noreturn)) _fassert(int line, const char *file, const char *expr, const char *func)
{
    char* ptr = pageBuffer;
    strcpy( ptr, file );
    ptr += strlen( ptr ) + 1;
    strcpy( ptr, expr );
    ptr += strlen( ptr ) + 1;
    strcpy( ptr, func );
    ptr += strlen( ptr ) + 1;
    while ( 1 )
    {
        PORTBINV = BIT_4;
        delayMs( 1 );
    }
}
