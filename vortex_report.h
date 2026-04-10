// Torsor v0.2 - PDF Report Generation
// Professional vortex shedding analysis reports using libHaru

#pragma once

#include <hpdf.h>
#include "vortex_physics.h"
#include "wind_data.h"
#include <ctime>
#include <sstream>
#include <iomanip>

namespace TorsorReport {

// Page layout constants (Letter size: 8.5" × 11")
constexpr float PAGE_WIDTH = 612.0f;   // 8.5" × 72 pts/inch
constexpr float PAGE_HEIGHT = 792.0f;  // 11" × 72 pts/inch
constexpr float MARGIN = 50.0f;
constexpr float CONTENT_WIDTH = PAGE_WIDTH - 2 * MARGIN;

// Generate timestamp for report
inline std::string get_timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm* ltm = std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(ltm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// Generate filename with timestamp
inline std::string generate_filename() {
    std::time_t now = std::time(nullptr);
    std::tm* ltm = std::localtime(&now);
    std::ostringstream oss;
    oss << "vortex_report_" << std::put_time(ltm, "%Y%m%d_%H%M%S") << ".pdf";
    return oss.str();
}

// Draw header on page
inline void draw_header(HPDF_Page page, HPDF_Font font_bold, HPDF_Font font_normal) {
    HPDF_Page_SetFontAndSize(page, font_bold, 20);
    HPDF_Page_BeginText(page);
    HPDF_Page_TextOut(page, PAGE_WIDTH / 2 - 150, PAGE_HEIGHT - MARGIN, "Torsor Vortex Shedding Analysis");
    HPDF_Page_EndText(page);

    // Draw line under header
    HPDF_Page_SetLineWidth(page, 1.0);
    HPDF_Page_MoveTo(page, MARGIN, PAGE_HEIGHT - MARGIN - 15);
    HPDF_Page_LineTo(page, PAGE_WIDTH - MARGIN, PAGE_HEIGHT - MARGIN - 15);
    HPDF_Page_Stroke(page);
}

// Draw section header
inline float draw_section_header(HPDF_Page page, HPDF_Font font_bold, float y_pos, const char* title) {
    HPDF_Page_SetFontAndSize(page, font_bold, 14);
    HPDF_Page_BeginText(page);
    HPDF_Page_TextOut(page, MARGIN, y_pos, title);
    HPDF_Page_EndText(page);

    // Underline
    HPDF_Page_SetLineWidth(page, 0.5);
    HPDF_Page_MoveTo(page, MARGIN, y_pos - 3);
    HPDF_Page_LineTo(page, PAGE_WIDTH - MARGIN, y_pos - 3);
    HPDF_Page_Stroke(page);

    return y_pos - 25;  // Return next line position
}

// Draw text line
inline float draw_text(HPDF_Page page, HPDF_Font font, float y_pos, const char* text, float size = 10) {
    HPDF_Page_SetFontAndSize(page, font, size);
    HPDF_Page_BeginText(page);
    HPDF_Page_TextOut(page, MARGIN, y_pos, text);
    HPDF_Page_EndText(page);
    return y_pos - (size + 4);  // Line spacing
}

// Draw warning box
inline float draw_warning_box(HPDF_Page page, HPDF_Font font_bold, HPDF_Font font_normal,
                             float y_pos, const WindData::VortexWarning& warning) {
    // Determine color based on level
    HPDF_REAL r, g, b;
    switch (warning.level) {
        case WindData::CRITICAL:
            r = 1.0; g = 0.0; b = 0.0; break;  // Red
        case WindData::HIGH_RISK:
            r = 1.0; g = 0.5; b = 0.0; break;  // Orange
        case WindData::WARNING:
            r = 1.0; g = 1.0; b = 0.0; break;  // Yellow
        case WindData::CAUTION:
            r = 0.0; g = 0.8; b = 1.0; break;  // Cyan
        default:
            r = 0.0; g = 0.8; b = 0.0; break;  // Green
    }

    // Draw box background
    HPDF_Page_SetRGBFill(page, r * 0.3, g * 0.3, b * 0.3);
    HPDF_Page_Rectangle(page, MARGIN, y_pos - 50, CONTENT_WIDTH, 50);
    HPDF_Page_Fill(page);

    // Draw text
    HPDF_Page_SetRGBFill(page, 1.0, 1.0, 1.0);  // White text
    HPDF_Page_SetFontAndSize(page, font_bold, 12);
    HPDF_Page_BeginText(page);
    std::string title = warning.level_name + " - " + warning.message;
    HPDF_Page_TextOut(page, MARGIN + 10, y_pos - 20, title.c_str());
    HPDF_Page_EndText(page);

    HPDF_Page_SetFontAndSize(page, font_normal, 9);
    HPDF_Page_BeginText(page);
    std::ostringstream detail;
    detail << "Wind percentile: " << std::fixed << std::setprecision(0) << warning.wind_percentile
           << "% | Stress ratio: " << std::setprecision(2) << (warning.stress_ratio * 100) << "%";
    HPDF_Page_TextOut(page, MARGIN + 10, y_pos - 40, detail.str().c_str());
    HPDF_Page_EndText(page);

    HPDF_Page_SetRGBFill(page, 0.0, 0.0, 0.0);  // Reset to black
    return y_pos - 60;
}

// Generate full PDF report
inline bool generate_report(
    const HSS_Member& member,
    const std::vector<VortexResults>& results,
    const std::vector<WindData::VortexWarning>& warnings,
    const WindData::WindClimate& location,
    double axial_force_kN,
    const std::string& filename = ""
) {
    HPDF_Doc pdf = HPDF_New(NULL, NULL);
    if (!pdf) {
        return false;
    }

    try {
        // Fonts
        HPDF_Font font_normal = HPDF_GetFont(pdf, "Helvetica", NULL);
        HPDF_Font font_bold = HPDF_GetFont(pdf, "Helvetica-Bold", NULL);
        HPDF_Font font_mono = HPDF_GetFont(pdf, "Courier", NULL);

        // ===== PAGE 1: Summary =====
        HPDF_Page page1 = HPDF_AddPage(pdf);
        HPDF_Page_SetSize(page1, HPDF_PAGE_SIZE_LETTER, HPDF_PAGE_PORTRAIT);

        draw_header(page1, font_bold, font_normal);
        float y = PAGE_HEIGHT - MARGIN - 40;

        // Input Parameters
        y = draw_section_header(page1, font_bold, y, "INPUT PARAMETERS");

        std::ostringstream input_info;
        input_info << "Material: " << member.material.name
                   << " (E=" << member.material.E_GPa << " GPa, ρ=" << member.material.density_kg_m3 << " kg/m³)";
        y = draw_text(page1, font_normal, y, input_info.str().c_str());

        input_info.str("");
        input_info << "Geometry: D=" << std::fixed << std::setprecision(1) << member.D_mm << " mm, "
                   << "t=" << member.t_mm << " mm, L=" << member.L_m << " m";
        y = draw_text(page1, font_normal, y, input_info.str().c_str());

        input_info.str("");
        input_info << "Damping: zeta=" << std::fixed << std::setprecision(4) << member.damping_ratio
                   << " | Axial Force: F=" << std::setprecision(1) << axial_force_kN << " kN";
        y = draw_text(page1, font_normal, y, input_info.str().c_str());

        input_info.str("");
        input_info << "Location: " << location.location << " (Mean wind: "
                   << std::fixed << std::setprecision(1) << location.mean_speed_ms << " m/s)";
        y = draw_text(page1, font_normal, y, input_info.str().c_str());

        y -= 10;

        // Calculated Properties
        y = draw_section_header(page1, font_bold, y, "CALCULATED SECTION PROPERTIES");

        input_info.str("");
        input_info << "Moment of Inertia: I = " << std::scientific << std::setprecision(3)
                   << member.I_m4 << " m⁴ = " << std::fixed << std::setprecision(0)
                   << (member.I_m4 * 1e12) << " mm⁴";
        y = draw_text(page1, font_normal, y, input_info.str().c_str());

        input_info.str("");
        input_info << "Cross-sectional Area: A = " << std::scientific << std::setprecision(3)
                   << member.A_m2 << " m² = " << std::fixed << std::setprecision(1)
                   << (member.A_m2 * 1e6) << " mm²";
        y = draw_text(page1, font_normal, y, input_info.str().c_str());

        input_info.str("");
        input_info << "Mass per unit length: μ = " << std::fixed << std::setprecision(2)
                   << member.mu_kg_m << " kg/m";
        y = draw_text(page1, font_normal, y, input_info.str().c_str());

        y -= 10;

        // Warnings Summary
        y = draw_section_header(page1, font_bold, y, "RISK ASSESSMENT");

        // Find worst warning
        WindData::WarningLevel worst_level = WindData::SAFE;
        size_t worst_idx = 0;
        for (size_t i = 0; i < warnings.size(); i++) {
            if (warnings[i].level > worst_level) {
                worst_level = warnings[i].level;
                worst_idx = i;
            }
        }

        y = draw_warning_box(page1, font_bold, font_normal, y, warnings[worst_idx]);

        y -= 10;

        // Recommendations
        y = draw_section_header(page1, font_bold, y, "ENGINEERING RECOMMENDATIONS");

        for (const auto& rec : warnings[worst_idx].recommendations) {
            std::string bullet = "• " + rec;
            y = draw_text(page1, font_normal, y, bullet.c_str(), 10);
            if (y < MARGIN + 50) break;  // Stop if approaching page bottom
        }

        // ===== PAGE 2: Results Table =====
        HPDF_Page page2 = HPDF_AddPage(pdf);
        HPDF_Page_SetSize(page2, HPDF_PAGE_SIZE_LETTER, HPDF_PAGE_PORTRAIT);

        draw_header(page2, font_bold, font_normal);
        y = PAGE_HEIGHT - MARGIN - 40;

        y = draw_section_header(page2, font_bold, y, "DETAILED RESULTS - ALL BOUNDARY CONDITIONS");

        // Table header
        HPDF_Page_SetFontAndSize(page2, font_bold, 9);
        HPDF_Page_BeginText(page2);
        HPDF_Page_TextOut(page2, MARGIN, y, "BC");
        HPDF_Page_TextOut(page2, MARGIN + 100, y, "Mode");
        HPDF_Page_TextOut(page2, MARGIN + 150, y, "f (Hz)");
        HPDF_Page_TextOut(page2, MARGIN + 210, y, "V (m/s)");
        HPDF_Page_TextOut(page2, MARGIN + 270, y, "F (kN)");
        HPDF_Page_TextOut(page2, MARGIN + 330, y, "σ_max (MPa)");
        HPDF_Page_TextOut(page2, MARGIN + 410, y, "Risk");
        HPDF_Page_EndText(page2);

        y -= 15;
        HPDF_Page_SetLineWidth(page2, 0.5);
        HPDF_Page_MoveTo(page2, MARGIN, y);
        HPDF_Page_LineTo(page2, PAGE_WIDTH - MARGIN, y);
        HPDF_Page_Stroke(page2);

        y -= 5;

        // Table rows
        HPDF_Page_SetFontAndSize(page2, font_normal, 8);
        for (size_t i = 0; i < results.size() && y > MARGIN + 30; i++) {
            const auto& bc_result = results[i];
            for (const auto& mode : bc_result.modes) {
                std::string bc_name = bc_result.bc_name;
                if (bc_name.length() > 15) bc_name = bc_name.substr(0, 12) + "...";

                std::ostringstream line;

                HPDF_Page_BeginText(page2);
                HPDF_Page_TextOut(page2, MARGIN, y, bc_name.c_str());

                line.str("");
                line << mode.mode_number;
                HPDF_Page_TextOut(page2, MARGIN + 100, y, line.str().c_str());

                line.str("");
                line << std::fixed << std::setprecision(2) << mode.freq_hz;
                HPDF_Page_TextOut(page2, MARGIN + 150, y, line.str().c_str());

                line.str("");
                line << std::fixed << std::setprecision(2) << mode.V_critical_ms;
                HPDF_Page_TextOut(page2, MARGIN + 210, y, line.str().c_str());

                line.str("");
                line << std::fixed << std::setprecision(3) << (mode.F_static_kN * mode.amplification);
                HPDF_Page_TextOut(page2, MARGIN + 270, y, line.str().c_str());

                line.str("");
                line << std::fixed << std::setprecision(1) << mode.distribution.max_stress_MPa;
                HPDF_Page_TextOut(page2, MARGIN + 330, y, line.str().c_str());

                if (i < warnings.size()) {
                    HPDF_Page_TextOut(page2, MARGIN + 410, y, warnings[i].level_name.c_str());
                }

                HPDF_Page_EndText(page2);

                y -= 12;
            }
        }

        // Save PDF
        std::string output_file = filename.empty() ? generate_filename() : filename;
        HPDF_SaveToFile(pdf, output_file.c_str());
        HPDF_Free(pdf);

        return true;

    } catch (...) {
        HPDF_Free(pdf);
        return false;
    }
}

} // namespace TorsorReport
