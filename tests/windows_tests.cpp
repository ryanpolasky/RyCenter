#define wWinMain rycenterApplicationMain
#include "../src/main.cpp"
#undef wWinMain

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <stdexcept>
#include <objidl.h>
#include <gdiplus.h>

namespace {
constexpr wchar_t fixtureClass[] = L"RyCenter.TestFixture";
constexpr UINT createFixture = WM_APP + 50;
HWND fixtureRoot;

void require(bool condition, const char *text) {
    if (!condition)
        throw std::runtime_error(text);
    std::printf("PASS: %s\n", text);
}

void pump(DWORD duration) {
    const auto deadline = GetTickCount64() + duration;
    do {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        const auto now = GetTickCount64();
        if (now >= deadline)
            return;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(deadline - now),
                                  QS_ALLINPUT);
    } while (true);
}

bool until(const std::function<bool()> &condition) {
    const auto deadline = GetTickCount64() + 3000;
    while (!condition()) {
        if (GetTickCount64() >= deadline)
            return false;
        pump(10);
    }
    return true;
}

LRESULT CALLBACK fixtureProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == createFixture) {
        const DWORD extended = wparam == 1 ? WS_EX_TOOLWINDOW : 0;
        const auto owner = wparam == 2 ? hwnd : nullptr;
        const auto created =
            CreateWindowExW(extended, fixtureClass, L"RyCenter test window", WS_OVERLAPPEDWINDOW,
                            30, 40, 320, 240, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
        ShowWindow(created, SW_SHOWNOACTIVATE);
        return reinterpret_cast<LRESULT>(created);
    }
    if (message == WM_DESTROY && hwnd == fixtureRoot) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

int runFixture() {
    WNDCLASSW cls{};
    cls.lpfnWndProc = fixtureProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = fixtureClass;
    RegisterClassW(&cls);
    fixtureRoot =
        CreateWindowExW(0, fixtureClass, L"RyCenter existing test window", WS_OVERLAPPEDWINDOW, 30,
                        40, 320, 240, nullptr, nullptr, cls.hInstance, nullptr);
    if (!fixtureRoot)
        return 1;
    ShowWindow(fixtureRoot, SW_SHOWNOACTIVATE);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
        DispatchMessageW(&message);
    return 0;
}

struct Fixture {
    PROCESS_INFORMATION process{};
    HWND root = nullptr;

    ~Fixture() {
        if (root)
            PostMessageW(root, WM_CLOSE, 0, 0);
        if (process.hProcess) {
            if (WaitForSingleObject(process.hProcess, 3000) != WAIT_OBJECT_0) {
                TerminateProcess(process.hProcess, 1);
            }
            CloseHandle(process.hProcess);
            CloseHandle(process.hThread);
        }
    }

    void start() {
        auto command = startupCommandLine() + L" --fixture";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                               nullptr, &startup, &process) != FALSE,
                "launch external-process test windows");
        require(until([&] {
                    struct Search {
                        DWORD process;
                        HWND hwnd;
                    } search{process.dwProcessId, nullptr};
                    EnumWindows(
                        [](HWND hwnd, LPARAM data) -> BOOL {
                            auto &result = *reinterpret_cast<Search *>(data);
                            DWORD pid = 0;
                            GetWindowThreadProcessId(hwnd, &pid);
                            if (pid == result.process && IsWindowVisible(hwnd)) {
                                result.hwnd = hwnd;
                                return FALSE;
                            }
                            return TRUE;
                        },
                        reinterpret_cast<LPARAM>(&search));
                    root = search.hwnd;
                    return root != nullptr;
                }),
                "external window is ready");
    }

    HWND create(WPARAM kind = 0) const {
        DWORD_PTR result = 0;
        require(SendMessageTimeoutW(root, createFixture, kind, 0, SMTO_ABORTIFHUNG, 3000,
                                    &result) != 0 &&
                    result != 0,
                "create test window");
        return reinterpret_cast<HWND>(result);
    }
};

RECT bounds(HWND hwnd) {
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    return rect;
}

bool isCentered(HWND hwnd) {
    DwmFlush();
    const auto outer = bounds(hwnd);
    RECT visible = outer;
    if (FAILED(
            DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &visible, sizeof(visible)))) {
        visible = outer;
    }
    MONITORINFO monitor{sizeof(MONITORINFO), {}, {}, 0};
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
    const auto position =
        rycenter::centeredPosition(rectangle(outer), rectangle(visible), rectangle(monitor.rcWork));
    return std::abs(outer.left - position.x) <= 1 && std::abs(outer.top - position.y) <= 1;
}

BOOL CALLBACK checkMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    const auto hwnd = reinterpret_cast<HWND>(data);
    MONITORINFO info{sizeof(MONITORINFO), {}, {}, 0};
    GetMonitorInfoW(monitor, &info);
    SetWindowPos(hwnd, nullptr, info.rcWork.left + 20, info.rcWork.top + 30, 320, 240,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    DwmFlush();
    const auto before = bounds(hwnd);
    const auto foreground = GetForegroundWindow();
    require(centerWindow(hwnd, false) == CenterResult::Centered, "accept center request");
    require(until([&] { return isCentered(hwnd); }),
            "center the visible frame on the monitor work area");
    const auto after = bounds(hwnd);
    require(after.right - after.left == before.right - before.left &&
                after.bottom - after.top == before.bottom - before.top,
            "preserve window size");
    require(GetForegroundWindow() == foreground, "preserve foreground focus");
    return TRUE;
}

void runChecks(Fixture &fixture) {
    require(validHotkey(MAKEWORD('C', HOTKEYF_CONTROL | HOTKEYF_ALT)) &&
                !validHotkey(MAKEWORD('C', 0)) && !validHotkey(MAKEWORD('C', HOTKEYF_SHIFT)) &&
                !validHotkey(0x10000),
            "validate shortcuts");
    require(registerShortcut(MAKEWORD(VK_F24, HOTKEYF_CONTROL | HOTKEYF_ALT), 99),
            "register a system hotkey without a keyboard hook");
    UnregisterHotKey(messageWindow, 99);
    EnumDisplayMonitors(nullptr, nullptr, checkMonitor, reinterpret_cast<LPARAM>(fixture.root));

    ShowWindow(fixture.root, SW_MAXIMIZE);
    require(until([&] { return IsZoomed(fixture.root) != FALSE; }), "maximize test window");
    require(centerWindow(fixture.root, false) == CenterResult::Skipped, "skip maximized window");
    ShowWindow(fixture.root, SW_MINIMIZE);
    require(until([&] { return IsIconic(fixture.root) != FALSE; }), "minimize test window");
    require(centerWindow(fixture.root, false) == CenterResult::Skipped, "skip minimized window");
    ShowWindow(fixture.root, SW_RESTORE);
    SetWindowPos(fixture.root, nullptr, 30, 40, 320, 240, SWP_NOACTIVATE | SWP_NOZORDER);

    require(!openHook && !dragHook && pending.empty(),
            "default mode has no hooks or settling timers");
    require(enableAutomatic(), "enable optional automatic mode");
    pump(400);
    require(bounds(fixture.root).left == 30 && bounds(fixture.root).top == 40,
            "do not rearrange existing windows when enabling auto mode");
    const auto automatic = fixture.create();
    require(until([&] { return isCentered(automatic); }),
            "automatically center a newly opened window");
    require(until([] { return pending.empty(); }), "remove the one-shot timer after centering");

    const auto moved = fixture.create();
    require(until([&] {
                return std::any_of(pending.begin(), pending.end(), [moved](const auto &item) {
                    return item.hwnd == moved && item.timer;
                });
            }),
            "new window receives a settling timer");
    SetWindowPos(moved, nullptr, 70, 80, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
    pump(400);
    require(bounds(moved).left == 70 && bounds(moved).top == 80,
            "leave a window alone if it moved during the settling delay");
    ShowWindow(moved, SW_HIDE);
    ShowWindow(moved, SW_SHOWNOACTIVATE);
    pump(400);
    require(bounds(moved).left == 70 && bounds(moved).top == 80,
            "do not recenter an existing window when it is shown again");

    const auto tool = fixture.create(1);
    const auto owned = fixture.create(2);
    pump(400);
    require(!eligible(tool, true) && bounds(tool).left == 30, "skip tool windows");
    require(!eligible(owned, true) && bounds(owned).left == 30,
            "skip owned dialogs in automatic mode");
    const auto temporary = fixture.create();
    PostMessageW(temporary, WM_CLOSE, 0, 0);
    require(until([&] { return !IsWindow(temporary); }), "destroy a window before its timer fires");
    pump(400);
    require(pending.empty(), "remove destroyed candidates and their timers");
    disableAutomatic();
    require(!openHook && !dragHook && pending.empty(),
            "unregister hooks and timers when automatic mode is off");
    const auto disabled = fixture.create();
    pump(400);
    require(bounds(disabled).left == 30 && bounds(disabled).top == 40,
            "new windows stay where they open in shortcut-only mode");
}

enum class SettingsCheck {
    Visual,
    Cancel,
    Save,
    ResetCancel,
    ResetSave,
    Conflict,
    Escape,
    FocusLoss
};
std::string settingsFailure;

void captureSettings(HWND dialog, const wchar_t *path) {
    RECT rect;
    GetClientRect(dialog, &rect);
    const auto screen = GetDC(dialog);
    const auto dc = CreateCompatibleDC(screen);
    const auto bitmap = CreateCompatibleBitmap(screen, rect.right, rect.bottom);
    const auto old = SelectObject(dc, bitmap);
    SendMessageW(dialog, WM_PRINT, reinterpret_cast<WPARAM>(dc),
                 PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
    bool saved = false;
    {
        Gdiplus::Bitmap image(bitmap, nullptr);
        const CLSID png = {0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0, 0, 0xf8, 0x1e, 0xf3, 0x2e}};
        saved = image.Save(path, &png, nullptr) == Gdiplus::Ok;
    }
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(dialog, screen);
    require(saved, "capture the native Windows settings render");
}

void checkLayout(HWND dialog, UINT dpi) {
    rycenter::ui::layout(dialog, dpi);
    RECT client;
    GetClientRect(dialog, &client);
    for (HWND control = GetWindow(dialog, GW_CHILD); control;
         control = GetWindow(control, GW_HWNDNEXT)) {
        RECT rect;
        GetWindowRect(control, &rect);
        MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT *>(&rect), 2);
        require(rect.left >= 0 && rect.top >= 0 && rect.right <= client.right &&
                    rect.bottom <= client.bottom && rect.right > rect.left &&
                    rect.bottom > rect.top,
                "DPI layout keeps every native control inside the window");
    }
    RECT window;
    GetWindowRect(dialog, &window);
    MONITORINFO monitor{sizeof(monitor), {}, {}, 0};
    GetMonitorInfoW(MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST), &monitor);
    require(window.top >= monitor.rcWork.top && window.bottom <= monitor.rcWork.bottom &&
                window.left >= monitor.rcWork.left && window.right <= monitor.rcWork.right,
            "scaled settings fit the available monitor work area");
}

void typeShortcut(HWND dialog, UINT key, BYTE modifiers, bool release = true) {
    const auto control = GetDlgItem(dialog, IDC_HOTKEY);
    SetFocus(control);
    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDC_HOTKEY, BN_CLICKED),
                 reinterpret_cast<LPARAM>(control));
    require(shortcutDraft.recording && !hotkeyRegistered,
            "focus starts recording and suspends the global shortcut");
    BYTE previous[256]{};
    GetKeyboardState(previous);
    BYTE state[256]{};
    state[VK_CONTROL] = modifiers & HOTKEYF_CONTROL ? 0x80 : 0;
    state[VK_MENU] = modifiers & HOTKEYF_ALT ? 0x80 : 0;
    state[VK_SHIFT] = modifiers & HOTKEYF_SHIFT ? 0x80 : 0;
    SetKeyboardState(state);
    SendMessageW(control, WM_KEYDOWN, key, 1);
    if (release)
        SendMessageW(control, WM_KEYUP, key, 1);
    SetKeyboardState(previous);
}

INT_PTR CALLBACK settingsTestProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_INITDIALOG) {
        settingsProc(dialog, message, wparam, lparam);
        SetWindowLongPtrW(dialog, DWLP_USER, lparam);
        SetTimer(dialog, 1, 10, nullptr);
        return FALSE;
    }
    if (message != WM_TIMER)
        return settingsProc(dialog, message, wparam, lparam);
    KillTimer(dialog, 1);
    ShowWindow(dialog, SW_SHOWNA);
    try {
        require(!shortcutDraft.recording && hotkeyRegistered,
                "opening settings does not start recording or disable the shortcut");
        require(shortcutDraft.value == options.hotkey, "reopen shows the saved shortcut");
        const auto check = static_cast<SettingsCheck>(GetWindowLongPtrW(dialog, DWLP_USER));
        const auto before = options.hotkey;
        if (check == SettingsCheck::Visual) {
            checkLayout(dialog, 96);
            RECT placed;
            GetWindowRect(dialog, &placed);
            SetWindowPos(dialog, nullptr, placed.left - 40, placed.top - 30, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            require(centerWindow(dialog, false) == CenterResult::Centered,
                    "the shortcut centers RyCenter's own Settings window");
            pump(150);
            RECT centered;
            GetWindowRect(dialog, &centered);
            require(std::abs(centered.left - placed.left) <= 2 &&
                        std::abs(centered.top - placed.top) <= 2,
                    "Settings returns to the middle of its monitor");
            captureSettings(dialog, L"settings-studio.png");
            SendDlgItemMessageW(dialog, IDC_AUTO, BM_CLICK, 0, 0);
            SendDlgItemMessageW(dialog, IDC_STARTUP, BM_CLICK, 0, 0);
            require(IsDlgButtonChecked(dialog, IDC_AUTO) == BST_CHECKED &&
                        IsDlgButtonChecked(dialog, IDC_STARTUP) == BST_CHECKED,
                    "native checkboxes retain real toggle behavior behind custom drawing");
            captureSettings(dialog, L"settings-studio-enabled.png");
            SetFocus(GetDlgItem(dialog, IDC_HOTKEY));
            captureSettings(dialog, L"settings-studio-recording.png");
            MSG tab{};
            tab.hwnd = GetDlgItem(dialog, IDC_HOTKEY);
            tab.message = WM_KEYDOWN;
            tab.wParam = VK_TAB;
            require(IsDialogMessageW(dialog, &tab) && !shortcutDraft.recording && hotkeyRegistered,
                    "Tab exits shortcut recording through normal dialog navigation");
            checkLayout(dialog, 144);
            captureSettings(dialog, L"settings-studio-150.png");
            checkLayout(dialog, 192);
            captureSettings(dialog, L"settings-studio-200-fit.png");
            rycenter::ui::setStatus(dialog, L"The shortcut is in use. Try another.", true);
            captureSettings(dialog, L"settings-studio-error.png");
            SendMessageW(dialog, WM_COMMAND, IDC_CLOSE, 0);
        } else if (check == SettingsCheck::ResetCancel || check == SettingsCheck::ResetSave) {
            const bool startup = IsDlgButtonChecked(dialog, IDC_STARTUP) == BST_CHECKED;
            SendMessageW(dialog, WM_COMMAND, IDC_RESET, 0);
            require(shortcutDraft.value == rycenter::defaultShortcut && !shortcutDraft.recording,
                    "Reset restores Ctrl+Alt+C without saving yet");
            require((IsDlgButtonChecked(dialog, IDC_STARTUP) == BST_CHECKED) == startup &&
                        options.hotkey == before,
                    "Reset preserves unrelated settings and the live shortcut");
            SendMessageW(dialog, WM_COMMAND, check == SettingsCheck::ResetSave ? IDOK : IDCANCEL,
                         0);
        } else if (check == SettingsCheck::Conflict) {
            require(registerShortcut(MAKEWORD(VK_F22, HOTKEYF_CONTROL | HOTKEYF_ALT), 98),
                    "reserve a competing shortcut");
            typeShortcut(dialog, VK_F22, HOTKEYF_CONTROL | HOTKEYF_ALT);
            SendMessageW(dialog, WM_COMMAND, IDOK, 0);
            require(IsWindowVisible(dialog), "conflict leaves settings open");
            require(options.hotkey == before && hotkeyRegistered,
                    "conflict preserves the working shortcut");
            wchar_t status[128]{};
            GetDlgItemTextW(dialog, IDC_STATUS, status, static_cast<int>(std::size(status)));
            require(std::wstring(status).find(L"in use") != std::wstring::npos,
                    "conflict explains the problem inline");
            UnregisterHotKey(messageWindow, 98);
            SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
        } else if (check == SettingsCheck::Escape) {
            typeShortcut(dialog, VK_ESCAPE, 0);
            require(!shortcutDraft.recording && shortcutDraft.value == before && hotkeyRegistered &&
                        IsWindowVisible(dialog),
                    "Escape cancels recording without closing settings");
            SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
        } else if (check == SettingsCheck::FocusLoss) {
            typeShortcut(dialog, VK_F23, HOTKEYF_CONTROL | HOTKEYF_SHIFT, false);
            SetFocus(GetDlgItem(dialog, IDCANCEL));
            require(!shortcutDraft.recording && shortcutDraft.value == before && hotkeyRegistered,
                    "leaving an unfinished recording restores the previous shortcut");
            SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
        } else {
            typeShortcut(dialog, 'X', 0);
            require(shortcutDraft.recording && shortcutDraft.value == before,
                    "typing without a modifier does not overwrite the shortcut");
            finishRecording(dialog, false);
            typeShortcut(dialog, LOBYTE(before), HIBYTE(before));
            require(!shortcutDraft.recording && shortcutDraft.value == before && hotkeyRegistered,
                    "the already-registered shortcut can be recorded again");
            typeShortcut(dialog, VK_F23, HOTKEYF_CONTROL | HOTKEYF_SHIFT);
            require(shortcutDraft.value == MAKEWORD(VK_F23, HOTKEYF_CONTROL | HOTKEYF_SHIFT) &&
                        !shortcutDraft.recording && hotkeyRegistered,
                    "key release commits the draft and restores the live shortcut");
            SendMessageW(dialog, WM_COMMAND, check == SettingsCheck::Save ? IDOK : IDCANCEL, 0);
        }
    } catch (const std::exception &error) {
        settingsFailure = error.what();
        EndDialog(dialog, IDCANCEL);
    }
    return TRUE;
}

void runSettingsChecks() {
    const auto sandboxPath = L"Software\\RyCenter.Tests." + std::to_wstring(GetCurrentProcessId());
    HKEY sandbox;
    require(RegCreateKeyExW(HKEY_CURRENT_USER, sandboxPath.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS,
                            nullptr, &sandbox, nullptr) == ERROR_SUCCESS,
            "create isolated test settings");
    require(RegOverridePredefKey(HKEY_CURRENT_USER, sandbox) == ERROR_SUCCESS,
            "redirect preferences and startup writes away from real user settings");
    try {
        options = {};
        hotkeyRegistered = registerShortcut(options.hotkey, hotkeyId);
        require(hotkeyRegistered && saveOptions(options), "start with the default shortcut");
        const auto run = [](SettingsCheck check) {
            settingsFailure.clear();
            const auto result =
                DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), messageWindow,
                                settingsTestProc, static_cast<LPARAM>(check));
            require(result != -1, "open the real settings dialog");
            if (!settingsFailure.empty())
                throw std::runtime_error(settingsFailure);
            require(loadOptions(), "reload persisted settings after dialog closes");
        };
        run(SettingsCheck::Visual);
        const auto gdiBefore = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        run(SettingsCheck::Cancel);
        require(options.hotkey == rycenter::defaultShortcut, "Cancel does not persist a draft");
        run(SettingsCheck::Save);
        const auto custom = MAKEWORD(VK_F23, HOTKEYF_CONTROL | HOTKEYF_SHIFT);
        require(options.hotkey == custom && hotkeyRegistered,
                "Save persists and registers the new shortcut");
        require(!registerShortcut(custom, 97), "the new shortcut is owned by RyCenter");
        require(registerShortcut(rycenter::defaultShortcut, 97),
                "Save releases the previous shortcut");
        UnregisterHotKey(messageWindow, 97);
        run(SettingsCheck::Conflict);
        run(SettingsCheck::Escape);
        run(SettingsCheck::FocusLoss);
        run(SettingsCheck::ResetCancel);
        require(options.hotkey == custom, "Cancel after Reset keeps the saved custom shortcut");
        run(SettingsCheck::ResetSave);
        require(options.hotkey == rycenter::defaultShortcut && hotkeyRegistered,
                "Reset then Save persists and registers Ctrl+Alt+C");
        const auto gdiAfter = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        std::printf("GDI objects after first open: %lu, after 7 more: %lu\n", gdiBefore, gdiAfter);
        require(gdiAfter <= gdiBefore + 2, "reopening settings does not accumulate GDI objects");
        UnregisterHotKey(messageWindow, hotkeyId);
        hotkeyRegistered = false;
    } catch (...) {
        RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        RegCloseKey(sandbox);
        RegDeleteTreeW(HKEY_CURRENT_USER, sandboxPath.c_str());
        throw;
    }
    RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
    RegCloseKey(sandbox);
    RegDeleteTreeW(HKEY_CURRENT_USER, sandboxPath.c_str());
}
}

int main(int argc, char **) {
    if (argc > 1)
        return runFixture();
    instance = GetModuleHandleW(nullptr);
    WNDCLASSW cls{};
    cls.lpfnWndProc = windowProc;
    cls.hInstance = instance;
    cls.lpszClassName = windowClass;
    RegisterClassW(&cls);
    messageWindow = CreateWindowExW(0, windowClass, L"RyCenter integration tests", 0, 0, 0, 0, 0,
                                    nullptr, nullptr, instance, nullptr);
    Fixture fixture;
    int result = 0;
    try {
        require(messageWindow != nullptr, "create hidden event receiver");
        fixture.start();
        runChecks(fixture);
        runSettingsChecks();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        result = 1;
    }
    disableAutomatic();
    DestroyWindow(messageWindow);
    return result;
}
