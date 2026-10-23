#pragma once

#include <cstdint>

namespace rycenter {
enum ShortcutModifier : std::uint8_t {
    Shift = 0x01,
    Control = 0x02,
    Alt = 0x04,
    Extended = 0x08,
    Windows = 0x10
};

constexpr std::uint16_t defaultShortcut = 'C' | ((Control | Alt) << 8);

constexpr bool sameShortcut(std::uint32_t first, std::uint32_t second) {
    return (first & ~(Extended << 8)) == (second & ~(Extended << 8));
}

constexpr bool validShortcut(std::uint32_t shortcut) {
    const auto key = shortcut & 0xff;
    const auto flags = shortcut >> 8;
    return shortcut <= 0xffff && key >= 8 && key < 0xff && key != 0x10 && key != 0x11 &&
           key != 0x12 && key != 0x5b && key != 0x5c && !(key >= 0xa0 && key <= 0xa5) &&
           (flags & (Control | Alt | Windows)) != 0 &&
           (flags & ~(Control | Alt | Shift | Extended | Windows)) == 0;
}

struct ShortcutDraft {
    std::uint16_t value = defaultShortcut;
    std::uint16_t previous = defaultShortcut;
    bool recording = false;

    void begin() {
        previous = value;
        recording = true;
    }

    bool capture(std::uint8_t key, std::uint8_t modifiers) {
        const auto candidate = static_cast<std::uint16_t>(key | (modifiers << 8));
        if (!recording || !validShortcut(candidate))
            return false;
        value = candidate;
        return true;
    }

    void cancel() {
        if (recording)
            value = previous;
        recording = false;
    }

    void reset() {
        value = defaultShortcut;
        recording = false;
    }
};
}
