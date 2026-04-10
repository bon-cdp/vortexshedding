// Test harness for the DOCX writer.
//
// Compiles a tiny standalone program that:
//   1. Initializes GDI+ (PNG encoder)
//   2. Builds a fake AppState
//   3. Calls save_docx_report with all sections enabled
//   4. Writes test_output.docx
//
// Then verify with: unzip -l test_output.docx

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <objbase.h>
#include <gdiplus.h>
#include <iostream>

// Minimal stand-ins for the GUI globals that save_docx_report touches.
// We avoid pulling in the full torsor_win.cpp by re-implementing just enough
// of the public surface and #include-ing the writer code via copy/paste.
//
// Easier path: just include torsor_win.cpp and override main. But torsor_win.cpp
// has WinMain. So we'd #define WinMain to something else. Let's do that.

#define WinMain Disabled_WinMain
#define WndProc Disabled_WndProc
#include "torsor_win.cpp"
#undef WinMain
#undef WndProc

int main() {
    // Manually init GDI+ (since we're skipping WinMain)
    Gdiplus::GdiplusStartupInput in;
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartup(&token, &in, nullptr);
    OleInitialize(nullptr);

    // Init fonts (some draw_* functions reference them)
    g_hFont      = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_hFontBold  = CreateFontW(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_hFontSmall = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_hFontTitle = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    // Run the physics so the report has data
    g_state.update();

    PrintOptions opt;  // all true by default
    opt.format = 0;    // .docx

    bool ok = save_docx_report("test_output.docx", opt);

    Gdiplus::GdiplusShutdown(token);
    OleUninitialize();

    std::cout << (ok ? "DOCX_WRITE_OK" : "DOCX_WRITE_FAIL") << std::endl;
    return ok ? 0 : 1;
}
