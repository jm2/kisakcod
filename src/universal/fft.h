#pragma once

#include <universal/q_shared.h>

struct complex_s // sizeof=0x8
{
    float real;
    float imag;
};
static_assert(sizeof(complex_s) == 8);

void __cdecl FFT_Init(int *fftBitswap, complex_s *fftTrigTable);
void __cdecl FFT(complex_s *data_inout, uint32_t log2_count, int *bitSwap, complex_s *trigTable);
