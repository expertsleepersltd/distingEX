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

#ifndef _ALGORITHM_H    /* Guard against multiple inclusion */
#define _ALGORITHM_H

#include "app.h"

enum { k_framesPerBlock = 32 };

typedef union {
    int     f[1];
    struct {
        int     in[3][2*k_framesPerBlock*2];         // double buffered & stereo
        int     out[3][2*k_framesPerBlock*2];
    };
} _algorithm_blocks;

void    algorithm_init(void);
void    algorithm_step( _algorithm_blocks* blocks, int ping );

#endif /* _ALGORITHM_H */
