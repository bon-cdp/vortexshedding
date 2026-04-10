// Torsor v0.1: Vortex Shedding Calculator
// Terminal-based interactive tool for analyzing vortex-induced vibration
// in HSS round members
//
// Compile: clang++ -std=c++20 -O3 vortex.cpp -o vortex \
//          -I/opt/homebrew/opt/ftxui/include -I/opt/homebrew/opt/libharu/include \
//          -L/opt/homebrew/opt/ftxui/lib -L/opt/homebrew/opt/libharu/lib \
//          -lftxui-component -lftxui-dom -lftxui-screen -lhpdf

#include "vortex_physics.h"
#include "vortex_report.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include <iostream>
#include <sstream>
#include <iomanip>

using namespace ftxui;

// ============================================================================
// COLOR MAPPING SYSTEM
// ============================================================================

enum class ColorVariable {
    UDL,      // Uniform distributed load (kN/m)
    MOMENT,   // Bending moment (kN·m)
    STRESS    // Bending stress (MPa)
};

// Map normalized value [0,1] to color (Blue → Green → Yellow → Red)
inline Color value_to_color(double normalized) {
    if (normalized < 0.2) return Color::Blue;
    else if (normalized < 0.4) return Color::Cyan;
    else if (normalized < 0.6) return Color::Green;
    else if (normalized < 0.8) return Color::Yellow;
    else return Color::Red;
}

// Get value at normalized position x along beam for given color variable
inline double get_value_at_position(const ModeResult& mode, double x_norm, ColorVariable var) {
    if (mode.distribution.x_m.empty()) return 0.0;

    size_t idx = static_cast<size_t>(x_norm * (mode.distribution.x_m.size() - 1));
    if (idx >= mode.distribution.x_m.size()) idx = mode.distribution.x_m.size() - 1;

    switch (var) {
        case ColorVariable::UDL:
            return mode.distribution.udl_kN_m;  // Constant along beam
        case ColorVariable::MOMENT:
            return mode.distribution.moment_kNm[idx];
        case ColorVariable::STRESS:
            return mode.distribution.stress_MPa[idx];
        default:
            return 0.0;
    }
}

// Get maximum value for color variable across all modes
inline double get_max_value(const std::vector<ModeResult>& modes, ColorVariable var) {
    double max_val = 0.0;
    for (const auto& mode : modes) {
        switch (var) {
            case ColorVariable::UDL:
                max_val = std::max(max_val, mode.distribution.udl_kN_m);
                break;
            case ColorVariable::MOMENT:
                max_val = std::max(max_val, mode.distribution.max_moment_kNm);
                break;
            case ColorVariable::STRESS:
                max_val = std::max(max_val, mode.distribution.max_stress_MPa);
                break;
        }
    }
    return max_val;
}

// ============================================================================
// APPLICATION STATE
// ============================================================================

struct AppState {
    // Input parameters (METRIC UNITS)
    int material_index = 0;
    double D_mm = 200.0;         // Outer diameter (mm)
    double t_mm = 6.0;           // Wall thickness (mm)
    double L_m = 6.0;            // Length (m)
    double damping = 0.01;       // 1% damping (typical for steel)
    double axial_force_kN = 0.0; // Axial force (kN, positive = tension)

    // Custom material (if selected)
    double custom_E_GPa = 200.0;       // Young's modulus (GPa)
    double custom_rho = 7850.0;        // Density (kg/m³)

    // Wind climate selection
    int location_index = 14;          // Default to "Moderate Wind (Generic)"

    // UI state
    int active_input = 0;             // Which input field is active (0-6 now)
    ColorVariable active_color_var = ColorVariable::STRESS;  // Start with stress
    bool show_uncertainty = true;     // Show damping uncertainty ranges

    // Results
    HSS_Member member;
    std::vector<VortexResults> results;
    std::vector<WindData::VortexWarning> warnings;  // Warning for each BC

    // Update member and recalculate
    void update() {
        // Set material
        if (static_cast<size_t>(material_index) < MATERIALS.size() - 1) {
            member.material = MATERIALS[material_index];
        } else {
            member.material = {"Custom", custom_E_GPa, custom_rho};
        }

        // Set geometry (metric)
        member.D_mm = D_mm;
        member.t_mm = t_mm;
        member.L_m = L_m;
        member.damping_ratio = damping;

        // Calculate derived properties
        member.calculate_properties();

        // Run vortex analysis with axial force
        results = VortexPhysics::analyze_member(member, 2, axial_force_kN);

        // Calculate warnings for each BC (use Mode 1 for warning assessment)
        warnings.clear();
        const auto& location = WindData::PORT_CLIMATES[location_index];
        for (const auto& bc_result : results) {
            if (!bc_result.modes.empty()) {
                const auto& mode1 = bc_result.modes[0];
                auto warning = WindData::assess_risk(
                    mode1.V_critical_ms,
                    mode1.distribution.max_stress_MPa,
                    member.material.name == "A36 Steel" ? 250.0 : 345.0,  // Yield strength (MPa)
                    member.L_m,
                    member.D_mm,
                    location
                );
                warnings.push_back(warning);
            }
        }
    }
};

// ============================================================================
// UI COMPONENTS
// ============================================================================

// Top panel: Logo and equations
Element render_header() {
    return vbox({
        text("╔═══════════════════════════════════════════════════════════════╗") | color(Color::Cyan),
        text("║                      Torsor  \\|/                              ║") | color(Color::Cyan) | bold,
        text("║              Vortex Shedding Calculator v0.2                  ║") | color(Color::Cyan),
        text("╚═══════════════════════════════════════════════════════════════╝") | color(Color::Cyan),
        text(""),
        hbox({
            text("ω = A√(EI/μL⁴)  ") | color(Color::Yellow),
            text("│") | color(Color::GrayDark),
            text("  V = f(ω,D,S)  ") | color(Color::Yellow),
            text("│") | color(Color::GrayDark),
            text("  F = C₁/(√(L/D)·√(ζ-C₂ρD²/M))·qₕ·D") | color(Color::Yellow)
        }) | center,
        separator()
    });
}

// Warning banner (always red - see wind distribution for details)
Element render_warning_banner(const AppState& state) {
    if (state.warnings.empty()) {
        return text("");
    }

    // Check if there are any non-SAFE warnings
    bool has_risk = false;
    WindData::WarningLevel worst_level = WindData::SAFE;
    for (size_t i = 0; i < state.warnings.size(); i++) {
        if (state.warnings[i].level > WindData::SAFE) {
            has_risk = true;
            if (state.warnings[i].level > worst_level) {
                worst_level = state.warnings[i].level;
            }
        }
    }

    // Only show banner if there's actual risk detected
    if (!has_risk) {
        return text("");
    }

    const auto& location = WindData::PORT_CLIMATES[state.location_index];

    // Create banner message - always red, directs to wind distribution
    std::ostringstream banner_text;
    banner_text << "⚠ VORTEX SHEDDING RISK DETECTED @ " << location.location
                << " — See Wind Distribution below for critical cases";

    return vbox({
        text(banner_text.str()) | color(Color::Red) | bold | center | border,
        text("")
    });
}

// Render a single mode shape curve using Braille characters
Element render_mode_shape(const std::string& bc_name, int mode, double max_deflection,
                         int width, int height, Color curve_color) {
    auto canvas = Canvas(width * 2, height * 4);  // Braille gives 2x4 resolution

    // Draw original member line (horizontal)
    int baseline_y = height * 4 / 2;
    for (int x = 0; x < width * 2; x++) {
        canvas.DrawPointLine(x, baseline_y, x, baseline_y, Color::GrayDark);
    }

    // Draw mode shape
    for (int x = 0; x < width * 2 - 1; x++) {
        double x_norm = static_cast<double>(x) / (width * 2);
        double x_next = static_cast<double>(x + 1) / (width * 2);

        double y1 = VortexPhysics::modal_shape(x_norm, 1.0, bc_name, mode);
        double y2 = VortexPhysics::modal_shape(x_next, 1.0, bc_name, mode);

        // Scale to canvas height (±height/4 from baseline)
        int canvas_y1 = baseline_y - static_cast<int>(y1 * height);
        int canvas_y2 = baseline_y - static_cast<int>(y2 * height);

        canvas.DrawPointLine(x, canvas_y1, x + 1, canvas_y2, curve_color);
    }

    return ftxui::canvas(std::move(canvas));
}

// Middle panel: Modal shapes with auto-scaling and color mapping
Element render_visualizations(const AppState& state) {
    if (state.results.empty()) {
        return text("Calculating...") | center;
    }

    std::vector<Element> bc_panels;

    // Get max value for color mapping across ALL boundary conditions
    double global_max_value = 0.0;
    for (const auto& result : state.results) {
        double bc_max = get_max_value(result.modes, state.active_color_var);
        global_max_value = std::max(global_max_value, bc_max);
    }

    for (const auto& result : state.results) {
        std::vector<Element> mode_shapes;

        // Title
        mode_shapes.push_back(text(result.bc_name) | bold | color(Color::Cyan) | center);

        // Calculate auto-scaling factor for this BC
        double max_deflection = 0.0;
        for (const auto& mode_res : result.modes) {
            for (int x = 0; x <= 100; x++) {
                double x_norm = x / 100.0;
                double deflection = VortexPhysics::modal_shape(x_norm, 1.0, result.bc_name, mode_res.mode_number);
                max_deflection = std::max(max_deflection, std::abs(deflection));
            }
        }

        // Canvas setup
        constexpr int canvas_width = 30;
        constexpr int canvas_height = 10;
        auto combined_canvas = Canvas(canvas_width * 2, canvas_height * 4);
        int baseline_y = canvas_height * 4 / 2;

        // Scale factor to fit modes in canvas (use 80% of height)
        double scale_factor = max_deflection > 0 ? (canvas_height * 4 * 0.4) / max_deflection : 1.0;

        // Draw baseline
        for (int x = 0; x < canvas_width * 2; x++) {
            combined_canvas.DrawPointLine(x, baseline_y, x, baseline_y, Color::GrayDark);
        }

        // Draw modes with color mapping
        for (const auto& mode_res : result.modes) {
            int mode = mode_res.mode_number;

            for (int x = 0; x < canvas_width * 2 - 1; x++) {
                double x_norm = static_cast<double>(x) / (canvas_width * 2);
                double x_next = static_cast<double>(x + 1) / (canvas_width * 2);

                // Get modal deflections
                double y1 = VortexPhysics::modal_shape(x_norm, 1.0, result.bc_name, mode);
                double y2 = VortexPhysics::modal_shape(x_next, 1.0, result.bc_name, mode);

                // Scale to canvas
                int canvas_y1 = baseline_y - static_cast<int>(y1 * scale_factor);
                int canvas_y2 = baseline_y - static_cast<int>(y2 * scale_factor);

                // Get color based on value at this position
                double value = get_value_at_position(mode_res, x_norm, state.active_color_var);
                double normalized = global_max_value > 0 ? value / global_max_value : 0.0;
                Color line_color = value_to_color(normalized);

                // Draw colored line
                combined_canvas.DrawPointLine(x, canvas_y1, x + 1, canvas_y2, line_color);
            }
        }

        mode_shapes.push_back(ftxui::canvas(std::move(combined_canvas)));

        // Mode info with max deflection and scale
        std::ostringstream scale_info;
        scale_info << "Max: " << std::fixed << std::setprecision(4) << max_deflection
                   << " | Scale: " << std::fixed << std::setprecision(1) << scale_factor;
        mode_shapes.push_back(text(scale_info.str()));  // Default color

        // Mode legends with intermediate values (split into multiple lines for readability)
        for (const auto& mode_res : result.modes) {
            // Line 1: Basic frequency and wind speed
            std::ostringstream oss1;
            oss1 << "Mode " << mode_res.mode_number << ": "
                 << "f=" << std::fixed << std::setprecision(2) << mode_res.freq_hz << " Hz"
                 << " | ω=" << std::fixed << std::setprecision(2) << mode_res.omega << " rad/s"
                 << " | V=" << std::fixed << std::setprecision(3) << mode_res.V_critical_ms << " m/s";
            mode_shapes.push_back(text(oss1.str()));  // Default color

            // Line 2: Force, stress, and Strouhal number (with optional uncertainty)
            std::ostringstream oss2;
            oss2 << "  F=" << std::fixed << std::setprecision(4) << mode_res.F_static_kN << " kN"
                 << " | σ_max=" << std::fixed << std::setprecision(2) << mode_res.distribution.max_stress_MPa << " MPa";

            // Add damping uncertainty range if enabled
            if (state.show_uncertainty) {
                // Calculate damping range for this mode
                size_t bc_index = &result - &state.results[0];  // Get BC index
                auto damping_range = VortexPhysics::analyze_mode_with_damping_range(
                    state.member,
                    BOUNDARY_CONDITIONS[bc_index],
                    mode_res.mode_number,
                    state.axial_force_kN
                );

                double stress_min = damping_range.result_max.distribution.max_stress_MPa;  // Max damping → min stress
                double stress_max = damping_range.result_min.distribution.max_stress_MPa;  // Min damping → max stress

                oss2 << " [" << std::fixed << std::setprecision(0) << stress_min
                     << "-" << std::fixed << std::setprecision(0) << stress_max << "]";
            }

            oss2 << " | S=" << std::fixed << std::setprecision(3) << mode_res.strouhal;
            mode_shapes.push_back(text(oss2.str()));  // Default color

            // Line 3: Amplification and damping parameters
            std::ostringstream oss3;
            oss3 << "  H=" << std::fixed << std::setprecision(1) << mode_res.amplification
                 << " | ωD²=" << std::fixed << std::setprecision(4) << mode_res.omega_D2 << " m²rad/s"
                 << " | ζ_eff=" << std::fixed << std::setprecision(5) << mode_res.damping_param;
            mode_shapes.push_back(text(oss3.str()));  // Default color
        }

        bc_panels.push_back(vbox(mode_shapes) | border);
    }

    // Arrange BC panels in 2 rows of 3 for better visibility
    std::vector<Element> top_row, bottom_row;
    for (size_t i = 0; i < bc_panels.size(); i++) {
        if (i < 3) {
            top_row.push_back(bc_panels[i]);
        } else {
            bottom_row.push_back(bc_panels[i]);
        }
    }

    return vbox({
        hbox(top_row) | center,
        hbox(bottom_row) | center
    });
}

// Color legend panel
Element render_color_legend(const AppState& state) {
    // Get variable name and units
    std::string var_name, units;
    double max_value = 0.0;

    // Get max across all results
    for (const auto& result : state.results) {
        double bc_max = get_max_value(result.modes, state.active_color_var);
        max_value = std::max(max_value, bc_max);
    }

    switch (state.active_color_var) {
        case ColorVariable::UDL:
            var_name = "UDL";
            units = "kN/m";
            break;
        case ColorVariable::MOMENT:
            var_name = "MOMENT";
            units = "kN·m";
            break;
        case ColorVariable::STRESS:
            var_name = "STRESS";
            units = "MPa";
            break;
    }

    // Create gradient bar
    std::vector<Element> gradient;
    for (int i = 0; i < 20; i++) {
        double norm = static_cast<double>(i) / 19.0;
        Color c = value_to_color(norm);
        gradient.push_back(text("█") | color(c));
    }

    // Format max value
    std::ostringstream max_str;
    max_str << std::fixed << std::setprecision(3) << max_value;

    return vbox({
        hbox({
            text("Showing: ") | color(Color::GrayLight),
            text(var_name) | bold | color(Color::Yellow),
            text(" (" + units + ")") | color(Color::GrayLight),
            text("  [Space to cycle]") | color(Color::GrayDark),
            text("  │  ") | color(Color::GrayDark),
            text("MAX = ") | color(Color::GrayLight),
            text(max_str.str() + " " + units) | bold | color(Color::Red)
        }) | center,
        hbox(gradient) | center,
        hbox({
            text("0.0") | color(Color::Blue),
            text(" ───────────── ") | color(Color::GrayDark),
            text(max_str.str()) | color(Color::Red)
        }) | center
    }) | border;
}

// Wind speed distribution panel
Element render_wind_distribution(const AppState& state) {
    const auto& location = WindData::PORT_CLIMATES[state.location_index];

    // Create histogram canvas
    constexpr int canvas_width = 60;
    constexpr int canvas_height = 8;
    auto canvas = Canvas(canvas_width * 2, canvas_height * 4);

    // Determine wind speed range (0 to 99th percentile + 20%)
    double max_wind = location.percentile_99 * 1.2;
    double wind_step = max_wind / (canvas_width * 2);

    // Draw Weibull distribution curve
    std::vector<double> pdf_values;
    double max_pdf = 0.0;

    for (int x = 0; x < canvas_width * 2; x++) {
        double v = x * wind_step;
        if (v <= 0.0) {
            pdf_values.push_back(0.0);
            continue;
        }

        // Weibull PDF: f(v) = (k/c) * (v/c)^(k-1) * exp(-(v/c)^k)
        double k = location.weibull_k;
        double c = location.weibull_c;
        double pdf = (k / c) * std::pow(v / c, k - 1) * std::exp(-std::pow(v / c, k));
        pdf_values.push_back(pdf);
        max_pdf = std::max(max_pdf, pdf);
    }

    // Normalize and draw distribution curve
    for (int x = 0; x < canvas_width * 2 - 1; x++) {
        double y1_norm = pdf_values[x] / max_pdf;
        double y2_norm = pdf_values[x + 1] / max_pdf;

        int y1 = static_cast<int>(y1_norm * canvas_height * 4);
        int y2 = static_cast<int>(y2_norm * canvas_height * 4);

        canvas.DrawPointLine(x, canvas_height * 4 - y1, x + 1, canvas_height * 4 - y2, Color::Cyan);
    }

    // Draw percentile lines
    auto draw_percentile_line = [&](double percentile_value, Color color) {
        int x_pos = static_cast<int>((percentile_value / max_wind) * canvas_width * 2);
        if (x_pos >= 0 && x_pos < canvas_width * 2) {
            for (int y = 0; y < canvas_height * 4; y += 2) {
                canvas.DrawPoint(x_pos, y, true, color);
            }
        }
    };

    draw_percentile_line(location.percentile_50, Color::Green);
    draw_percentile_line(location.percentile_75, Color::Yellow);
    draw_percentile_line(location.percentile_90, Color::Red);

    // Mark critical wind speeds from current results
    std::vector<Element> critical_labels;
    if (!state.results.empty() && !state.results[0].modes.empty()) {
        for (size_t i = 0; i < state.results.size(); i++) {
            const auto& bc_result = state.results[i];
            if (!bc_result.modes.empty()) {
                double V_crit = bc_result.modes[0].V_critical_ms;
                int x_pos = static_cast<int>((V_crit / max_wind) * canvas_width * 2);
                if (x_pos >= 0 && x_pos < canvas_width * 2) {
                    for (int y = 0; y < canvas_height * 4; y++) {
                        canvas.DrawPoint(x_pos, y, true, Color::Magenta);
                    }

                    // Add label for this critical case if it's risky
                    if (i < state.warnings.size() && state.warnings[i].level > WindData::SAFE) {
                        const auto& warning = state.warnings[i];

                        // Shorten BC name for display
                        std::string bc_short = bc_result.bc_name;
                        if (bc_short.find("Fixed-Fixed") != std::string::npos) bc_short = "Fix-Fix";
                        else if (bc_short.find("Fixed-Hinged") != std::string::npos) bc_short = "Fix-Hng";
                        else if (bc_short.find("Hinged-Hinged") != std::string::npos) bc_short = "Hng-Hng";
                        else if (bc_short.find("Fixed-Free") != std::string::npos) bc_short = "Cantilever";
                        else if (bc_short.find("Hinged-Free") != std::string::npos) bc_short = "Hng-Free";
                        else if (bc_short.find("Free-Free") != std::string::npos) bc_short = "Free-Free";

                        std::ostringstream label;
                        label << bc_short << ": " << std::fixed << std::setprecision(1)
                              << V_crit << "m/s [" << warning.level_name << "]";

                        // Color based on risk level
                        Color label_color;
                        switch (warning.level) {
                            case WindData::CRITICAL: label_color = Color::RedLight; break;
                            case WindData::HIGH_RISK: label_color = Color::Red; break;
                            case WindData::WARNING: label_color = Color::Yellow; break;
                            default: label_color = Color::Cyan; break;
                        }

                        critical_labels.push_back(
                            text(label.str()) | color(label_color)
                        );
                    }
                }
            }
        }
    }

    std::ostringstream legend;
    legend << "Wind Distribution: " << location.location << " | "
           << "Mean=" << std::fixed << std::setprecision(1) << location.mean_speed_ms << " m/s | "
           << "50%=" << location.percentile_50 << " | "
           << "75%=" << location.percentile_75 << " | "
           << "90%=" << location.percentile_90 << " m/s";

    std::vector<Element> content = {
        text("═══ WIND SPEED DISTRIBUTION (Weibull) ═══") | color(Color::Cyan) | center,
        ftxui::canvas(std::move(canvas)),
        text(legend.str()) | center,
    };

    // Add critical case labels if any - show compactly in one line
    if (!critical_labels.empty()) {
        std::vector<Element> critical_hbox;
        critical_hbox.push_back(text("⚠ CRITICAL: ") | color(Color::Red) | bold);
        for (size_t i = 0; i < critical_labels.size(); i++) {
            critical_hbox.push_back(critical_labels[i]);
            if (i < critical_labels.size() - 1) {
                critical_hbox.push_back(text(" | "));
            }
        }
        content.push_back(hbox(critical_hbox) | center);
    }

    content.push_back(hbox({
        text(" ▌ Magenta=Critical V ") | color(Color::Magenta),
        text("│"),
        text(" ▌ Green=50% ") | color(Color::Green),
        text("│"),
        text(" ▌ Yellow=75% ") | color(Color::Yellow),
        text("│"),
        text(" ▌ Red=90% ") | color(Color::Red)
    }) | center);

    return vbox(content) | border;
}

// Bottom panel: Inputs and outputs
Element render_inputs(const AppState& state) {
    auto material_name = (static_cast<size_t>(state.material_index) < MATERIALS.size())
                        ? MATERIALS[state.material_index].name
                        : "Custom";

    const auto& location = WindData::PORT_CLIMATES[state.location_index];
    std::string location_name = location.location;
    if (location_name.length() > 25) {
        location_name = location_name.substr(0, 22) + "...";
    }

    std::ostringstream oss_D, oss_t, oss_L, oss_damp, oss_axial;
    oss_D << std::fixed << std::setprecision(1) << state.D_mm;
    oss_t << std::fixed << std::setprecision(2) << state.t_mm;
    oss_L << std::fixed << std::setprecision(1) << state.L_m;
    oss_damp << std::fixed << std::setprecision(4) << state.damping;
    oss_axial << std::fixed << std::setprecision(1) << state.axial_force_kN;

    auto highlight_if_active = [&](int index, const std::string& label, const std::string& value) {
        auto elem = hbox({
            text(label + ": ") | color(Color::Yellow),
            text(value)  // Default color (black on white terminal)
        });
        if (state.active_input == index) {
            return elem | bold | bgcolor(Color::Cyan);  // Light cyan background - readable with black text
        }
        return elem;
    };

    // Damping guidance based on current value
    std::string damping_note;
    if (state.damping < 0.007) {
        damping_note = "  (Bare steel - conservative)";
    } else if (state.damping < 0.013) {
        damping_note = "  (Typical for HSS members)";
    } else {
        damping_note = "  (With attachments/bolted connections)";
    }

    return vbox({
        text("═══ INPUTS (Tab to cycle, ↑↓ to adjust, u=uncertainty) ═══") | color(Color::Cyan) | center,
        text(""),
        hbox({
            vbox({
                highlight_if_active(0, "Material", material_name),
                highlight_if_active(1, "Diameter (D)", oss_D.str() + " mm"),
                highlight_if_active(2, "Thickness (t)", oss_t.str() + " mm"),
                highlight_if_active(3, "Length (L)", oss_L.str() + " m"),
            }) | flex,
            separator(),
            vbox({
                highlight_if_active(4, "Damping (ζ)", oss_damp.str() + damping_note),
                highlight_if_active(5, "Axial Force (F)", oss_axial.str() + " kN"),
                highlight_if_active(6, "Location", location_name),
                text("  Wind: " + std::to_string((int)location.mean_speed_ms) + " m/s avg"),
            }) | flex,
            separator(),
            vbox({
                text("Calculated:"),
                [&]() {
                    std::ostringstream oss;
                    oss << "  I = " << std::fixed << std::setprecision(0) << (state.member.I_m4 * 1e12) << " mm⁴";
                    return text(oss.str());
                }(),
                [&]() {
                    std::ostringstream oss;
                    oss << "  A = " << std::fixed << std::setprecision(1) << (state.member.A_m2 * 1e6) << " mm²";
                    return text(oss.str());
                }(),
                [&]() {
                    std::ostringstream oss;
                    oss << "  μ = " << std::fixed << std::setprecision(2) << state.member.mu_kg_m << " kg/m";
                    return text(oss.str());
                }(),
            }) | flex
        }),
        separator(),
        text("q=quit  Space=color  Tab=input  ↑↓=adjust  u=toggle uncertainty  p=PDF") | color(Color::GrayDark) | center
    }) | border;
}

// ============================================================================
// MAIN APPLICATION
// ============================================================================

int main() {
    auto screen = ScreenInteractive::Fullscreen();
    AppState state;

    // Initial calculation
    state.update();

    // Component for capturing keyboard input
    auto component = Renderer([&] {
        return vbox({
            render_header(),
            separator(),
            render_visualizations(state) | flex,
            separator(),
            render_color_legend(state),
            separator(),
            render_wind_distribution(state),  // Wind speed distribution
            separator(),
            render_inputs(state)
        });
    });

    // Event handler
    component = CatchEvent(component, [&](Event event) {
        // Quit
        if (event == Event::Character('q') || event == Event::Character('Q')) {
            screen.ExitLoopClosure()();
            return true;
        }

        // Tab: cycle active input
        if (event == Event::Tab) {
            state.active_input = (state.active_input + 1) % 7;  // 7 inputs now (0-6)
            return true;
        }

        // Space: cycle color variable (UDL → Moment → Stress)
        if (event == Event::Character(' ')) {
            switch (state.active_color_var) {
                case ColorVariable::UDL:
                    state.active_color_var = ColorVariable::MOMENT;
                    break;
                case ColorVariable::MOMENT:
                    state.active_color_var = ColorVariable::STRESS;
                    break;
                case ColorVariable::STRESS:
                    state.active_color_var = ColorVariable::UDL;
                    break;
            }
            return true;
        }

        // 'u' or 'U': toggle uncertainty display
        if (event == Event::Character('u') || event == Event::Character('U')) {
            state.show_uncertainty = !state.show_uncertainty;
            return true;
        }

        // 'p' or 'P': generate PDF report
        if (event == Event::Character('p') || event == Event::Character('P')) {
            const auto& location = WindData::PORT_CLIMATES[state.location_index];
            bool success = TorsorReport::generate_report(
                state.member,
                state.results,
                state.warnings,
                location,
                state.axial_force_kN
            );

            // Note: We can't easily show a message in the current FTXUI setup
            // The file will be saved in the current directory
            // Future enhancement: add a status message area
            (void)success;  // Suppress unused variable warning
            return true;
        }

        // Arrow keys: adjust values
        bool changed = false;

        if (event == Event::ArrowUp) {
            switch (state.active_input) {
                case 0: state.material_index = (state.material_index + 1) % MATERIALS.size(); break;
                case 1: state.D_mm += 10.0; break;     // Increment by 10mm
                case 2: state.t_mm += 0.5; break;      // Increment by 0.5mm
                case 3: state.L_m += 0.5; break;       // Increment by 0.5m
                case 4: state.damping += 0.001; break;
                case 5: state.axial_force_kN += 10.0; break;  // Increment by 10kN
                case 6: state.location_index = (state.location_index + 1) % WindData::PORT_CLIMATES.size(); break;
            }
            changed = true;
        }

        if (event == Event::ArrowDown) {
            switch (state.active_input) {
                case 0: state.material_index = (state.material_index - 1 + MATERIALS.size()) % MATERIALS.size(); break;
                case 1: if (state.D_mm > 20.0) state.D_mm -= 10.0; break;
                case 2: if (state.t_mm > 1.0) state.t_mm -= 0.5; break;
                case 3: if (state.L_m > 1.0) state.L_m -= 0.5; break;
                case 4: if (state.damping > 0.001) state.damping -= 0.001; break;
                case 5: state.axial_force_kN -= 10.0; break;  // Can be negative (compression)
                case 6: state.location_index = (state.location_index - 1 + WindData::PORT_CLIMATES.size()) % WindData::PORT_CLIMATES.size(); break;
            }
            changed = true;
        }

        // Recalculate if inputs changed
        if (changed) {
            state.update();
            return true;
        }

        return false;
    });

    screen.Loop(component);

    std::cout << "\nThank you for using Torsor!\n";
    std::cout << "Keyboard-driven engineering tools for the modern era.\n\n";

    return 0;
}
