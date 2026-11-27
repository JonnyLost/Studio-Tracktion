// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
namespace studio::themes
{
enum Role : unsigned { rail, canvas, timeline, panel, control, raised, divider, selected, well, grid, text, secondary, action, focus, playhead, warning, record, signal, atelierAccent, midiClip, audioClip, count };
struct Palette { const char* name; std::array<std::uint32_t,count> argb; bool light; };
inline constexpr std::array<Palette,7> palettes {{
    { "Studio Classic", { 0xff141619u, 0xff191c20u, 0xff20242au, 0xff262a30u, 0xff30353cu, 0xff3b4149u, 0xff515862u, 0xff414d5bu, 0xff14181du, 0xff3b4b61u, 0xfff3f5f7u, 0xffc0c5cbu, 0xffd1bd98u, 0xff91c8ffu, 0xff2b91ffu, 0xffffcb70u, 0xffef6674u, 0xff91d1b8u, 0xffe0bf85u, 0xff365a77u, 0xff305b5fu }, false },
    { "Graphite Red", { 0xff141414u, 0xff191919u, 0xff232323u, 0xff292929u, 0xff303030u, 0xff3b3b3bu, 0xff686868u, 0xff424242u, 0xff101010u, 0xff4b4b4bu, 0xfff5f4f0u, 0xffc5c5c5u, 0xffe8e6dfu, 0xffdeded9u, 0xfff2efe4u, 0xffffcf86u, 0xfff47770u, 0xff91d1a1u, 0xffe1b9a9u, 0xff474747u, 0xff515151u }, false },
    { "Light Studio", { 0xffe4e5e8u, 0xfff6f7f8u, 0xffeef0f2u, 0xffe8eaedu, 0xffe1e4e8u, 0xffd8dce1u, 0xff939ca7u, 0xffd0d9e4u, 0xffffffffu, 0xffb7c2d1u, 0xff15191eu, 0xff343c46u, 0xff8b282eu, 0xff164b81u, 0xff005bc5u, 0xff805000u, 0xffb21c36u, 0xff176344u, 0xff864f13u, 0xffb8d7edu, 0xffb1d9d3u }, true },
    { "Emerald", { 0xff101a18u, 0xff14221fu, 0xff192b26u, 0xff21332du, 0xff283c35u, 0xff32483eu, 0xff557368u, 0xff395548u, 0xff0e1c17u, 0xff3b584cu, 0xfff0faf4u, 0xffc2dfd1u, 0xffa6e9b9u, 0xffa5d7ffu, 0xff62b7ffu, 0xffffce7au, 0xffff7681u, 0xff8fe4c2u, 0xffc0e9aau, 0xff325c5du, 0xff385c48u }, false },
    { "Midnight Blue", { 0xff101523u, 0xff151c2eu, 0xff1b2539u, 0xff222d43u, 0xff2a354eu, 0xff35425au, 0xff536581u, 0xff3d4d69u, 0xff10192bu, 0xff405573u, 0xfff4f7ffu, 0xffc9d5ebu, 0xffb4d4ffu, 0xffaddcffu, 0xff57b4ffu, 0xffffd084u, 0xffff7583u, 0xff9be1c2u, 0xffd2dcffu, 0xff365a80u, 0xff335d68u }, false },
    { "Cinematic Atelier", { 0xff191411u, 0xff211a16u, 0xff29221du, 0xff332a24u, 0xff3d322bu, 0xff473b31u, 0xff75604eu, 0xff514237u, 0xff1a1512u, 0xff54463bu, 0xfffff5e8u, 0xffe0cdbbu, 0xfff0d5a2u, 0xffadd4ffu, 0xff62afffu, 0xffffd080u, 0xffff7782u, 0xffa5dec1u, 0xfff4d49bu, 0xff535675u, 0xff425e58u }, false },
    { "Amethyst", { 0xff18131fu, 0xff20192bu, 0xff292135u, 0xff31283fu, 0xff3b3049u, 0xff463b57u, 0xff716385u, 0xff524466u, 0xff191322u, 0xff59486cu, 0xfff9f3ffu, 0xffe1d4eeu, 0xffe2c4ffu, 0xffaed7ffu, 0xff69b4ffu, 0xffffcf82u, 0xffff7587u, 0xff9be0c2u, 0xffebd1ffu, 0xff4f5280u, 0xff3d5b68u }, false }
}};
// Preserve the persisted Graphite Red index and all other palettes.
inline constexpr int hardwareConsoleIndex = 1;
inline constexpr std::uint32_t hardwareScale = 0xffc65b45u;
inline constexpr std::uint32_t hardwareCap = 0xff858585u;
inline constexpr std::uint32_t hardwareIvory = 0xffe8e6dfu;
inline constexpr std::uint32_t hardwareInk = 0xff191919u;
inline constexpr std::uint32_t hardwareRecord = 0xffb9232du;
inline constexpr bool hardwareConsole(int index) { return index == hardwareConsoleIndex; }
inline constexpr int validIndex(int value) { return value >= 0 && value < (int) palettes.size() ? value : 0; }
}
