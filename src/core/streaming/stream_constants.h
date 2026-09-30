// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <chrono>
#include "common/common_types.h"

namespace Core::Streaming {

// Wire protocol version implemented here, per the Unison repo's
// docs/protocol.md -- exact-match only, no major/minor scheme. Mirrors
// GBA_STREAM_PROTOCOL_VERSION in the sibling dolphin-gba-stream project
// (same document, same value, two independent hand-written implementations
// of the same wire format).
//
// 2 -> 4: session_ready.video_port now names a dedicated UDP channel
// carrying Video (this stream type has no outgoing Audio -- see
// bottom_screen_stream.cpp's SendVideoFrame, only Input/Mic stay on the
// TCP control connection), instead of Video staying multiplexed on that
// same connection. See docs/protocol.md's "Dedicated video/audio channel
// (UDP)". Skips the intermediate protocol_version 3 (a second, still-TCP
// video connection) entirely -- that step was superseded before this
// fork ever adopted it.
constexpr int STREAM_PROTOCOL_VERSION = 4;

constexpr char STREAM_TYPE[] = "N3DS_BOTTOM_SCREEN";
constexpr char INPUT_ENCODING[] = "n3ds_touch_and_buttons";
constexpr char EMULATOR_IDENTIFIER[] = "Azahar";

// UDP broadcast port and beacon interval, shared across the whole Unison
// ecosystem -- UNISON_BEACON_PORT is a #define in unison/core/discovery.h,
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
