// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include "audio_core/finlink_input.h"
#include "core/core.h"
#include "core/streaming/bottom_screen_stream.h"

namespace AudioCore {

FinlinkInput::FinlinkInput(Core::System& system_) : system(system_) {}

FinlinkInput::~FinlinkInput() {
    StopSampling();
}

void FinlinkInput::StartSampling(const InputParameters& params) {
    parameters = params;
    is_sampling = true;
    if (auto* stream = system.BottomScreenStream()) {
        stream->SetMicWanted(true, params.sample_rate);
    }
}

void FinlinkInput::StopSampling() {
    is_sampling = false;
    if (auto* stream = system.BottomScreenStream()) {
        stream->SetMicWanted(false, 0);
    }
}

bool FinlinkInput::IsSampling() {
    return is_sampling;
}

void FinlinkInput::AdjustSampleRate(u32 sample_rate) {
    parameters.sample_rate = sample_rate;
    if (is_sampling) {
        if (auto* stream = system.BottomScreenStream()) {
            stream->SetMicWanted(true, sample_rate);
        }
    }
}

Samples FinlinkInput::Read() {
    if (!is_sampling) {
        return {};
    }
    auto* stream = system.BottomScreenStream();
    if (!stream) {
        return {};
    }

    Samples samples = stream->PollMicAudio(); // raw s16le bytes, mono
    if (parameters.sample_size != 8 || samples.empty()) {
        return samples;
    }

    // Cubeb's own input backend hits this same "application wants 8-bit"
    // case (CubebInput::Impl::DataCallback) -- mirror its conversion
    // exactly so an 8-bit-requesting game sees the same kind of data
    // regardless of which Input backend is selected.
    Samples out;
    out.reserve(samples.size() / 2);
    for (size_t i = 0; i + 1 < samples.size(); i += 2) {
        s16 sample;
        std::memcpy(&sample, &samples[i], sizeof(sample));
        out.push_back(static_cast<u8>(static_cast<u16>(sample) >> 8));
    }
    return out;
}

} // namespace AudioCore
