#pragma once

#include <algorithm>
#include <cstdint>

namespace rycenter {
struct Rect {
    std::int32_t left, top, right, bottom;
};

struct Point {
    std::int32_t x, y;
};

constexpr Point centeredPosition(Rect outer, Rect visible, Rect work) {
    const auto x =
        work.left + std::max(0, (work.right - work.left) - (visible.right - visible.left)) / 2;
    const auto y =
        work.top + std::max(0, (work.bottom - work.top) - (visible.bottom - visible.top)) / 2;
    return {x - (visible.left - outer.left), y - (visible.top - outer.top)};
}

constexpr bool coversMonitor(Rect visible, Rect monitor) {
    return visible.left <= monitor.left && visible.top <= monitor.top &&
           visible.right >= monitor.right && visible.bottom >= monitor.bottom;
}
}
