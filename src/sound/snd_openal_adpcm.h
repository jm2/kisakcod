#pragma once

// snd_openal_adpcm.h: IMA ADPCM loaded sounds for the OpenAL Soft shim
// (snd_openal.cpp), decoded through libavcodec's adpcm_ima_wav.

#include <cstdint>
#include <vector>

namespace snd_openal
{
// Decodes the whole blocks of an IMA ADPCM sound (WAVE_FORMAT_IMA_ADPCM,
// blockSize bytes per block) into interleaved 16-bit PCM. Fails closed,
// leaving out empty, on a header that can't describe whole blocks inside
// dataLen or on any decode error.
bool DecodeImaAdpcm(const void *data, std::uint32_t dataLen, std::uint32_t blockSize, int channels,
                    std::uint32_t rate, std::vector<std::int16_t> &out);
} // namespace snd_openal
