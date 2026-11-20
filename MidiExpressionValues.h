// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>

namespace studio
{
enum class ExpressionValue { sevenBit, switch64, pitch14 };

constexpr int expressionMaximum (ExpressionValue range)
{
    return range == ExpressionValue::pitch14 ? 16383 : 127;
}

constexpr int decodeExpressionValue (ExpressionValue range, int stored)
{
    const int value = range == ExpressionValue::pitch14 ? stored : stored >> 7;
    return std::clamp (value, 0, expressionMaximum (range));
}

constexpr int encodeExpressionValue (ExpressionValue range, int value)
{
    value = std::clamp (value, 0, expressionMaximum (range));
    if (range == ExpressionValue::switch64) value = value < 64 ? 0 : 127;
    return range == ExpressionValue::pitch14 ? value : value << 7;
}

constexpr bool matchingClipChannel (int source, int destination)
{
    return source >= 1 && source <= 16 && source == destination;
}
}
