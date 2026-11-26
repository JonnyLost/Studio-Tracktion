// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace studio::design
{
// Opaque ARGB tokens. Musical states retain distinct semantic colours;
// champagne/brass is an identity accent, never the recording or focus signal.
inline constexpr std::uint32_t rail = 0xff141619u;
inline constexpr std::uint32_t canvas = 0xff191c20u;
inline constexpr std::uint32_t timeline = 0xff20242au;
inline constexpr std::uint32_t panel = 0xff262a30u;
inline constexpr std::uint32_t control = 0xff30353cu;
inline constexpr std::uint32_t raised = 0xff3b4149u;
inline constexpr std::uint32_t divider = 0xff515862u;
inline constexpr std::uint32_t selected = 0xff414d5bu;
inline constexpr std::uint32_t well = 0xff14181du;
inline constexpr std::uint32_t grid = 0xff3b4b61u;
inline constexpr std::uint32_t text = 0xfff3f5f7u;
inline constexpr std::uint32_t secondary = 0xffc0c5cbu;
inline constexpr std::uint32_t action = 0xffd1bd98u;
inline constexpr std::uint32_t focus = 0xff91c8ffu;
inline constexpr std::uint32_t playhead = 0xff2b91ffu;
inline constexpr std::uint32_t warning = 0xffffcb70u;
inline constexpr std::uint32_t record = 0xffef6674u;
inline constexpr std::uint32_t signal = 0xff91d1b8u;
inline constexpr std::uint32_t strings = 0xff9abfe8u;
inline constexpr std::uint32_t winds = 0xff8ed0c3u;
inline constexpr std::uint32_t brass = 0xffe0bf85u;
inline constexpr std::uint32_t percussion = 0xffd7a4bau;
inline constexpr std::uint32_t keys = 0xffb9a8e0u;
inline constexpr std::uint32_t other = 0xffb2becbu;
inline constexpr std::uint32_t midiClip = 0xff365a77u;
inline constexpr std::uint32_t audioClip = 0xff305b5fu;
inline constexpr std::uint32_t roomReturn = 0xffa992d3u;
inline constexpr std::uint32_t atelierAccent = 0xffe0bf85u;

inline constexpr int corner = 5, gap = 4, inset = 8;
inline constexpr float caption = 11.0f, body = 13.0f, section = 15.0f;
}
