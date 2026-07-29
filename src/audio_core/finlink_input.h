// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "audio_core/input.h"

namespace Core {
class System;
}

namespace AudioCore {

// Sources the console's microphone from a connected finlink client's own
// real microphone (src/core/streaming/bottom_screen_stream.h) instead of a
// host device -- selected in Settings exactly like Cubeb/OpenAL/Static/
// Null, see input_details.cpp. Read() returns silence (empty) whenever no
// client is connected or none has sent anything yet, the same as NullInput
// would -- there's no error state here, just "nothing available right
// now".
class FinlinkInput final : public Input {
public:
    explicit FinlinkInput(Core::System& system);
    ~FinlinkInput() override;

    void StartSampling(const InputParameters& params) override;
    void StopSampling() override;
    bool IsSampling() override;
    void AdjustSampleRate(u32 sample_rate) override;
    Samples Read() override;

private:
    Core::System& system;
    bool is_sampling = false;
};

} // namespace AudioCore
