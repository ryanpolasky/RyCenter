#pragma once

#include <windows.h>

namespace rycenter::ui {
bool initialize(HWND dialog, HINSTANCE instance);
void destroy(HWND dialog);
bool handleMessage(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam, INT_PTR &result);
void setRecording(HWND dialog, bool recording);
void setStatus(HWND dialog, const wchar_t *text, bool error);
void layout(HWND dialog, UINT dpi, const RECT *suggested = nullptr);
}
