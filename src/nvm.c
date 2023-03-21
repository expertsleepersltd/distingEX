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

/*

The standard disting EX code memory map is as follows:

          size (KB) size        address
bootloader	32      0x8000      0x1D000000
settings	16      0x4000      0x1D008000
code        1712	0x1AC000	0x1D00C000
autosave	16      0x4000      0x1D1B8000
mappings	128     0x20000     0x1D1BC000
presets     128     0x20000     0x1D1DC000
dm4 presets	16      0x4000      0x1D1FC000
top of memory                   0x1D200000

The flash memory page size is 16KB (0x4000 bytes). The row size is 2KB.

The flash memory is split into two pages, at 0x1D000000 and 0x1D100000.
You can execute code from one page while writing to the other page without stalling.

*/
#include "nvm.h"

#include "peripheral/nvm/plib_nvm.h"

unsigned int NVMUnlock (unsigned int nvmop)
{
    unsigned int status;
    // Suspend or Disable all Interrupts
    asm volatile ("di %0" : "=r" (status));

    // disable audio SPI
    SPI3CONCLR = BIT_15;
    SPI4CONCLR = BIT_15;
    SPI6CONCLR = BIT_15;
    IFS4CLR = BIT_27;               // INT_SOURCE_SPI_3_RECEIVE
    IFS5CLR = BIT_4 | BIT_26;       // INT_SOURCE_SPI_4_RECEIVE | INT_SOURCE_SPI_6_RECEIVE
    
    // Disable DMA
    int dma_susp;
    if ( !( dma_susp = DMACONbits.SUSPEND ) )
    {
        DMACONSET = _DMACON_SUSPEND_MASK; 
        while ( DMACONbits.DMABUSY )
            ;
    }

    // Enable Flash Write/Erase Operations and Select
    // Flash operation to perform
    NVMCON = nvmop;
    // Write Keys
    NVMKEY = 0;
    NVMKEY = NVM_UNLOCK_KEY1;
    NVMKEY = NVM_UNLOCK_KEY2;
    // Start the operation using the Set Register
    NVMCONSET = 0x8000;
    // Wait for operation to complete
    while (NVMCON & 0x8000)
        ;

    // Restore DMA
    if ( !dma_susp )
    {
        DMACONCLR = _DMACON_SUSPEND_MASK;
    }
    // Restore Interrupts
    if ( status & 0x00000001 )
    {
        asm volatile ("ei %0" : "=r" (status));
    }
    else
    {
        asm volatile ("di %0" : "=r" (status));
    }

    // enable audio SPI
    SPI3CONSET = BIT_15;
    SPI4CONSET = BIT_15;
    SPI6CONSET = BIT_15;

    // Disable NVM write enable
    NVMCONCLR = 0x0004000;
    // Return WRERR and LVDERR Error Status Bits
    return (NVMCON & 0x3000);
}

void NVMErasePage( void* ptr )
{
    // Set NVMADDR to the Start Address of page to erase
    NVMADDR = (unsigned int)ptr & 0x1FFFFFFF;
    // Unlock and Erase Page
    unsigned int res = NVMUnlock( 0x4004 );
}

void NVMWriteWord( void* ptr, DWORD data )
{
    // Load data into NVMDATA register
    NVMDATA0 = data;
    // Load address to program into NVMADDR register
    NVMADDR = (unsigned int)ptr & 0x1FFFFFFF;
    // Unlock and Write Word
    unsigned int res = NVMUnlock( 0x4001 );
}

void NVMWriteRow( void* ptr, const DWORD* data )
{
    // Load data address into NVMSRCADDR register
    NVMSRCADDR = (unsigned int)data & 0x1FFFFFFF;
    // Load address to program into NVMADDR register
    NVMADDR = (unsigned int)ptr & 0x1FFFFFFF;
    // Unlock and Write Row
    unsigned int res = NVMUnlock( 0x4003 );
}

unsigned int NVMOpWithAudioService( unsigned int nvmop )
{
    unsigned int status;
    // Suspend or Disable all Interrupts
    asm volatile ( "di %0" : "=r" (status) );
    
    // Disable DMA
    int dma_susp;
    if ( !( dma_susp = DMACONbits.SUSPEND ) )
    {
        DMACONSET = _DMACON_SUSPEND_MASK; 
        while ( DMACONbits.DMABUSY )
            ;
    }

    // Enable Flash Write/Erase Operations and Select
    // Flash operation to perform
    NVMCON = nvmop;
    // Write Keys
    NVMKEY = 0;
    NVMKEY = NVM_UNLOCK_KEY1;
    NVMKEY = NVM_UNLOCK_KEY2;
    // Start the operation using the Set Register
    NVMCONSET = 0x8000;

    // Restore DMA
    if ( !dma_susp )
    {
        DMACONCLR = _DMACON_SUSPEND_MASK;
    }
    // Restore Interrupts
    if ( status & 0x00000001 )
    {
        asm volatile ( "ei %0" : "=r" (status) );
    }

    // Wait for operation to complete
    while ( NVMCON & 0x8000 )
    {
        CHECK_SERVICE_AUDIO_INTERNAL
    }

    // Disable NVM write enable
    NVMCONCLR = 0x0004000;

    // Return WRERR and LVDERR Error Status Bits
    return (NVMCON & 0x3000);
}
