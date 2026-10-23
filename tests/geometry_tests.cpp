#include "geometry.h"

#include <cstdio>
#include <cstdlib>

using rycenter::Rect;

void check(const char *name, Rect outer, Rect visible, Rect work, int x, int y) {
    const auto position = rycenter::centeredPosition(outer, visible, work);
    if (position.x != x || position.y != y) {
        std::fprintf(stderr, "%s: expected (%d, %d), got (%d, %d)\n", name, x, y, position.x,
                     position.y);
        std::exit(1);
    }
}

int main() {
    check("primary/taskbar", {100, 100, 900, 700}, {100, 100, 900, 700}, {0, 0, 1920, 1040}, 560,
          220);
    check("invisible borders", {92, 100, 908, 708}, {100, 100, 900, 700}, {0, 0, 1920, 1040}, 552,
          220);
    check("left monitor", {-1800, 100, -1000, 700}, {-1800, 100, -1000, 700}, {-1920, 0, 0, 1040},
          -1360, 220);
    check("monitor above", {100, -1000, 900, -400}, {100, -1000, 900, -400}, {0, -1080, 1920, 0},
          560, -840);
    check("left taskbar", {100, 100, 900, 700}, {100, 100, 900, 700}, {80, 0, 1920, 1080}, 600,
          240);
    check("top taskbar", {100, 100, 900, 700}, {100, 100, 900, 700}, {0, 48, 1920, 1080}, 560, 264);
    check("150 percent physical pixels", {0, 0, 1216, 912}, {8, 0, 1208, 900},
          {1920, 0, 5760, 2100}, 3232, 600);
    check("portrait monitor", {0, 0, 800, 600}, {0, 0, 800, 600}, {1920, -400, 3000, 1480}, 2060,
          240);
    check("odd dimensions", {0, 0, 801, 601}, {0, 0, 801, 601}, {0, 0, 1920, 1040}, 559, 219);
    check("oversized width", {0, 0, 2200, 600}, {0, 0, 2200, 600}, {0, 0, 1920, 1040}, 0, 220);
    check("oversized height", {0, 0, 800, 1400}, {0, 0, 800, 1400}, {0, 48, 1920, 1080}, 560, 48);
    check("already centered", {552, 220, 1368, 828}, {560, 220, 1360, 820}, {0, 0, 1920, 1040}, 552,
          220);
    if (!rycenter::coversMonitor({0, 0, 1920, 1080}, {0, 0, 1920, 1080}) ||
        !rycenter::coversMonitor({-8, -8, 1928, 1088}, {0, 0, 1920, 1080}) ||
        rycenter::coversMonitor({0, 0, 1920, 1040}, {0, 0, 1920, 1080}) ||
        rycenter::coversMonitor({100, 100, 900, 700}, {0, 0, 1920, 1080})) {
        std::fputs("Fullscreen classification failed\n", stderr);
        return 1;
    }
    std::puts("12 centering cases and 4 fullscreen cases passed");
}
