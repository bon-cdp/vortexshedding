// Torsor v0.2 — Vortex Shedding Calculator (Windows native port)
//
// Native Win32 GUI version. Physics is unchanged from the mac/linux build —
// this file replaces the FTXUI front-end and libharu PDF report with a
// dependency-free Win32 + GDI UI and a plain-text engineering report.
//
// Build (MinGW64 from MSYS2):
//   g++ -std=c++20 -O2 -mwindows torsor_win.cpp -o torsor.exe ^
//       -lgdi32 -lcomctl32 -lcomdlg32 ^
//       -static -static-libgcc -static-libstdc++
//
// The resulting torsor.exe is a single self-contained executable.

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <objbase.h>
#include <shlwapi.h>
#include <gdiplus.h>

#include "vortex_physics.h"
// NOTE: vortex_report.h (libharu) intentionally NOT included — replaced by
// save_text_report() below.

#include <string>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <ctime>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <functional>

// ============================================================================
// COLOR MAPPING
// ============================================================================

enum class ColorVariable { UDL, MOMENT, STRESS };

// Smooth linear interpolation through 5 anchor colors.
// Returns a continuously-blended COLORREF for any normalized value [0,1].
static COLORREF value_to_colorref(double normalized) {
    struct RGB3 { int r, g, b; };
    static constexpr RGB3 anchors[] = {
        { 40, 110, 230},   // 0.00  Blue
        {  0, 180, 220},   // 0.25  Cyan
        { 40, 180,  60},   // 0.50  Green
        {240, 190,   0},   // 0.75  Yellow
        {220,  40,  40},   // 1.00  Red
    };
    constexpr int N = 4;  // number of segments (anchors - 1)

    if (normalized <= 0.0) return RGB(anchors[0].r, anchors[0].g, anchors[0].b);
    if (normalized >= 1.0) return RGB(anchors[N].r, anchors[N].g, anchors[N].b);

    double scaled = normalized * N;          // 0..4
    int    seg    = (int)scaled;             // segment index 0..3
    double t      = scaled - seg;            // fraction within segment
    if (seg >= N) { seg = N - 1; t = 1.0; }

    const RGB3& a = anchors[seg];
    const RGB3& b = anchors[seg + 1];
    int r = a.r + (int)((b.r - a.r) * t);
    int g = a.g + (int)((b.g - a.g) * t);
    int bl= a.b + (int)((b.b - a.b) * t);
    return RGB(r, g, bl);
}

// Band index for the pre-created pen cache in draw_mode_shapes.
// 16 bands so each pen covers a narrow slice of the gradient.
static int color_band(double normalized) {
    int band = (int)(normalized * 16);
    if (band < 0)  band = 0;
    if (band > 15) band = 15;
    return band;
}

static double get_value_at_position(const ModeResult& mode, double x_norm, ColorVariable var) {
    if (mode.distribution.x_m.empty()) return 0.0;
    size_t idx = static_cast<size_t>(x_norm * (mode.distribution.x_m.size() - 1));
    if (idx >= mode.distribution.x_m.size()) idx = mode.distribution.x_m.size() - 1;
    switch (var) {
        case ColorVariable::UDL:    return mode.distribution.udl_kN_m;
        case ColorVariable::MOMENT: return mode.distribution.moment_kNm[idx];
        case ColorVariable::STRESS: return mode.distribution.stress_MPa[idx];
    }
    return 0.0;
}

static double get_max_value(const std::vector<ModeResult>& modes, ColorVariable var) {
    double m = 0.0;
    for (const auto& mode : modes) {
        switch (var) {
            case ColorVariable::UDL:    m = std::max(m, mode.distribution.udl_kN_m); break;
            case ColorVariable::MOMENT: m = std::max(m, mode.distribution.max_moment_kNm); break;
            case ColorVariable::STRESS: m = std::max(m, mode.distribution.max_stress_MPa); break;
        }
    }
    return m;
}

// ============================================================================
// APPLICATION STATE
// ============================================================================

struct AppState {
    int    material_index = 0;
    double D_mm           = 200.0;
    double t_mm           =   6.0;
    double L_m            =   6.0;
    double damping        =   0.01;
    double axial_force_kN =   0.0;
    double custom_E_GPa   = 200.0;
    double custom_rho     = 7850.0;
    int    location_index = 14;          // "Moderate Wind (Generic)"
    ColorVariable active_color_var = ColorVariable::STRESS;
    bool   show_uncertainty = true;

    // Custom uniform wind distribution parameters (used only when the
    // selected location entry has uniform == true).
    double custom_wind_min = 0.0;
    double custom_wind_max = 25.0;

    HSS_Member member{};
    std::vector<VortexResults>            results;
    std::vector<WindData::VortexWarning>  warnings;

    // Returns the active wind climate, with custom min/max applied if the
    // selected entry is the uniform-distribution placeholder.
    WindData::WindClimate current_climate() const {
        WindData::WindClimate c = WindData::PORT_CLIMATES[location_index];
        if (c.uniform) {
            c.v_min = custom_wind_min;
            c.v_max = custom_wind_max;
            if (c.v_max <= c.v_min) c.v_max = c.v_min + 0.1;
            double range = c.v_max - c.v_min;
            c.mean_speed_ms = (c.v_min + c.v_max) / 2.0;
            c.percentile_50 = c.v_min + 0.50 * range;
            c.percentile_75 = c.v_min + 0.75 * range;
            c.percentile_90 = c.v_min + 0.90 * range;
            c.percentile_95 = c.v_min + 0.95 * range;
            c.percentile_99 = c.v_min + 0.99 * range;
        }
        return c;
    }

    void update() {
        if (static_cast<size_t>(material_index) < MATERIALS.size() - 1) {
            member.material = MATERIALS[material_index];
        } else {
            member.material = {"Custom", custom_E_GPa, custom_rho};
        }
        member.D_mm          = D_mm;
        member.t_mm          = t_mm;
        member.L_m           = L_m;
        member.damping_ratio = damping;
        member.calculate_properties();

        results = VortexPhysics::analyze_member(member, 2, axial_force_kN);

        warnings.clear();
        const auto location = current_climate();
        for (const auto& bc_result : results) {
            if (bc_result.modes.empty()) continue;
            const auto& mode1 = bc_result.modes[0];
            warnings.push_back(WindData::assess_risk(
                mode1.V_critical_ms,
                mode1.distribution.max_stress_MPa,
                member.material.name == "A36 Steel" ? 250.0 : 345.0,
                member.L_m, member.D_mm, location));
        }
    }
};

// ============================================================================
// GLOBALS
// ============================================================================

static AppState g_state;
static HWND g_hwnd = nullptr;

// Input controls
static HWND g_hMaterialCombo = nullptr;
static HWND g_hLocationCombo = nullptr;
static HWND g_hDEdit = nullptr, g_hTEdit = nullptr, g_hLEdit = nullptr;
static HWND g_hDampEdit = nullptr, g_hAxialEdit = nullptr;
static HWND g_hVminEdit = nullptr, g_hVmaxEdit = nullptr;
static HWND g_hCustomEEdit = nullptr, g_hCustomRhoEdit = nullptr;
static HWND g_hUDLRadio = nullptr, g_hMomentRadio = nullptr, g_hStressRadio = nullptr;
static HWND g_hUncertaintyCheck = nullptr;
static HWND g_hSaveButton = nullptr, g_hQuitButton = nullptr;
static HWND g_hInfoText = nullptr;
static HWND g_hStatusText = nullptr;

// Fonts
static HFONT g_hFont = nullptr, g_hFontBold = nullptr;
static HFONT g_hFontSmall = nullptr, g_hFontTitle = nullptr;

// Custom-drawn region rectangles (recomputed on resize / scroll, in client coords)
static RECT g_rectModes   = {};
static RECT g_rectLegend  = {};
static RECT g_rectWind    = {};
static RECT g_rectWarning = {};

// Vertical scroll state
static int g_scroll_y  = 0;   // current scroll offset (pixels)
static int g_content_h = 0;   // total virtual content height (pixels)

// A child control with a "virtual" y position (relative to top of content,
// before scroll). LayoutWindow translates these to client-area coordinates.
struct PCtrl {
    HWND h;
    int  x, vy, w, ch;
};
static std::vector<PCtrl> g_pos_ctrls;

// Layout constants
static constexpr int MARGIN   = 10;
static constexpr int LABEL_W  =  95;
static constexpr int EDIT_W   = 110;
static constexpr int ROW_H    =  26;
static constexpr int INPUTS_H = 200;

// Control IDs
enum {
    ID_MATERIAL = 1001,
    ID_DEDIT    = 1002,
    ID_TEDIT    = 1003,
    ID_LEDIT    = 1004,
    ID_DAMPEDIT = 1005,
    ID_AXIALEDIT= 1006,
    ID_LOCATION = 1007,
    ID_VMINEDIT = 1008,
    ID_VMAXEDIT = 1009,
    ID_CUSTOM_E = 1010,
    ID_CUSTOM_RHO = 1011,
    ID_RADIO_UDL    = 2001,
    ID_RADIO_MOMENT = 2002,
    ID_RADIO_STRESS = 2003,
    ID_CHK_UNCERT   = 2004,
    ID_BTN_SAVE     = 3001,
    ID_BTN_QUIT     = 3002,
};

// ============================================================================
// HELPERS
// ============================================================================

static std::wstring to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::string from_wide(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                        &s[0], n, nullptr, nullptr);
    return s;
}

static std::string fmt(double v, int precision) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << v;
    return oss.str();
}

static double parse_double(HWND hEdit, double fallback) {
    wchar_t buf[64] = {};
    GetWindowTextW(hEdit, buf, 64);
    try {
        std::wstring s = buf;
        if (s.empty()) return fallback;
        return std::stod(s);
    } catch (...) {
        return fallback;
    }
}

static void set_edit_double(HWND hEdit, double v, int precision) {
    SetWindowTextW(hEdit, to_wide(fmt(v, precision)).c_str());
}

// ============================================================================
// REPORT GENERATION
// ============================================================================

// User selections from the Print Report dialog. Each section can be toggled
// independently; the .docx and .txt writers honor these flags.
struct PrintOptions {
    bool include_logo           = true;
    bool include_inputs         = true;
    bool include_calculated     = true;
    bool include_results_table  = true;
    bool include_uncertainty    = true;
    bool include_risk           = true;
    bool include_recommendations= true;
    bool include_mode_plots     = true;
    bool include_wind_plot      = true;
    int  format = 0;  // 0 = .docx, 1 = .txt
};

// Forward declarations of plot drawing functions used by the report writers.
static void draw_mode_shapes      (HDC hdc, RECT r);
static void draw_color_legend     (HDC hdc, RECT r);
static void draw_wind_distribution(HDC hdc, RECT r);

static bool save_text_report(const std::string& filename, const PrintOptions& opt) {
    std::ofstream out(filename);
    if (!out) return false;

    std::time_t now = std::time(nullptr);
    char timestr[64];
    std::strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S",
                  std::localtime(&now));

    const auto location = g_state.current_climate();

    out << "============================================================\n";
    out << "  TORSOR v0.2 - Vortex Shedding Analysis Report\n";
    out << "  Generated: " << timestr << "\n";
    out << "============================================================\n\n";

    if (opt.include_inputs) {
        out << "INPUT PARAMETERS\n";
        out << "----------------\n";
        out << "  Material:    " << g_state.member.material.name
            << "  (E=" << g_state.member.material.E_GPa << " GPa, rho="
            << g_state.member.material.density_kg_m3 << " kg/m^3)\n";
        out << "  Diameter D:  " << fmt(g_state.D_mm, 1) << " mm\n";
        out << "  Thickness t: " << fmt(g_state.t_mm, 2) << " mm\n";
        out << "  Length L:    " << fmt(g_state.L_m,  2) << " m\n";
        out << "  Damping:     " << fmt(g_state.damping, 4) << "\n";
        out << "  Axial Force: " << fmt(g_state.axial_force_kN, 1) << " kN\n";
        out << "  Location:    " << location.location;
        if (location.uniform) {
            out << "  (uniform " << fmt(location.v_min, 1) << " - "
                << fmt(location.v_max, 1) << " m/s, mean "
                << fmt(location.mean_speed_ms, 1) << " m/s)\n\n";
        } else {
            out << "  (mean wind " << fmt(location.mean_speed_ms, 1) << " m/s)\n\n";
        }
    }

    if (opt.include_calculated) {
        out << "CALCULATED SECTION PROPERTIES\n";
        out << "-----------------------------\n";
        out << "  I  = " << fmt(g_state.member.I_m4 * 1e12, 0) << " mm^4\n";
        out << "  A  = " << fmt(g_state.member.A_m2 * 1e6, 1) << " mm^2\n";
        out << "  mu = " << fmt(g_state.member.mu_kg_m, 2) << " kg/m\n";
        out << "  r  = " << fmt(g_state.member.r_gyration_mm, 1) << " mm\n\n";
    }

    if (opt.include_results_table) {
        out << "DETAILED RESULTS - ALL BOUNDARY CONDITIONS\n";
        out << "------------------------------------------\n";
        out << "  BC                          Mode   f(Hz)    V(m/s)   F(kN)    sigma(MPa)\n";
        for (const auto& bc_result : g_state.results) {
            for (const auto& mode : bc_result.modes) {
                char line[256];
                std::snprintf(line, sizeof(line),
                    "  %-26s   %d    %7.2f  %7.2f  %7.3f  %9.1f\n",
                    bc_result.bc_name.c_str(),
                    mode.mode_number,
                    mode.freq_hz,
                    mode.V_critical_ms,
                    mode.F_static_kN * mode.amplification,
                    mode.distribution.max_stress_MPa);
                out << line;
            }
        }
        out << "\n";
    }

    if (opt.include_uncertainty) {
        out << "DAMPING UNCERTAINTY (zeta range 0.005 - 0.020)\n";
        out << "----------------------------------------------\n";
        out << "  BC                          Mode  sigma_min   sigma_nom   sigma_max  (MPa)\n";
        for (size_t i = 0; i < g_state.results.size(); i++) {
            const auto& bc_result = g_state.results[i];
            for (const auto& mode_res : bc_result.modes) {
                auto rng = VortexPhysics::analyze_mode_with_damping_range(
                    g_state.member,
                    BOUNDARY_CONDITIONS[i],
                    mode_res.mode_number,
                    g_state.axial_force_kN);
                char line[256];
                std::snprintf(line, sizeof(line),
                    "  %-26s   %d    %9.1f   %9.1f   %9.1f\n",
                    bc_result.bc_name.c_str(),
                    mode_res.mode_number,
                    rng.result_max.distribution.max_stress_MPa,
                    rng.result_nominal.distribution.max_stress_MPa,
                    rng.result_min.distribution.max_stress_MPa);
                out << line;
            }
        }
        out << "\n";
    }

    if (opt.include_risk) {
        out << "RISK ASSESSMENT\n";
        out << "---------------\n";
        for (size_t i = 0; i < g_state.warnings.size() && i < g_state.results.size(); i++) {
            const auto& w = g_state.warnings[i];
            out << "  " << g_state.results[i].bc_name << "\n";
            out << "    [" << w.level_name << "] " << w.message << "\n";
            out << "    V_critical at " << fmt(w.wind_percentile, 0)
                << "th percentile of local wind, peak stress "
                << fmt(w.stress_ratio * 100.0, 1) << "% of yield\n";
        }
        out << "\n";
    }

    if (opt.include_recommendations) {
        WindData::WarningLevel worst = WindData::SAFE;
        size_t worst_idx = 0;
        for (size_t i = 0; i < g_state.warnings.size(); i++) {
            if (g_state.warnings[i].level > worst) {
                worst = g_state.warnings[i].level;
                worst_idx = i;
            }
        }
        if (!g_state.warnings.empty()) {
            out << "ENGINEERING RECOMMENDATIONS (worst case: "
                << g_state.warnings[worst_idx].level_name << ")\n";
            out << "------------------------------------------\n";
            for (const auto& rec : g_state.warnings[worst_idx].recommendations) {
                out << "  - " << rec << "\n";
            }
            out << "\n";
        }
    }

    out << "============================================================\n";
    out << "  End of report\n";
    out << "============================================================\n";

    return out.good();
}

// ============================================================================
// CUSTOM DRAWING — mode shapes panel
// ============================================================================

static void draw_mode_shapes(HDC hdc, RECT r) {
    HBRUSH bg = CreateSolidBrush(RGB(252, 252, 254));
    FillRect(hdc, &r, bg);
    DeleteObject(bg);
    FrameRect(hdc, &r, (HBRUSH)GetStockObject(GRAY_BRUSH));

    if (g_state.results.empty()) return;

    // Pre-create one pen per color band (avoid creating GDI objects in inner loop)
    constexpr int N_PENS = 16;
    HPEN band_pens[N_PENS];
    for (int i = 0; i < N_PENS; i++) {
        band_pens[i] = CreatePen(PS_SOLID, 2,
            value_to_colorref(((double)i + 0.5) / N_PENS));
    }

    // Global max for color normalization (across all BCs and modes)
    double global_max = 0.0;
    for (const auto& res : g_state.results) {
        global_max = std::max(global_max, get_max_value(res.modes, g_state.active_color_var));
    }

    // 6 BCs in 2 rows × 3 cols
    constexpr int n_cols = 3;
    constexpr int n_rows = 2;
    int cell_w = (r.right - r.left - 4) / n_cols;
    int cell_h = (r.bottom - r.top - 4) / n_rows;

    SetBkMode(hdc, TRANSPARENT);

    for (size_t i = 0; i < g_state.results.size() && i < 6; i++) {
        int col = (int)i % n_cols;
        int row = (int)i / n_cols;
        RECT cell;
        cell.left   = r.left + 2 + col * cell_w;
        cell.top    = r.top  + 2 + row * cell_h;
        cell.right  = cell.left + cell_w - 2;
        cell.bottom = cell.top  + cell_h - 2;

        // Cell border
        FrameRect(hdc, &cell, (HBRUSH)GetStockObject(LTGRAY_BRUSH));

        const auto& result = g_state.results[i];

        // Title
        SelectObject(hdc, g_hFontBold);
        SetTextColor(hdc, RGB(0, 80, 150));
        std::wstring title = to_wide(result.bc_name);
        RECT title_rect = cell;
        title_rect.bottom = title_rect.top + 18;
        DrawTextW(hdc, title.c_str(), -1, &title_rect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Canvas area for mode shapes
        RECT canvas;
        canvas.left   = cell.left + 8;
        canvas.right  = cell.right - 8;
        canvas.top    = cell.top + 22;
        canvas.bottom = canvas.top + 70;
        int canvas_w = canvas.right  - canvas.left;
        int canvas_h = canvas.bottom - canvas.top;
        int baseline = canvas.top + canvas_h / 2;

        // Baseline (gray dashed)
        HPEN baseline_pen = CreatePen(PS_DOT, 1, RGB(170, 170, 170));
        HGDIOBJ old_pen = SelectObject(hdc, baseline_pen);
        MoveToEx(hdc, canvas.left, baseline, nullptr);
        LineTo  (hdc, canvas.right, baseline);
        SelectObject(hdc, old_pen);
        DeleteObject(baseline_pen);

        // Auto-scale across all modes in this BC
        double max_def = 0.0;
        for (const auto& m : result.modes) {
            for (int x = 0; x <= 200; x++) {
                double xn = x / 200.0;
                double y = VortexPhysics::modal_shape(xn, 1.0,
                                                     result.bc_name, m.mode_number);
                max_def = std::max(max_def, std::abs(y));
            }
        }
        if (max_def < 1e-9) max_def = 1.0;
        double scale = (canvas_h * 0.42) / max_def;

        // Draw each mode as a polyline, switching pens by color band
        for (const auto& m : result.modes) {
            int last_x = canvas.left;
            int last_y = baseline;
            int n_seg = canvas_w;
            for (int s = 0; s <= n_seg; s++) {
                double xn = (double)s / n_seg;
                double y  = VortexPhysics::modal_shape(xn, 1.0,
                                                      result.bc_name, m.mode_number);
                int px = canvas.left + s;
                int py = baseline - (int)(y * scale);

                if (s > 0) {
                    double val = get_value_at_position(m, xn, g_state.active_color_var);
                    double norm = global_max > 0 ? val / global_max : 0.0;
                    int band = color_band(norm);
                    SelectObject(hdc, band_pens[band]);
                    MoveToEx(hdc, last_x, last_y, nullptr);
                    LineTo  (hdc, px, py);
                }
                last_x = px;
                last_y = py;
            }
        }

        // Mode info text below the canvas
        SelectObject(hdc, g_hFontSmall);
        SetTextColor(hdc, RGB(40, 40, 40));
        int text_y = canvas.bottom + 4;
        constexpr int line_h = 13;

        for (const auto& m : result.modes) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "M%d  f=%.2f Hz  V=%.2f m/s",
                m.mode_number, m.freq_hz, m.V_critical_ms);
            std::wstring l1 = to_wide(buf);
            TextOutW(hdc, cell.left + 8, text_y, l1.c_str(), (int)l1.size());
            text_y += line_h;

            // Stress line. When uncertainty is enabled, append the
            // [min - max] damping range computed by sweeping zeta from
            // 0.005 to 0.020 (high damping → low stress and vice versa).
            if (g_state.show_uncertainty && i < BOUNDARY_CONDITIONS.size()) {
                auto rng = VortexPhysics::analyze_mode_with_damping_range(
                    g_state.member,
                    BOUNDARY_CONDITIONS[i],
                    m.mode_number,
                    g_state.axial_force_kN);
                double s_lo = rng.result_max.distribution.max_stress_MPa;
                double s_hi = rng.result_min.distribution.max_stress_MPa;
                std::snprintf(buf, sizeof(buf),
                    "  F=%.3f kN  s=%.0f [%.0f-%.0f] MPa",
                    m.F_static_kN * m.amplification,
                    m.distribution.max_stress_MPa,
                    s_lo, s_hi);
            } else {
                std::snprintf(buf, sizeof(buf),
                    "  F=%.3f kN  sigma=%.0f MPa",
                    m.F_static_kN * m.amplification,
                    m.distribution.max_stress_MPa);
            }
            std::wstring l2 = to_wide(buf);
            TextOutW(hdc, cell.left + 8, text_y, l2.c_str(), (int)l2.size());
            text_y += line_h;
        }
    }

    for (int i = 0; i < N_PENS; i++) DeleteObject(band_pens[i]);
}

// ============================================================================
// CUSTOM DRAWING — color legend
// ============================================================================

static void draw_color_legend(HDC hdc, RECT r) {
    HBRUSH bg = CreateSolidBrush(RGB(252, 252, 254));
    FillRect(hdc, &r, bg);
    DeleteObject(bg);
    FrameRect(hdc, &r, (HBRUSH)GetStockObject(GRAY_BRUSH));

    if (g_state.results.empty()) return;

    double max_val = 0.0;
    for (const auto& res : g_state.results) {
        max_val = std::max(max_val, get_max_value(res.modes, g_state.active_color_var));
    }

    const char* var_name = "STRESS";
    const char* units    = "MPa";
    switch (g_state.active_color_var) {
        case ColorVariable::UDL:    var_name = "UDL";    units = "kN/m"; break;
        case ColorVariable::MOMENT: var_name = "MOMENT"; units = "kN.m"; break;
        case ColorVariable::STRESS: var_name = "STRESS"; units = "MPa";  break;
    }

    SetBkMode(hdc, TRANSPARENT);

    // ----- Row 1: header with variable name and global max -----
    SelectObject(hdc, g_hFont);
    SetTextColor(hdc, RGB(40, 40, 40));
    char hdr[256];
    std::snprintf(hdr, sizeof(hdr),
        "Showing: %s (%s)     |     Global MAX = %.2f %s",
        var_name, units, max_val, units);
    std::wstring h = to_wide(hdr);
    RECT hr = r;
    hr.top    += 4;
    hr.bottom  = hr.top + 16;
    DrawTextW(hdc, h.c_str(), -1, &hr, DT_CENTER | DT_SINGLELINE);

    // ----- Row 2: gradient bar with tick marks at color boundaries -----
    int bar_y = r.top + 22;
    int bar_h = 14;
    int bar_x = r.left + 50;
    int bar_w = (r.right - r.left) - 100;

    // Draw the gradient
    int n_steps = 60;
    for (int i = 0; i < n_steps; i++) {
        double norm = (double)i / (n_steps - 1);
        COLORREF c = value_to_colorref(norm);
        RECT seg = {
            bar_x +  i      * bar_w / n_steps, bar_y,
            bar_x + (i + 1) * bar_w / n_steps, bar_y + bar_h
        };
        HBRUSH b = CreateSolidBrush(c);
        FillRect(hdc, &seg, b);
        DeleteObject(b);
    }
    RECT bar_rect = { bar_x, bar_y, bar_x + bar_w, bar_y + bar_h };
    FrameRect(hdc, &bar_rect, (HBRUSH)GetStockObject(BLACK_BRUSH));

    // Tick marks and labels at 0%, 20%, 40%, 60%, 80%, 100% of max
    SelectObject(hdc, g_hFontSmall);
    HPEN tick_pen = CreatePen(PS_SOLID, 1, RGB(60, 60, 60));
    HGDIOBJ old_pen = SelectObject(hdc, tick_pen);

    constexpr int N_TICKS = 16;
    for (int ti = 0; ti <= N_TICKS; ti++) {
        double tn = (double)ti / N_TICKS;
        int tx = bar_x + (int)(tn * bar_w);
        // Tick line below the bar
        MoveToEx(hdc, tx, bar_y + bar_h, nullptr);
        LineTo  (hdc, tx, bar_y + bar_h + 5);
        // Value label
        double val = tn * max_val;
        char tick_buf[32];
        if (max_val >= 100.0)
            std::snprintf(tick_buf, sizeof(tick_buf), "%.0f", val);
        else if (max_val >= 1.0)
            std::snprintf(tick_buf, sizeof(tick_buf), "%.1f", val);
        else
            std::snprintf(tick_buf, sizeof(tick_buf), "%.3f", val);
        std::wstring tw = to_wide(tick_buf);
        SetTextColor(hdc, value_to_colorref(tn));
        int text_w = (int)tw.size() * 6;  // rough char width
        TextOutW(hdc, tx - text_w / 2, bar_y + bar_h + 6, tw.c_str(), (int)tw.size());
    }

    SelectObject(hdc, old_pen);
    DeleteObject(tick_pen);

}

// ============================================================================
// CUSTOM DRAWING — wind distribution
// ============================================================================

static void draw_wind_distribution(HDC hdc, RECT r) {
    HBRUSH bg = CreateSolidBrush(RGB(252, 252, 254));
    FillRect(hdc, &r, bg);
    DeleteObject(bg);
    FrameRect(hdc, &r, (HBRUSH)GetStockObject(GRAY_BRUSH));

    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, g_hFont);

    const auto location = g_state.current_climate();
    const char* dist_name = location.uniform ? "Uniform" : "Weibull";

    // Title
    SetTextColor(hdc, RGB(0, 80, 150));
    SelectObject(hdc, g_hFontBold);
    char title[256];
    std::snprintf(title, sizeof(title),
        "Wind Speed Distribution (%s)  -  %s",
        dist_name, location.location.c_str());
    std::wstring tw = to_wide(title);
    RECT tr = r;
    tr.top    += 6;
    tr.bottom  = tr.top + 18;
    DrawTextW(hdc, tw.c_str(), -1, &tr, DT_CENTER | DT_SINGLELINE);
    SelectObject(hdc, g_hFont);

    // Plot area
    RECT plot;
    plot.left   = r.left  + 50;
    plot.right  = r.right - 50;
    plot.top    = r.top   + 30;
    plot.bottom = r.bottom - 50;
    int plot_w = plot.right  - plot.left;
    int plot_h = plot.bottom - plot.top;
    if (plot_w < 50 || plot_h < 30) return;

    // Cap the plot range at the 99.99th percentile (with a small margin) so
    // unreachable tail wind speeds don't make the rest of the plot tiny.
    double v_ceiling = WindData::climate_percentile(location, 0.9999);
    double max_wind  = v_ceiling * 1.05;
    if (max_wind < 1.0) max_wind = 20.0;

    // Build the PDF samples (Weibull curve OR uniform rectangle)
    int n_samples = plot_w;
    std::vector<double> pdf((size_t)n_samples, 0.0);
    double max_pdf = 0.0;
    for (int i = 0; i < n_samples; i++) {
        double v = (double)i / (n_samples - 1) * max_wind;
        double p = 0.0;
        if (location.uniform) {
            double range = location.v_max - location.v_min;
            if (range > 0 && v >= location.v_min && v <= location.v_max) {
                p = 1.0 / range;
            }
        } else if (v > 0) {
            double k = location.weibull_k;
            double c = location.weibull_c;
            p = (k / c) * std::pow(v / c, k - 1) * std::exp(-std::pow(v / c, k));
        }
        pdf[i] = p;
        if (p > max_pdf) max_pdf = p;
    }
    if (max_pdf < 1e-12) max_pdf = 1.0;

    // Axes
    HPEN axis_pen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
    HGDIOBJ old_pen = SelectObject(hdc, axis_pen);
    MoveToEx(hdc, plot.left,  plot.top,    nullptr);
    LineTo  (hdc, plot.left,  plot.bottom);
    LineTo  (hdc, plot.right, plot.bottom);
    SelectObject(hdc, old_pen);
    DeleteObject(axis_pen);

    // Percentile vertical lines
    auto draw_pct_line = [&](double v, COLORREF c, const char* label) {
        int x = plot.left + (int)((v / max_wind) * plot_w);
        if (x < plot.left || x > plot.right) return;
        HPEN pen = CreatePen(PS_DASH, 1, c);
        HGDIOBJ o = SelectObject(hdc, pen);
        MoveToEx(hdc, x, plot.top,    nullptr);
        LineTo  (hdc, x, plot.bottom);
        SelectObject(hdc, o);
        DeleteObject(pen);

        SetTextColor(hdc, c);
        std::wstring lw = to_wide(label);
        TextOutW(hdc, x - 12, plot.top - 14, lw.c_str(), (int)lw.size());
    };
    SelectObject(hdc, g_hFontSmall);
    draw_pct_line(location.percentile_50, RGB( 40, 170,  40), "50%");
    draw_pct_line(location.percentile_75, RGB(220, 180,   0), "75%");
    draw_pct_line(location.percentile_90, RGB(220,  50,  50), "90%");
    SelectObject(hdc, g_hFont);

    // Critical wind speeds (per BC) — solid magenta lines
    if (!g_state.results.empty()) {
        for (size_t i = 0; i < g_state.results.size(); i++) {
            const auto& bc_result = g_state.results[i];
            if (bc_result.modes.empty()) continue;
            double V_crit = bc_result.modes[0].V_critical_ms;
            int x = plot.left + (int)((V_crit / max_wind) * plot_w);
            if (x < plot.left || x > plot.right) continue;
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(200, 0, 200));
            HGDIOBJ o = SelectObject(hdc, pen);
            MoveToEx(hdc, x, plot.top,    nullptr);
            LineTo  (hdc, x, plot.bottom);
            SelectObject(hdc, o);
            DeleteObject(pen);
        }
    }

    // Weibull PDF curve
    HPEN curve_pen = CreatePen(PS_SOLID, 2, RGB(0, 120, 200));
    old_pen = SelectObject(hdc, curve_pen);
    bool started = false;
    for (int i = 0; i < n_samples; i++) {
        int x = plot.left + i;
        int y = plot.bottom - (int)((pdf[i] / max_pdf) * (plot_h - 4));
        if (!started) { MoveToEx(hdc, x, y, nullptr); started = true; }
        else            LineTo  (hdc, x, y);
    }
    SelectObject(hdc, old_pen);
    DeleteObject(curve_pen);

    // X-axis tick labels
    SetTextColor(hdc, RGB(60, 60, 60));
    SelectObject(hdc, g_hFontSmall);
    for (int s = 0; s <= 5; s++) {
        double v = (max_wind / 5) * s;
        int x = plot.left + (int)((v / max_wind) * plot_w);
        char b[32];
        std::snprintf(b, sizeof(b), "%.0f", v);
        std::wstring bw = to_wide(b);
        TextOutW(hdc, x - 8, plot.bottom + 2, bw.c_str(), (int)bw.size());
    }
    TextOutW(hdc, plot.right - 30, plot.bottom + 16, L"m/s", 3);

    // Legend
    SelectObject(hdc, g_hFont);
    SetTextColor(hdc, RGB(60, 60, 60));
    char legend[256];
    std::snprintf(legend, sizeof(legend),
        "Mean=%.1f m/s   |   Magenta = critical V (per BC)   |   "
        "--- Green=50%%   Yellow=75%%   Red=90%%",
        location.mean_speed_ms);
    std::wstring lw = to_wide(legend);
    RECT lr = r;
    lr.top    = plot.bottom + 18;
    lr.bottom = lr.top + 16;
    DrawTextW(hdc, lw.c_str(), -1, &lr, DT_CENTER | DT_SINGLELINE);
}

// ============================================================================
// CUSTOM DRAWING — warning banner
// ============================================================================

// Color for a given warning level (background + text).
static void warning_level_colors(WindData::WarningLevel lvl,
                                 COLORREF& bg_out, COLORREF& fg_out) {
    switch (lvl) {
        case WindData::CRITICAL:  bg_out = RGB(200,  30,  30); fg_out = RGB(255,255,255); break;
        case WindData::HIGH_RISK: bg_out = RGB(230, 100,  30); fg_out = RGB(255,255,255); break;
        case WindData::WARNING:   bg_out = RGB(240, 200,  30); fg_out = RGB( 40, 40, 40); break;
        case WindData::CAUTION:   bg_out = RGB(100, 180, 220); fg_out = RGB(255,255,255); break;
        default:                  bg_out = RGB(120, 200, 100); fg_out = RGB(255,255,255); break;
    }
}

static void draw_warning_banner(HDC hdc, RECT r) {
    // Background
    HBRUSH bg = CreateSolidBrush(RGB(248, 248, 248));
    FillRect(hdc, &r, bg);
    DeleteObject(bg);
    FrameRect(hdc, &r, (HBRUSH)GetStockObject(GRAY_BRUSH));

    if (g_state.warnings.empty()) return;

    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, g_hFontSmall);

    int row_h = 15;
    int y = r.top + 4;
    int x = r.left + 6;
    int avail_w = r.right - r.left - 12;

    // One line per BC
    for (size_t i = 0; i < g_state.warnings.size() && i < g_state.results.size(); i++) {
        if (y + row_h > r.bottom) break;   // clip if panel too small

        const auto& w   = g_state.warnings[i];
        const auto& bc  = g_state.results[i];

        // Shorten BC name
        std::string bc_short = bc.bc_name;
        if (bc_short.find("Fixed-Fixed") != std::string::npos)       bc_short = "Fix-Fix";
        else if (bc_short.find("Fixed-Hinged") != std::string::npos) bc_short = "Fix-Hng";
        else if (bc_short.find("Hinged-Hinged") != std::string::npos)bc_short = "Hng-Hng";
        else if (bc_short.find("Fixed-Free") != std::string::npos)   bc_short = "Cantilev";
        else if (bc_short.find("Hinged-Free") != std::string::npos)  bc_short = "Hng-Free";
        else if (bc_short.find("Free-Free") != std::string::npos)    bc_short = "Free-Free";

        // Small colored tag for the level
        COLORREF tag_bg, tag_fg;
        warning_level_colors(w.level, tag_bg, tag_fg);

        int tag_w = 80;
        RECT tag_r = { x, y, x + tag_w, y + row_h };
        HBRUSH tb = CreateSolidBrush(tag_bg);
        FillRect(hdc, &tag_r, tb);
        DeleteObject(tb);

        SetTextColor(hdc, tag_fg);
        std::wstring lvl_w = to_wide(w.level_name);
        DrawTextW(hdc, lvl_w.c_str(), -1, &tag_r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // BC name + details
        SetTextColor(hdc, RGB(40, 40, 40));
        char line[256];
        std::snprintf(line, sizeof(line),
            "  %-10s  V=%.2f m/s  wind=%.1f%%  stress=%.1f%% yield",
            bc_short.c_str(),
            bc.modes.empty() ? 0.0 : bc.modes[0].V_critical_ms,
            w.wind_percentile,
            w.stress_ratio * 100.0);
        std::wstring lw = to_wide(line);
        TextOutW(hdc, x + tag_w + 4, y, lw.c_str(), (int)lw.size());

        y += row_h;
    }
}

// ============================================================================
// LAYOUT (recompute on WM_SIZE / WM_VSCROLL / WM_MOUSEWHEEL)
// ============================================================================

static void LayoutWindow(int width, int height) {
    // Virtual layout — y values are content-relative (before scroll).
    int input_bottom_vy = MARGIN + INPUTS_H;
    int modes_vy   = input_bottom_vy + MARGIN;
    int modes_h    = 350;
    int legend_vy  = modes_vy + modes_h + MARGIN;
    int legend_h   = 58;
    int wind_vy    = legend_vy + legend_h + MARGIN;
    int wind_h     = 200;
    int warn_vy    = wind_vy + wind_h + MARGIN;
    int warn_h     = 100;
    int btn_vy     = warn_vy + warn_h + MARGIN;
    int btn_h      = 28;
    g_content_h    = btn_vy + btn_h + MARGIN;

    // Update scroll bar range / page (BEFORE clamping g_scroll_y).
    {
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask  = SIF_RANGE | SIF_PAGE;
        si.nMin   = 0;
        si.nMax   = g_content_h - 1;       // inclusive max
        si.nPage  = (UINT)(height > 0 ? height : 1);
        SetScrollInfo(g_hwnd, SB_VERT, &si, TRUE);
    }

    // Clamp current scroll position to the new range.
    int max_scroll = std::max(0, g_content_h - height);
    if (g_scroll_y > max_scroll) g_scroll_y = max_scroll;
    if (g_scroll_y < 0)          g_scroll_y = 0;

    {
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask  = SIF_POS;
        si.nPos   = g_scroll_y;
        SetScrollInfo(g_hwnd, SB_VERT, &si, TRUE);
    }

    int dy = -g_scroll_y;

    // Reposition every tracked input-row child control with the scroll offset.
    HDWP hdwp = BeginDeferWindowPos((int)g_pos_ctrls.size() + 4);
    for (const auto& p : g_pos_ctrls) {
        if (!p.h) continue;
        hdwp = DeferWindowPos(hdwp, p.h, nullptr,
                              p.x, p.vy + dy, p.w, p.ch,
                              SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // Bottom row: width-relative x, virtual y from layout above.
    int btn_w = 150;
    hdwp = DeferWindowPos(hdwp, g_hSaveButton, nullptr,
        width - 2 * btn_w - 2 * MARGIN, btn_vy + dy, btn_w, btn_h,
        SWP_NOZORDER | SWP_NOACTIVATE);
    hdwp = DeferWindowPos(hdwp, g_hQuitButton, nullptr,
        width - 1 * btn_w - 1 * MARGIN, btn_vy + dy, btn_w, btn_h,
        SWP_NOZORDER | SWP_NOACTIVATE);
    hdwp = DeferWindowPos(hdwp, g_hStatusText, nullptr,
        MARGIN, btn_vy + dy + 6, width - 2 * btn_w - 3 * MARGIN, 18,
        SWP_NOZORDER | SWP_NOACTIVATE);
    EndDeferWindowPos(hdwp);

    // Custom-draw rects in CLIENT coordinates (virtual y + dy).
    g_rectModes   = { MARGIN, modes_vy  + dy, width - MARGIN, modes_vy  + modes_h  + dy };
    g_rectLegend  = { MARGIN, legend_vy + dy, width - MARGIN, legend_vy + legend_h + dy };
    g_rectWind    = { MARGIN, wind_vy   + dy, width - MARGIN, wind_vy   + wind_h   + dy };
    g_rectWarning = { MARGIN, warn_vy   + dy, width - MARGIN, warn_vy   + warn_h   + dy };
}

// Apply a new scroll position, clamp, relayout, repaint.
static void ApplyScroll(int new_pos) {
    RECT cr;
    GetClientRect(g_hwnd, &cr);
    int max_scroll = std::max(0, g_content_h - (int)cr.bottom);
    if (new_pos < 0)          new_pos = 0;
    if (new_pos > max_scroll) new_pos = max_scroll;
    if (new_pos == g_scroll_y) return;
    g_scroll_y = new_pos;
    LayoutWindow(cr.right, cr.bottom);
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

// ============================================================================
// CONTROL CREATION
// ============================================================================

static void CreateControls(HWND hwnd) {
    HINSTANCE hi = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);

    // All "label/edit/combo/button" lambdas remember the control's virtual
    // position so LayoutWindow can reposition them when the user scrolls.
    auto label = [&](LPCWSTR text, int x, int y, int w) {
        HWND h = CreateWindowW(L"STATIC", text,
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            x, y, w, 20, hwnd, nullptr, hi, nullptr);
        g_pos_ctrls.push_back({ h, x, y, w, 20 });
        return h;
    };
    auto edit = [&](LPCWSTR text, int x, int y, int w, int id) {
        HWND h = CreateWindowW(L"EDIT", text,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_LEFT | ES_AUTOHSCROLL,
            x, y, w, 22, hwnd, (HMENU)(intptr_t)id, hi, nullptr);
        g_pos_ctrls.push_back({ h, x, y, w, 22 });
        return h;
    };
    auto combo = [&](int x, int y, int w, int id) {
        HWND h = CreateWindowW(L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST,
            x, y, w, 220, hwnd, (HMENU)(intptr_t)id, hi, nullptr);
        g_pos_ctrls.push_back({ h, x, y, w, 220 });
        return h;
    };
    auto button = [&](LPCWSTR text, int x, int y, int w, int h, int id, DWORD extra = 0) {
        HWND hb = CreateWindowW(L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | extra,
            x, y, w, h, hwnd, (HMENU)(intptr_t)id, hi, nullptr);
        g_pos_ctrls.push_back({ hb, x, y, w, h });
        return hb;
    };

    int y = MARGIN;

    // ----- Column 1: Material, D, t, L -----
    int col1_x = MARGIN;
    label(L"Material:",       col1_x, y + 4,           LABEL_W);
    g_hMaterialCombo = combo(col1_x + LABEL_W, y, EDIT_W + 40, ID_MATERIAL);

    label(L"Diameter (mm):",  col1_x, y + 1*ROW_H + 4, LABEL_W);
    g_hDEdit = edit(L"200.0", col1_x + LABEL_W, y + 1*ROW_H, EDIT_W, ID_DEDIT);

    label(L"Thickness (mm):", col1_x, y + 2*ROW_H + 4, LABEL_W);
    g_hTEdit = edit(L"6.0",   col1_x + LABEL_W, y + 2*ROW_H, EDIT_W, ID_TEDIT);

    label(L"Length (m):",     col1_x, y + 3*ROW_H + 4, LABEL_W);
    g_hLEdit = edit(L"6.0",   col1_x + LABEL_W, y + 3*ROW_H, EDIT_W, ID_LEDIT);

    // ----- Column 2: damping, axial, location -----
    int col2_x = col1_x + LABEL_W + EDIT_W + 70;

    label(L"Damping (zeta):", col2_x, y + 4,           LABEL_W);
    g_hDampEdit = edit(L"0.010", col2_x + LABEL_W, y, EDIT_W, ID_DAMPEDIT);

    label(L"Axial F (kN):",   col2_x, y + 1*ROW_H + 4, LABEL_W);
    g_hAxialEdit = edit(L"0.0", col2_x + LABEL_W, y + 1*ROW_H, EDIT_W, ID_AXIALEDIT);

    label(L"Location:",       col2_x, y + 2*ROW_H + 4, LABEL_W);
    g_hLocationCombo = combo(col2_x + LABEL_W, y + 2*ROW_H, EDIT_W + 100, ID_LOCATION);

    // ----- Column 3: Calculated properties -----
    int col3_x = col2_x + LABEL_W + EDIT_W + 130;
    label(L"Calculated:", col3_x, y + 4, 200);
    g_hInfoText = CreateWindowW(L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        col3_x, y + ROW_H, 230, 100,
        hwnd, nullptr, hi, nullptr);
    g_pos_ctrls.push_back({ g_hInfoText, col3_x, y + ROW_H, 230, 100 });

    // ----- Column 4: Color variable + uncertainty -----
    int col4_x = col3_x + 240;
    label(L"Color by:", col4_x, y + 4, 80);
    g_hUDLRadio    = button(L"UDL",    col4_x, y + 1*ROW_H,  90, 22, ID_RADIO_UDL,
                            BS_AUTORADIOBUTTON | WS_GROUP);
    g_hMomentRadio = button(L"Moment", col4_x, y + 2*ROW_H,  90, 22, ID_RADIO_MOMENT,
                            BS_AUTORADIOBUTTON);
    g_hStressRadio = button(L"Stress", col4_x, y + 3*ROW_H,  90, 22, ID_RADIO_STRESS,
                            BS_AUTORADIOBUTTON);
    SendMessage(g_hStressRadio, BM_SETCHECK, BST_CHECKED, 0);

    g_hUncertaintyCheck = button(L"Show uncertainty",
        col4_x + 100, y + 1*ROW_H, 160, 22, ID_CHK_UNCERT, BS_AUTOCHECKBOX);
    SendMessage(g_hUncertaintyCheck, BM_SETCHECK, BST_CHECKED, 0);

    // ----- Row 4: wind range (only for Custom Uniform location) -----
    int row4_y = y + 4*ROW_H;
    label(L"Wind range (Custom Uniform only):", col1_x, row4_y + 4, 220);
    g_hVminEdit = edit(L"0.0",  col1_x + 230, row4_y, 60, ID_VMINEDIT);
    label(L"to", col1_x + 295, row4_y + 4, 16);
    g_hVmaxEdit = edit(L"25.0", col1_x + 315, row4_y, 60, ID_VMAXEDIT);
    label(L"m/s", col1_x + 380, row4_y + 4, 30);

    // ----- Row 5: custom material E and density (only for Custom material) -----
    int row5_y = y + 5*ROW_H;
    label(L"Custom material:", col1_x, row5_y + 4, 110);
    label(L"E (GPa):", col1_x + 115, row5_y + 4, 55);
    g_hCustomEEdit   = edit(L"200.0", col1_x + 172, row5_y, 60, ID_CUSTOM_E);
    label(L"Density (kg/m3):", col1_x + 245, row5_y + 4, 105);
    g_hCustomRhoEdit = edit(L"7850", col1_x + 355, row5_y, 60, ID_CUSTOM_RHO);

    // ----- Bottom row buttons (positioned later by LayoutWindow) -----
    g_hSaveButton = button(L"Print Report...", 0, 0, 150, 28, ID_BTN_SAVE);
    g_hQuitButton = button(L"Quit",            0, 0, 150, 28, ID_BTN_QUIT);

    g_hStatusText = CreateWindowW(L"STATIC", L"Ready.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 400, 18, hwnd, nullptr, hi, nullptr);

    // Populate combos
    for (const auto& mat : MATERIALS) {
        SendMessageW(g_hMaterialCombo, CB_ADDSTRING, 0,
                     (LPARAM)to_wide(mat.name).c_str());
    }
    SendMessage(g_hMaterialCombo, CB_SETCURSEL, g_state.material_index, 0);

    for (const auto& loc : WindData::PORT_CLIMATES) {
        SendMessageW(g_hLocationCombo, CB_ADDSTRING, 0,
                     (LPARAM)to_wide(loc.location).c_str());
    }
    SendMessage(g_hLocationCombo, CB_SETCURSEL, g_state.location_index, 0);

    // Sync edit fields with initial state
    set_edit_double(g_hDEdit,     g_state.D_mm,           1);
    set_edit_double(g_hTEdit,     g_state.t_mm,           2);
    set_edit_double(g_hLEdit,     g_state.L_m,            2);
    set_edit_double(g_hDampEdit,  g_state.damping,        4);
    set_edit_double(g_hAxialEdit, g_state.axial_force_kN, 1);
    set_edit_double(g_hVminEdit,  g_state.custom_wind_min, 1);
    set_edit_double(g_hVmaxEdit,  g_state.custom_wind_max, 1);
    set_edit_double(g_hCustomEEdit,   g_state.custom_E_GPa, 1);
    set_edit_double(g_hCustomRhoEdit, g_state.custom_rho, 0);

    // Apply font to every child (labels, edits, combos, buttons, statics)
    EnumChildWindows(hwnd, [](HWND h, LPARAM lp) -> BOOL {
        SendMessageW(h, WM_SETFONT, (WPARAM)lp, TRUE);
        return TRUE;
    }, (LPARAM)g_hFont);
}

// ============================================================================
// STATE REFRESH (read controls → update state → recalc → invalidate)
// ============================================================================

// Enable / disable conditional fields based on current selections.
static void UpdateConditionalFields() {
    // Wind range: only active for Custom Uniform location
    bool wind_en = g_state.current_climate().uniform;
    if (g_hVminEdit) EnableWindow(g_hVminEdit, wind_en);
    if (g_hVmaxEdit) EnableWindow(g_hVmaxEdit, wind_en);

    // Custom material E / density: only active when material = "Custom"
    bool mat_en = (static_cast<size_t>(g_state.material_index) >= MATERIALS.size() - 1);
    if (g_hCustomEEdit)   EnableWindow(g_hCustomEEdit,   mat_en);
    if (g_hCustomRhoEdit) EnableWindow(g_hCustomRhoEdit, mat_en);
}

static void UpdateInfoText() {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "  I  = %.0f mm^4\r\n"
        "  A  = %.1f mm^2\r\n"
        "  mu = %.2f kg/m\r\n"
        "  r  = %.1f mm",
        g_state.member.I_m4 * 1e12,
        g_state.member.A_m2 * 1e6,
        g_state.member.mu_kg_m,
        g_state.member.r_gyration_mm);
    SetWindowTextW(g_hInfoText, to_wide(buf).c_str());
}

static void RefreshState() {
    g_state.D_mm            = parse_double(g_hDEdit,     g_state.D_mm);
    g_state.t_mm            = parse_double(g_hTEdit,     g_state.t_mm);
    g_state.L_m             = parse_double(g_hLEdit,     g_state.L_m);
    g_state.damping         = parse_double(g_hDampEdit,  g_state.damping);
    g_state.axial_force_kN  = parse_double(g_hAxialEdit, g_state.axial_force_kN);
    g_state.custom_wind_min = parse_double(g_hVminEdit,  g_state.custom_wind_min);
    g_state.custom_wind_max = parse_double(g_hVmaxEdit,  g_state.custom_wind_max);
    g_state.custom_E_GPa   = parse_double(g_hCustomEEdit,   g_state.custom_E_GPa);
    g_state.custom_rho     = parse_double(g_hCustomRhoEdit,  g_state.custom_rho);

    // Sanity clamps so we never crash the physics
    if (g_state.D_mm < 1.0)               g_state.D_mm = 1.0;
    if (g_state.t_mm < 0.1)               g_state.t_mm = 0.1;
    if (g_state.t_mm > g_state.D_mm / 2)  g_state.t_mm = g_state.D_mm / 2;
    if (g_state.L_m  < 0.1)               g_state.L_m  = 0.1;
    if (g_state.damping < 0.0001)         g_state.damping = 0.0001;
    if (g_state.damping > 0.5)            g_state.damping = 0.5;
    if (g_state.custom_wind_min < 0.0)    g_state.custom_wind_min = 0.0;
    if (g_state.custom_wind_max <= g_state.custom_wind_min)
        g_state.custom_wind_max = g_state.custom_wind_min + 0.1;
    if (g_state.custom_E_GPa < 0.1)     g_state.custom_E_GPa = 0.1;
    if (g_state.custom_rho   < 1.0)     g_state.custom_rho   = 1.0;

    g_state.material_index = (int)SendMessage(g_hMaterialCombo, CB_GETCURSEL, 0, 0);
    g_state.location_index = (int)SendMessage(g_hLocationCombo, CB_GETCURSEL, 0, 0);
    if (g_state.material_index < 0) g_state.material_index = 0;
    if (g_state.location_index < 0) g_state.location_index = 0;

    g_state.update();
    UpdateInfoText();
    UpdateConditionalFields();

    // Repaint custom-drawn regions only
    InvalidateRect(g_hwnd, &g_rectModes,   FALSE);
    InvalidateRect(g_hwnd, &g_rectLegend,  FALSE);
    InvalidateRect(g_hwnd, &g_rectWind,    FALSE);
    InvalidateRect(g_hwnd, &g_rectWarning, FALSE);
}

// ============================================================================
// PRINT REPORT — DOCX EXPORT WITH LOGO + STAAD-STYLE SECTION DIALOG
// ============================================================================
//
// We build a real .docx (Office Open XML) from scratch:
//   - a tiny store-mode ZIP writer (no compression)
//   - hand-rolled OOXML for the document body
//   - the EMS-Tech logo embedded as PNG
//   - mode-shape and wind-distribution panels rendered to PNG via GDI+
// No external libraries beyond gdi32 / gdiplus / ole32 (all Windows built-in).

// ----- CRC-32 (needed by ZIP) -----
static uint32_t g_crc32_table[256];
static bool     g_crc32_inited = false;

static void crc32_init() {
    if (g_crc32_inited) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        g_crc32_table[i] = c;
    }
    g_crc32_inited = true;
}

static uint32_t crc32_compute(const uint8_t* data, size_t len) {
    crc32_init();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        c = g_crc32_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// ----- Tiny ZIP writer (store mode, no compression) -----
struct ZipEntry {
    std::string          name;
    std::vector<uint8_t> data;
};

static void zip_le16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(uint8_t(v & 0xFF));
    b.push_back(uint8_t((v >> 8) & 0xFF));
}
static void zip_le32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(uint8_t( v        & 0xFF));
    b.push_back(uint8_t((v >>  8) & 0xFF));
    b.push_back(uint8_t((v >> 16) & 0xFF));
    b.push_back(uint8_t((v >> 24) & 0xFF));
}

static bool write_zip(const std::string& filename,
                      const std::vector<ZipEntry>& entries) {
    std::vector<uint8_t> buf;
    std::vector<uint32_t> offsets(entries.size());
    std::vector<uint32_t> crcs   (entries.size());

    for (size_t i = 0; i < entries.size(); i++) {
        const auto& e = entries[i];
        offsets[i] = (uint32_t)buf.size();
        crcs[i]    = crc32_compute(e.data.data(), e.data.size());

        zip_le32(buf, 0x04034b50);                      // local header sig
        zip_le16(buf, 20);                              // version needed
        zip_le16(buf, 0);                               // flags
        zip_le16(buf, 0);                               // method = stored
        zip_le16(buf, 0);                               // mod time
        zip_le16(buf, 0x21);                            // mod date (1980-01-01)
        zip_le32(buf, crcs[i]);
        zip_le32(buf, (uint32_t)e.data.size());         // compressed size
        zip_le32(buf, (uint32_t)e.data.size());         // uncompressed size
        zip_le16(buf, (uint16_t)e.name.size());
        zip_le16(buf, 0);                               // extra length
        for (char c : e.name) buf.push_back((uint8_t)c);
        buf.insert(buf.end(), e.data.begin(), e.data.end());
    }

    uint32_t cd_offset = (uint32_t)buf.size();
    for (size_t i = 0; i < entries.size(); i++) {
        const auto& e = entries[i];
        zip_le32(buf, 0x02014b50);                      // central dir sig
        zip_le16(buf, 20);                              // version made by
        zip_le16(buf, 20);                              // version needed
        zip_le16(buf, 0);                               // flags
        zip_le16(buf, 0);                               // method
        zip_le16(buf, 0);                               // mod time
        zip_le16(buf, 0x21);                            // mod date
        zip_le32(buf, crcs[i]);
        zip_le32(buf, (uint32_t)e.data.size());
        zip_le32(buf, (uint32_t)e.data.size());
        zip_le16(buf, (uint16_t)e.name.size());
        zip_le16(buf, 0);                               // extra length
        zip_le16(buf, 0);                               // comment length
        zip_le16(buf, 0);                               // disk number
        zip_le16(buf, 0);                               // internal attrs
        zip_le32(buf, 0);                               // external attrs
        zip_le32(buf, offsets[i]);                      // local header offset
        for (char c : e.name) buf.push_back((uint8_t)c);
    }
    uint32_t cd_size = (uint32_t)buf.size() - cd_offset;

    zip_le32(buf, 0x06054b50);                          // EOCD sig
    zip_le16(buf, 0);                                   // disk number
    zip_le16(buf, 0);                                   // start disk
    zip_le16(buf, (uint16_t)entries.size());            // entries this disk
    zip_le16(buf, (uint16_t)entries.size());            // total entries
    zip_le32(buf, cd_size);
    zip_le32(buf, cd_offset);
    zip_le16(buf, 0);                                   // comment length

    std::ofstream out(filename, std::ios::binary);
    if (!out) return false;
    out.write((const char*)buf.data(), (std::streamsize)buf.size());
    return out.good();
}

// ----- File reading helpers -----
static std::vector<uint8_t> read_file_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto sz = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data((size_t)sz);
    if (sz > 0) f.read((char*)data.data(), sz);
    return data;
}

// Look for `name` next to the executable and return its bytes (empty on miss).
static std::vector<uint8_t> read_file_next_to_exe(const std::wstring& name) {
    wchar_t exe_path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::wstring p = exe_path;
    auto slash = p.find_last_of(L"\\/");
    if (slash != std::wstring::npos) p.erase(slash + 1);
    p += name;
    return read_file_bytes(from_wide(p));
}

// ----- GDI+ helpers: render a callback into a bitmap, encode to PNG bytes -----
static int gdiplus_get_png_clsid(CLSID* clsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    std::vector<uint8_t> mem(size);
    Gdiplus::ImageCodecInfo* info = (Gdiplus::ImageCodecInfo*)mem.data();
    Gdiplus::GetImageEncoders(num, size, info);
    for (UINT j = 0; j < num; j++) {
        if (wcscmp(info[j].MimeType, L"image/png") == 0) {
            *clsid = info[j].Clsid;
            return (int)j;
        }
    }
    return -1;
}

static std::vector<uint8_t> hbitmap_to_png_bytes(HBITMAP hbm) {
    Gdiplus::Bitmap bmp(hbm, nullptr);
    CLSID png_clsid;
    if (gdiplus_get_png_clsid(&png_clsid) < 0) return {};

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK) return {};
    if (bmp.Save(stream, &png_clsid, nullptr) != Gdiplus::Ok) {
        stream->Release();
        return {};
    }
    HGLOBAL hg = nullptr;
    GetHGlobalFromStream(stream, &hg);
    SIZE_T sz = GlobalSize(hg);
    void*  p  = GlobalLock(hg);
    std::vector<uint8_t> out((uint8_t*)p, (uint8_t*)p + sz);
    GlobalUnlock(hg);
    stream->Release();
    return out;
}

// Render a draw_* function into a fresh bitmap and return the PNG bytes.
static std::vector<uint8_t> render_panel_png(int width, int height,
        std::function<void(HDC, RECT)> draw_fn) {
    HDC screen = GetDC(nullptr);
    HDC mem    = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, width, height);
    HGDIOBJ ob  = SelectObject(mem, bmp);

    RECT r = { 0, 0, width, height };
    HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(mem, &r, bg);
    DeleteObject(bg);
    SetBkMode(mem, TRANSPARENT);

    draw_fn(mem, r);

    SelectObject(mem, ob);
    auto png = hbitmap_to_png_bytes(bmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return png;
}

// ----- XML / OOXML helpers -----
static std::string xml_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '&':  out += "&amp;";  break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;        break;
        }
    }
    return out;
}

// w:sz uses half-points; w:val color uses RRGGBB hex.
static std::string ooxml_paragraph(const std::string& text,
                                   bool bold = false,
                                   int size_pt = 11,
                                   const char* color_hex = "000000") {
    std::string sz = std::to_string(size_pt * 2);
    std::string out = "<w:p><w:r><w:rPr>";
    out += "<w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\"/>";
    out += "<w:sz w:val=\"" + sz + "\"/>";
    out += "<w:szCs w:val=\"" + sz + "\"/>";
    if (bold) out += "<w:b/><w:bCs/>";
    out += std::string("<w:color w:val=\"") + color_hex + "\"/>";
    out += "</w:rPr><w:t xml:space=\"preserve\">" + xml_escape(text) + "</w:t></w:r></w:p>";
    return out;
}

static std::string ooxml_heading(const std::string& text, int level = 1) {
    int size = (level == 1) ? 18 : (level == 2 ? 14 : 12);
    std::string sz = std::to_string(size * 2);
    std::string out =
        "<w:p><w:pPr><w:spacing w:before=\"240\" w:after=\"120\"/></w:pPr>";
    out += "<w:r><w:rPr>";
    out += "<w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\"/>";
    out += "<w:sz w:val=\"" + sz + "\"/><w:szCs w:val=\"" + sz + "\"/>";
    out += "<w:b/><w:bCs/><w:color w:val=\"1F4E78\"/>";
    out += "</w:rPr><w:t xml:space=\"preserve\">" + xml_escape(text) + "</w:t></w:r></w:p>";
    return out;
}

static std::string ooxml_table_cell(const std::string& text,
                                    int width_dxa,
                                    bool bold = false) {
    std::string out = "<w:tc>";
    out += "<w:tcPr><w:tcW w:w=\"" + std::to_string(width_dxa) + "\" w:type=\"dxa\"/>";
    out += "<w:tcBorders>";
    out += "<w:top w:val=\"single\" w:sz=\"4\" w:color=\"888888\"/>";
    out += "<w:left w:val=\"single\" w:sz=\"4\" w:color=\"888888\"/>";
    out += "<w:bottom w:val=\"single\" w:sz=\"4\" w:color=\"888888\"/>";
    out += "<w:right w:val=\"single\" w:sz=\"4\" w:color=\"888888\"/>";
    out += "</w:tcBorders></w:tcPr>";
    out += "<w:p><w:pPr><w:spacing w:before=\"40\" w:after=\"40\"/></w:pPr>";
    out += "<w:r><w:rPr><w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\"/>";
    out += "<w:sz w:val=\"18\"/>";
    if (bold) out += "<w:b/><w:bCs/>";
    out += "</w:rPr><w:t xml:space=\"preserve\">" + xml_escape(text) + "</w:t></w:r>";
    out += "</w:p></w:tc>";
    return out;
}

// Build a table from a row-major matrix. First row is treated as header (bold).
static std::string ooxml_table(const std::vector<std::vector<std::string>>& rows,
                               const std::vector<int>& widths_dxa) {
    std::string out =
        "<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/></w:tblPr>";
    for (size_t i = 0; i < rows.size(); i++) {
        out += "<w:tr>";
        for (size_t j = 0; j < rows[i].size(); j++) {
            int w = (j < widths_dxa.size()) ? widths_dxa[j] : 1500;
            out += ooxml_table_cell(rows[i][j], w, /*bold*/ i == 0);
        }
        out += "</w:tr>";
    }
    out += "</w:tbl>";
    return out;
}

// ----- DOCX writer -----
//
// Builds the four required parts of a minimal OOXML word doc:
//   [Content_Types].xml
//   _rels/.rels
//   word/_rels/document.xml.rels
//   word/document.xml
// plus optional images under word/media/. Each image gets a relationship Id
// (rId10..) and an inline drawing block in document.xml.

struct DocxImage {
    std::string filename;     // e.g. "image1.png"
    std::vector<uint8_t> data;
    int  width_px;
    int  height_px;
    std::string rel_id;       // e.g. "rId10"
};

// EMU per pixel at 96 DPI
static constexpr int EMU_PER_PIXEL = 9525;

static std::string ooxml_inline_image(const DocxImage& img, int draw_id) {
    int cx = img.width_px  * EMU_PER_PIXEL;
    int cy = img.height_px * EMU_PER_PIXEL;
    std::string id = std::to_string(draw_id);
    std::string s_cx = std::to_string(cx);
    std::string s_cy = std::to_string(cy);
    std::string out = "<w:p><w:r><w:drawing>";
    out += "<wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">";
    out += "<wp:extent cx=\"" + s_cx + "\" cy=\"" + s_cy + "\"/>";
    out += "<wp:effectExtent l=\"0\" t=\"0\" r=\"0\" b=\"0\"/>";
    out += "<wp:docPr id=\"" + id + "\" name=\"Image" + id + "\"/>";
    out += "<wp:cNvGraphicFramePr/>";
    out += "<a:graphic xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">";
    out += "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">";
    out += "<pic:pic xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">";
    out += "<pic:nvPicPr><pic:cNvPr id=\"" + id + "\" name=\"" + img.filename + "\"/>";
    out += "<pic:cNvPicPr/></pic:nvPicPr>";
    out += "<pic:blipFill><a:blip r:embed=\"" + img.rel_id + "\"/>";
    out += "<a:stretch><a:fillRect/></a:stretch></pic:blipFill>";
    out += "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/>";
    out += "<a:ext cx=\"" + s_cx + "\" cy=\"" + s_cy + "\"/></a:xfrm>";
    out += "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr>";
    out += "</pic:pic></a:graphicData></a:graphic>";
    out += "</wp:inline></w:drawing></w:r></w:p>";
    return out;
}

static std::vector<uint8_t> str_bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

static bool save_docx_report(const std::string& filename, const PrintOptions& opt) {
    const auto location = g_state.current_climate();

    // ----- Collect images -----
    std::vector<DocxImage> images;
    int next_rid = 10;

    if (opt.include_logo) {
        auto logo_bytes = read_file_next_to_exe(L"image.png");
        if (!logo_bytes.empty()) {
            DocxImage img;
            img.filename  = "image1.png";
            img.data      = std::move(logo_bytes);
            img.width_px  = 250;
            img.height_px = 86;
            img.rel_id    = "rId" + std::to_string(next_rid++);
            images.push_back(std::move(img));
        }
    }

    if (opt.include_mode_plots) {
        auto png = render_panel_png(900, 360,
            [](HDC hdc, RECT r) { draw_mode_shapes(hdc, r); });
        if (!png.empty()) {
            DocxImage img;
            img.filename  = "image" + std::to_string(images.size() + 1) + ".png";
            img.data      = std::move(png);
            img.width_px  = 700;   // displayed size (scaled by EMU)
            img.height_px = 280;
            img.rel_id    = "rId" + std::to_string(next_rid++);
            images.push_back(std::move(img));
        }
    }

    if (opt.include_wind_plot) {
        auto png = render_panel_png(900, 220,
            [](HDC hdc, RECT r) { draw_wind_distribution(hdc, r); });
        if (!png.empty()) {
            DocxImage img;
            img.filename  = "image" + std::to_string(images.size() + 1) + ".png";
            img.data      = std::move(png);
            img.width_px  = 700;
            img.height_px = 170;
            img.rel_id    = "rId" + std::to_string(next_rid++);
            images.push_back(std::move(img));
        }
    }

    // ----- Build document.xml body -----
    std::string body;
    int draw_id = 1;

    // Logo
    if (opt.include_logo && !images.empty()) {
        body += ooxml_inline_image(images[0], draw_id++);
    }

    // Title
    body += ooxml_heading("Vortex Shedding Analysis Report", 1);
    {
        std::time_t now = std::time(nullptr);
        char ts[64];
        std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        body += ooxml_paragraph(std::string("Generated: ") + ts, false, 9, "666666");
    }

    if (opt.include_inputs) {
        body += ooxml_heading("Input Parameters", 2);
        std::vector<std::vector<std::string>> rows = {
            {"Parameter", "Value"},
            {"Material",      g_state.member.material.name},
            {"Young's modulus E (GPa)", fmt(g_state.member.material.E_GPa, 1)},
            {"Density (kg/m^3)",        fmt(g_state.member.material.density_kg_m3, 0)},
            {"Outer diameter D (mm)",   fmt(g_state.D_mm, 1)},
            {"Wall thickness t (mm)",   fmt(g_state.t_mm, 2)},
            {"Length L (m)",            fmt(g_state.L_m, 2)},
            {"Damping ratio (zeta)",    fmt(g_state.damping, 4)},
            {"Axial force (kN)",        fmt(g_state.axial_force_kN, 1)},
            {"Wind climate",            location.location},
        };
        if (location.uniform) {
            rows.push_back({"Wind range (m/s)",
                fmt(location.v_min, 1) + " - " + fmt(location.v_max, 1)});
        } else {
            rows.push_back({"Mean wind (m/s)", fmt(location.mean_speed_ms, 1)});
        }
        body += ooxml_table(rows, {3200, 3200});
    }

    if (opt.include_calculated) {
        body += ooxml_heading("Calculated Section Properties", 2);
        std::vector<std::vector<std::string>> rows = {
            {"Property", "Value"},
            {"Moment of inertia I (mm^4)",
                fmt(g_state.member.I_m4 * 1e12, 0)},
            {"Cross-sectional area A (mm^2)",
                fmt(g_state.member.A_m2 * 1e6, 1)},
            {"Mass per unit length mu (kg/m)",
                fmt(g_state.member.mu_kg_m, 2)},
            {"Radius of gyration r (mm)",
                fmt(g_state.member.r_gyration_mm, 1)},
        };
        body += ooxml_table(rows, {4200, 2200});
    }

    if (opt.include_results_table) {
        body += ooxml_heading("Detailed Results — All Boundary Conditions", 2);
        std::vector<std::vector<std::string>> rows;
        rows.push_back({"Boundary condition", "Mode", "f (Hz)",
                        "V_crit (m/s)", "F (kN)", "sigma_max (MPa)"});
        for (const auto& bc_result : g_state.results) {
            for (const auto& m : bc_result.modes) {
                rows.push_back({
                    bc_result.bc_name,
                    std::to_string(m.mode_number),
                    fmt(m.freq_hz, 2),
                    fmt(m.V_critical_ms, 2),
                    fmt(m.F_static_kN * m.amplification, 3),
                    fmt(m.distribution.max_stress_MPa, 1),
                });
            }
        }
        body += ooxml_table(rows, {2400, 700, 1100, 1300, 1100, 1500});
    }

    if (opt.include_uncertainty) {
        body += ooxml_heading("Damping Uncertainty (zeta 0.005 - 0.020)", 2);
        std::vector<std::vector<std::string>> rows;
        rows.push_back({"Boundary condition", "Mode",
                        "sigma_min (MPa)", "sigma_nom (MPa)", "sigma_max (MPa)"});
        for (size_t i = 0; i < g_state.results.size(); i++) {
            const auto& bc_result = g_state.results[i];
            for (const auto& mode_res : bc_result.modes) {
                auto rng = VortexPhysics::analyze_mode_with_damping_range(
                    g_state.member, BOUNDARY_CONDITIONS[i],
                    mode_res.mode_number, g_state.axial_force_kN);
                rows.push_back({
                    bc_result.bc_name,
                    std::to_string(mode_res.mode_number),
                    fmt(rng.result_max.distribution.max_stress_MPa, 1),
                    fmt(rng.result_nominal.distribution.max_stress_MPa, 1),
                    fmt(rng.result_min.distribution.max_stress_MPa, 1),
                });
            }
        }
        body += ooxml_table(rows, {2800, 700, 1500, 1500, 1500});
    }

    if (opt.include_risk) {
        body += ooxml_heading("Risk Assessment per Boundary Condition", 2);
        std::vector<std::vector<std::string>> rows;
        rows.push_back({"Boundary condition", "Level", "Wind percentile", "Stress / yield"});
        for (size_t i = 0; i < g_state.warnings.size() && i < g_state.results.size(); i++) {
            const auto& w = g_state.warnings[i];
            rows.push_back({
                g_state.results[i].bc_name,
                w.level_name,
                fmt(w.wind_percentile, 0.999) + "%",
                fmt(w.stress_ratio * 100.0, 1) + "%",
            });
        }
        body += ooxml_table(rows, {2800, 1500, 1700, 1700});
    }

    if (opt.include_recommendations) {
        WindData::WarningLevel worst = WindData::SAFE;
        size_t worst_idx = 0;
        for (size_t i = 0; i < g_state.warnings.size(); i++) {
            if (g_state.warnings[i].level > worst) {
                worst = g_state.warnings[i].level;
                worst_idx = i;
            }
        }
        if (!g_state.warnings.empty()) {
            body += ooxml_heading(
                std::string("Engineering Recommendations (worst case: ")
                + g_state.warnings[worst_idx].level_name + ")", 2);
            for (const auto& rec : g_state.warnings[worst_idx].recommendations) {
                body += ooxml_paragraph(std::string("• ") + rec, false, 11);
            }
        }
    }

    // Plot images appear at the end (after all data tables)
    {
        size_t img_idx = (opt.include_logo && !images.empty()) ? 1 : 0;
        if (opt.include_mode_plots && img_idx < images.size()) {
            body += ooxml_heading("Mode Shape Plots", 2);
            body += ooxml_inline_image(images[img_idx++], draw_id++);
        }
        if (opt.include_wind_plot && img_idx < images.size()) {
            body += ooxml_heading("Wind Speed Distribution", 2);
            body += ooxml_inline_image(images[img_idx++], draw_id++);
        }
    }

    // ----- Wrap document.xml -----
    std::string document_xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:document "
        "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
        "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" "
        "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
        "xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
        "<w:body>";
    document_xml += body;
    document_xml += "<w:sectPr><w:pgSz w:w=\"12240\" w:h=\"15840\"/>"
                    "<w:pgMar w:top=\"1080\" w:right=\"1080\" "
                    "w:bottom=\"1080\" w:left=\"1080\" w:header=\"720\" "
                    "w:footer=\"720\" w:gutter=\"0\"/></w:sectPr>";
    document_xml += "</w:body></w:document>";

    // ----- Build [Content_Types].xml -----
    std::string content_types =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Default Extension=\"png\" ContentType=\"image/png\"/>"
        "<Override PartName=\"/word/document.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "</Types>";

    // ----- Build _rels/.rels -----
    std::string root_rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
        "Target=\"word/document.xml\"/>"
        "</Relationships>";

    // ----- Build word/_rels/document.xml.rels -----
    std::string doc_rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
    for (const auto& img : images) {
        doc_rels += "<Relationship Id=\"" + img.rel_id + "\" "
            "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" "
            "Target=\"media/" + img.filename + "\"/>";
    }
    doc_rels += "</Relationships>";

    // ----- Assemble ZIP entries -----
    std::vector<ZipEntry> entries;
    entries.push_back({"[Content_Types].xml",         str_bytes(content_types)});
    entries.push_back({"_rels/.rels",                 str_bytes(root_rels)});
    entries.push_back({"word/document.xml",           str_bytes(document_xml)});
    entries.push_back({"word/_rels/document.xml.rels",str_bytes(doc_rels)});
    for (const auto& img : images) {
        entries.push_back({"word/media/" + img.filename, img.data});
    }

    return write_zip(filename, entries);
}

// ============================================================================
// PRINT-REPORT DIALOG (STAAD-style section checkboxes)
// ============================================================================

static PrintOptions g_print_options;
static bool g_print_dialog_done = false;
static bool g_print_dialog_ok   = false;
static HWND g_print_dialog_hwnd = nullptr;

// Dialog control HWNDs
static HWND g_dlg_chk_logo, g_dlg_chk_inputs, g_dlg_chk_calc;
static HWND g_dlg_chk_results, g_dlg_chk_uncert, g_dlg_chk_risk, g_dlg_chk_recs;
static HWND g_dlg_chk_modes, g_dlg_chk_wind;
static HWND g_dlg_radio_docx, g_dlg_radio_txt;
static HWND g_dlg_btn_ok, g_dlg_btn_cancel;

enum {
    IDC_DLG_BTN_OK     = 8001,
    IDC_DLG_BTN_CANCEL = 8002,
};

static LRESULT CALLBACK PrintDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE hi = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);

            auto mk_label = [&](LPCWSTR text, int x, int y, int w, int h) {
                return CreateWindowW(L"STATIC", text,
                    WS_CHILD | WS_VISIBLE | SS_LEFT,
                    x, y, w, h, hwnd, nullptr, hi, nullptr);
            };
            auto mk_chk = [&](LPCWSTR text, int x, int y, int w, int h, bool checked) {
                HWND b = CreateWindowW(L"BUTTON", text,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                    x, y, w, h, hwnd, nullptr, hi, nullptr);
                SendMessage(b, BM_SETCHECK,
                            checked ? BST_CHECKED : BST_UNCHECKED, 0);
                return b;
            };
            auto mk_radio = [&](LPCWSTR text, int x, int y, int w, int h, DWORD extra) {
                return CreateWindowW(L"BUTTON", text,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | extra,
                    x, y, w, h, hwnd, nullptr, hi, nullptr);
            };
            auto mk_btn = [&](LPCWSTR text, int x, int y, int w, int h, int id) {
                return CreateWindowW(L"BUTTON", text,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    x, y, w, h, hwnd, (HMENU)(intptr_t)id, hi, nullptr);
            };

            int x = 18;
            mk_label(L"Sections to include in report:", x, 12, 360, 18);

            int y = 38, gap = 24;
            g_dlg_chk_logo    = mk_chk(L"Title page with EMS-Tech logo",
                                       x, y, 380, 20, g_print_options.include_logo);    y += gap;
            g_dlg_chk_inputs  = mk_chk(L"Input parameters and member geometry",
                                       x, y, 380, 20, g_print_options.include_inputs);  y += gap;
            g_dlg_chk_calc    = mk_chk(L"Calculated section properties",
                                       x, y, 380, 20, g_print_options.include_calculated); y += gap;
            g_dlg_chk_results = mk_chk(L"Detailed results table (all BCs, both modes)",
                                       x, y, 380, 20, g_print_options.include_results_table); y += gap;
            g_dlg_chk_uncert  = mk_chk(L"Damping uncertainty table",
                                       x, y, 380, 20, g_print_options.include_uncertainty); y += gap;
            g_dlg_chk_risk    = mk_chk(L"Risk assessment per boundary condition",
                                       x, y, 380, 20, g_print_options.include_risk);    y += gap;
            g_dlg_chk_recs    = mk_chk(L"Engineering recommendations",
                                       x, y, 380, 20, g_print_options.include_recommendations); y += gap;
            g_dlg_chk_modes   = mk_chk(L"Mode shape plots (rendered as image)",
                                       x, y, 380, 20, g_print_options.include_mode_plots); y += gap;
            g_dlg_chk_wind    = mk_chk(L"Wind speed distribution plot (rendered as image)",
                                       x, y, 380, 20, g_print_options.include_wind_plot); y += gap + 8;

            mk_label(L"Output format:", x, y, 200, 18); y += 22;
            g_dlg_radio_docx = mk_radio(L"Word document (.docx)",
                                        x, y, 240, 20, WS_GROUP); y += 22;
            g_dlg_radio_txt  = mk_radio(L"Plain text (.txt)",
                                        x, y, 240, 20, 0);        y += 30;
            SendMessage(
                g_print_options.format == 1 ? g_dlg_radio_txt : g_dlg_radio_docx,
                BM_SETCHECK, BST_CHECKED, 0);

            // Buttons (right-aligned)
            int btn_w = 150, btn_h = 28;
            int btn_y = y + 6;
            int dlg_w_client = 440;
            g_dlg_btn_ok     = mk_btn(L"Generate Report...",
                dlg_w_client - 2 * btn_w - 20, btn_y, btn_w, btn_h, IDC_DLG_BTN_OK);
            g_dlg_btn_cancel = mk_btn(L"Cancel",
                dlg_w_client - btn_w - 12, btn_y, btn_w, btn_h, IDC_DLG_BTN_CANCEL);

            // Apply font to all children
            EnumChildWindows(hwnd, [](HWND h, LPARAM lp2) -> BOOL {
                SendMessageW(h, WM_SETFONT, (WPARAM)lp2, TRUE);
                return TRUE;
            }, (LPARAM)g_hFont);
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wp);
            int code = HIWORD(wp);
            if (code == BN_CLICKED && id == IDC_DLG_BTN_OK) {
                g_print_options.include_logo =
                    SendMessage(g_dlg_chk_logo, BM_GETCHECK, 0, 0) == BST_CHECKED;
                g_print_options.include_inputs =
                    SendMessage(g_dlg_chk_inputs, BM_GETCHECK, 0, 0) == BST_CHECKED;
                g_print_options.include_calculated =
                    SendMessage(g_dlg_chk_calc, BM_GETCHECK, 0, 0) == BST_CHECKED;
                g_print_options.include_results_table =
                    SendMessage(g_dlg_chk_results, BM_GETCHECK, 0, 0) == BST_CHECKED;
                g_print_options.include_uncertainty =
                    SendMessage(g_dlg_chk_uncert, BM_GETCHECK, 0, 0) == BST_CHECKED;
                g_print_options.include_risk =
                    SendMessage(g_dlg_chk_risk, BM_GETCHECK, 0, 0) == BST_CHECKED;
                g_print_options.include_recommendations =
                    SendMessage(g_dlg_chk_recs, BM_GETCHECK, 0, 0) == BST_CHECKED;
                g_print_options.include_mode_plots =
                    SendMessage(g_dlg_chk_modes, BM_GETCHECK, 0, 0) == BST_CHECKED;
                g_print_options.include_wind_plot =
                    SendMessage(g_dlg_chk_wind, BM_GETCHECK, 0, 0) == BST_CHECKED;
                g_print_options.format =
                    (SendMessage(g_dlg_radio_txt, BM_GETCHECK, 0, 0) == BST_CHECKED)
                        ? 1 : 0;

                g_print_dialog_ok   = true;
                g_print_dialog_done = true;
                return 0;
            }
            if (code == BN_CLICKED && id == IDC_DLG_BTN_CANCEL) {
                g_print_dialog_ok   = false;
                g_print_dialog_done = true;
                return 0;
            }
            return 0;
        }

        case WM_CLOSE:
            g_print_dialog_ok   = false;
            g_print_dialog_done = true;
            return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// Show the print dialog modally. Returns true on Generate, false on Cancel.
static bool show_print_dialog(HWND parent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = PrintDlgProc;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"TorsorPrintDlg";
        RegisterClassExW(&wc);
        registered = true;
    }

    g_print_dialog_done = false;
    g_print_dialog_ok   = false;

    int dlg_w = 460, dlg_h = 480;
    RECT pr;
    GetWindowRect(parent, &pr);
    int dlg_x = pr.left + ((pr.right - pr.left) - dlg_w) / 2;
    int dlg_y = pr.top  + ((pr.bottom - pr.top) - dlg_h) / 2;
    if (dlg_x < 0) dlg_x = 100;
    if (dlg_y < 0) dlg_y = 80;

    g_print_dialog_hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME,
        L"TorsorPrintDlg", L"Print Report",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        dlg_x, dlg_y, dlg_w, dlg_h,
        parent, nullptr, GetModuleHandle(nullptr), nullptr);

    if (!g_print_dialog_hwnd) return false;

    EnableWindow(parent, FALSE);

    MSG msg;
    while (!g_print_dialog_done && GetMessage(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessage(g_print_dialog_hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    DestroyWindow(g_print_dialog_hwnd);
    g_print_dialog_hwnd = nullptr;

    return g_print_dialog_ok;
}

// ============================================================================
// WINDOW PROCEDURE
// ============================================================================

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            // Fonts
            g_hFont      = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            g_hFontBold  = CreateFontW(15, 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            g_hFontSmall = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            g_hFontTitle = CreateFontW(20, 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

            CreateControls(hwnd);

            g_state.update();
            UpdateInfoText();
            UpdateConditionalFields();
            return 0;
        }

        case WM_SIZE: {
            int w = LOWORD(lp);
            int h = HIWORD(lp);
            LayoutWindow(w, h);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }

        case WM_VSCROLL: {
            SCROLLINFO si = {};
            si.cbSize = sizeof(si);
            si.fMask  = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);

            int new_pos = si.nPos;
            int line_h  = 30;
            switch (LOWORD(wp)) {
                case SB_LINEUP:        new_pos -= line_h;       break;
                case SB_LINEDOWN:      new_pos += line_h;       break;
                case SB_PAGEUP:        new_pos -= (int)si.nPage; break;
                case SB_PAGEDOWN:      new_pos += (int)si.nPage; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: new_pos = si.nTrackPos;   break;
                case SB_TOP:           new_pos = si.nMin;        break;
                case SB_BOTTOM:        new_pos = si.nMax;        break;
            }
            ApplyScroll(new_pos);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            // 3 lines per notch, ~30 px per line.
            int step = -(delta * 90) / WHEEL_DELTA;
            ApplyScroll(g_scroll_y + step);
            return 0;
        }

        case WM_KEYDOWN: {
            switch (wp) {
                case VK_PRIOR: ApplyScroll(g_scroll_y - 200); return 0;  // PageUp
                case VK_NEXT:  ApplyScroll(g_scroll_y + 200); return 0;  // PageDown
                case VK_HOME:  ApplyScroll(0);                return 0;
                case VK_END:   ApplyScroll(g_content_h);      return 0;
            }
            break;
        }

        case WM_COMMAND: {
            int id   = LOWORD(wp);
            int code = HIWORD(wp);

            if (id >= ID_DEDIT && id <= ID_CUSTOM_RHO
                && id != ID_LOCATION
                && code == EN_CHANGE) {
                RefreshState();
                return 0;
            }
            if ((id == ID_MATERIAL || id == ID_LOCATION) && code == CBN_SELCHANGE) {
                RefreshState();
                return 0;
            }
            if (code == BN_CLICKED) {
                if (id == ID_RADIO_UDL) {
                    g_state.active_color_var = ColorVariable::UDL;
                    InvalidateRect(hwnd, &g_rectModes,  FALSE);
                    InvalidateRect(hwnd, &g_rectLegend, FALSE);
                    return 0;
                }
                if (id == ID_RADIO_MOMENT) {
                    g_state.active_color_var = ColorVariable::MOMENT;
                    InvalidateRect(hwnd, &g_rectModes,  FALSE);
                    InvalidateRect(hwnd, &g_rectLegend, FALSE);
                    return 0;
                }
                if (id == ID_RADIO_STRESS) {
                    g_state.active_color_var = ColorVariable::STRESS;
                    InvalidateRect(hwnd, &g_rectModes,  FALSE);
                    InvalidateRect(hwnd, &g_rectLegend, FALSE);
                    return 0;
                }
                if (id == ID_CHK_UNCERT) {
                    g_state.show_uncertainty =
                        (SendMessage(g_hUncertaintyCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    InvalidateRect(hwnd, &g_rectModes, FALSE);
                    return 0;
                }
                if (id == ID_BTN_SAVE) {
                    // Open the STAAD-style print dialog with section checkboxes
                    if (!show_print_dialog(hwnd)) {
                        SetWindowTextW(g_hStatusText, L"Print cancelled.");
                        return 0;
                    }

                    bool want_docx = (g_print_options.format == 0);
                    wchar_t filename[MAX_PATH] = {};
                    {
                        std::time_t now = std::time(nullptr);
                        std::wcsftime(filename, MAX_PATH,
                            want_docx ? L"vortex_report_%Y%m%d_%H%M%S.docx"
                                      : L"vortex_report_%Y%m%d_%H%M%S.txt",
                            std::localtime(&now));
                    }
                    OPENFILENAMEW ofn = {};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner   = hwnd;
                    ofn.lpstrFilter = want_docx
                        ? L"Word Document (*.docx)\0*.docx\0All Files\0*.*\0\0"
                        : L"Text Reports (*.txt)\0*.txt\0All Files\0*.*\0\0";
                    ofn.lpstrFile   = filename;
                    ofn.nMaxFile    = MAX_PATH;
                    ofn.lpstrDefExt = want_docx ? L"docx" : L"txt";
                    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

                    if (GetSaveFileNameW(&ofn)) {
                        bool ok = want_docx
                            ? save_docx_report(from_wide(filename), g_print_options)
                            : save_text_report(from_wide(filename), g_print_options);
                        if (ok) {
                            std::wstring m = L"Report saved: ";
                            m += filename;
                            SetWindowTextW(g_hStatusText, m.c_str());
                        } else {
                            SetWindowTextW(g_hStatusText, L"Failed to save report.");
                        }
                    } else {
                        SetWindowTextW(g_hStatusText, L"Save cancelled.");
                    }
                    return 0;
                }
                if (id == ID_BTN_QUIT) {
                    DestroyWindow(hwnd);
                    return 0;
                }
            }
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wp;
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }

        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wp;
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH bg = (HBRUSH)GetSysColorBrush(COLOR_BTNFACE);
            FillRect(hdc, &rc, bg);
            return 1;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT cr;
            GetClientRect(hwnd, &cr);

            // Double-buffered draw to eliminate flicker
            HDC     mem  = CreateCompatibleDC(hdc);
            HBITMAP bmp  = CreateCompatibleBitmap(hdc, cr.right, cr.bottom);
            HGDIOBJ ob   = SelectObject(mem, bmp);

            HBRUSH bg = (HBRUSH)GetSysColorBrush(COLOR_BTNFACE);
            FillRect(mem, &cr, bg);

            draw_mode_shapes      (mem, g_rectModes);
            draw_color_legend     (mem, g_rectLegend);
            draw_wind_distribution(mem, g_rectWind);
            draw_warning_banner   (mem, g_rectWarning);

            BitBlt(hdc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);

            SelectObject(mem, ob);
            DeleteObject(bmp);
            DeleteDC(mem);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            if (g_hFont)      DeleteObject(g_hFont);
            if (g_hFontBold)  DeleteObject(g_hFontBold);
            if (g_hFontSmall) DeleteObject(g_hFontSmall);
            if (g_hFontTitle) DeleteObject(g_hFontTitle);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// ============================================================================
// WinMain
// ============================================================================

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    // GDI+ for plot rendering and PNG encoding (DOCX export)
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    // OLE for IStream-based PNG encoding
    OleInitialize(nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hi;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"TorsorMain";
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm       = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    int win_w = 1100;
    int win_h = 980;

    g_hwnd = CreateWindowExW(0, L"TorsorMain",
        L"Torsor v0.2 - Vortex Shedding Calculator",
        WS_OVERLAPPEDWINDOW | WS_VSCROLL,
        CW_USEDEFAULT, CW_USEDEFAULT, win_w, win_h,
        nullptr, nullptr, hi, nullptr);

    if (!g_hwnd) return 1;

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        // IsDialogMessage gives us Tab navigation between controls
        if (!IsDialogMessage(g_hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    OleUninitialize();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return (int)msg.wParam;
}
