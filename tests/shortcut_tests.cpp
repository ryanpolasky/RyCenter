#include "shortcut.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>

void require(bool condition, const char *description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        std::exit(1);
    }
    std::printf("PASS: %s\n", description);
}

int main() {
    using namespace rycenter;
    require(validShortcut(defaultShortcut), "default is Ctrl+Alt+C");
    require(sameShortcut(defaultShortcut, defaultShortcut | (Extended << 8)) &&
                !sameShortcut(defaultShortcut, defaultShortcut | (Shift << 8)),
            "extended-key metadata does not create a conflicting duplicate registration");
    require(!validShortcut('C') && !validShortcut('C' | (Shift << 8)),
            "reject unmodified and shift-only typing");
    require(!validShortcut(0x10000) && !validShortcut('C' | 0x8000),
            "reject malformed persisted shortcuts");
    for (const auto key : {0x10, 0x11, 0x12, 0x5b, 0x5c, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5})
        require(!validShortcut(key | (Control << 8)), "ignore modifier-only presses");
    const auto custom = static_cast<std::uint16_t>('K' | ((Control | Shift) << 8));
    ShortcutDraft draft;
    require(!draft.capture('K', Control | Shift), "capture only while recording");
    draft.begin();
    require(!draft.capture('K', 0) && draft.value == defaultShortcut && draft.recording,
            "invalid input leaves the last valid shortcut intact");
    require(draft.capture('K', Control | Shift) && draft.value == custom,
            "capture a custom shortcut");
    draft.cancel();
    require(draft.value == defaultShortcut && !draft.recording,
            "escape or focus loss restores the pre-recording draft");
    draft.begin();
    draft.capture('K', Control | Shift);
    draft.recording = false;
    draft.begin();
    draft.cancel();
    require(draft.value == custom, "cancel a second recording preserves the first choice");
    draft.begin();
    require(draft.capture(0x25, Control | Alt | Extended), "record an extended key");
    draft.reset();
    require(draft.value == defaultShortcut && !draft.recording,
            "reset stops recording and restores Ctrl+Alt+C");
    draft.begin();
    require(draft.capture('K', Windows | Shift), "record Windows-modified shortcuts");
}
