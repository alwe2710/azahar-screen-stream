// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <chrono>
#include "common/common_types.h"

namespace Core::Streaming {

// Wire protocol version implemented here, per the finlink repo's
// docs/protocol.md -- exact-match only, no major/minor scheme. Mirrors
// GBA_STREAM_PROTOCOL_VERSION in the sibling dolphin-gba-stream project
// (same document, same value, two independent hand-written implementations
// of the same wire format).
constexpr int STREAM_PROTOCOL_VERSION = 2;

constexpr char STREAM_TYPE[] = "N3DS_BOTTOM_SCREEN";
constexpr char INPUT_ENCODING[] = "n3ds_touch";
constexpr char EMULATOR_IDENTIFIER[] = "Azahar";

// UDP broadcast port and beacon interval, shared across the whole finlink
// ecosystem -- FINLINK_BEACON_PORT is a #define in finlink/core/discovery.h,
// re-declared here as a typed constant rather than included directly since
// this header is C++-only and that one is written for C callers too.
constexpr u16 BEACON_PORT = 6805;
constexpr std::chrono::milliseconds BEACON_INTERVAL{2000};

// Bottom screen's fixed native resolution (Core::kScreenBottomWidth/Height,
// see src/core/3ds.h) -- this stream type is deliberately not negotiable the
// way GC_GBA_LINK's video is: there's exactly one screen size, no per-client
// downscaling, so no VideoLimits/NegotiatedVideo dance is needed here.
constexpr u32 STREAM_WIDTH = 320;
constexpr u32 STREAM_HEIGHT = 240;
constexpr double STREAM_FPS = 60.0;

} // namespace Core::Streaming
