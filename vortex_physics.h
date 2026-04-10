#ifndef VORTEX_PHYSICS_H
#define VORTEX_PHYSICS_H

// Torsor v0.2: Vortex Shedding Analysis - Physics Engine
// Calculates natural frequencies, critical wind speeds, and dynamic forces
// for HSS round members under vortex-induced vibration
// Includes wind climate database and engineering warnings

#include <cmath>
#include <string>
#include <vector>
#include <array>
#include "wind_data.h"  // Wind climate database and warning system

const double PI = 3.14159265358979323846;

// ============================================================================
// MATERIAL DATABASE
// ============================================================================

struct Material {
    std::string name;
    double E_GPa;              // Young's modulus (GPa) - input unit
    double density_kg_m3;      // Density (kg/m³)
};

// Standard structural steel materials (metric SI units)
const std::vector<Material> MATERIALS = {
    {"A36 Steel", 200.0, 7850.0},        // E = 200 GPa, ρ = 7850 kg/m³
    {"A572-50 Steel", 200.0, 7850.0},
    {"A500 Grade B HSS", 200.0, 7850.0},
    {"Stainless 316", 193.0, 8000.0},
    {"Aluminum 6061", 68.9, 2700.0},
    {"Custom", 0.0, 0.0}  // User-defined
};

// ============================================================================
// BOUNDARY CONDITION COEFFICIENTS
// ============================================================================

struct BoundaryCondition {
    std::string name;
    std::vector<double> A_coeffs;  // A coefficients for each mode (1-indexed, mode 0 unused)

    // From reference: "Beams of Uniform Section and Uniformly Distributed Load"
    // ω_n = A√(EI/μL⁴) RAD/SEC
    // Complete table for modes 1-5
};

const std::vector<BoundaryCondition> BOUNDARY_CONDITIONS = {
    {"Fixed-Free (Cantilever)", {0.0, 3.52, 22.0, 61.7, 121.0, 200.0}},
    {"Hinged-Hinged (Simple)", {0.0, 9.87, 39.5, 88.9, 158, 247}},
    {"Fixed-Fixed (Built-in)", {0.0, 22.4, 61.7, 121, 200, 298}},
    {"Free-Free", {0.0, 22.4, 61.7, 121, 200, 298}},  // Same coefficients as Fixed-Fixed (confirmed)
    {"Fixed-Hinged", {0.0, 15.4, 50.0, 104, 178, 272}},
    {"Hinged-Free", {0.0, 15.4, 50.0, 104, 178, 272}}  // Same coefficients as Fixed-Hinged (confirmed)
};

// ============================================================================
// HSS MEMBER PROPERTIES
// ============================================================================

struct HSS_Member {
    // Geometric properties (INPUT units for user convenience)
    double D_mm;           // Outer diameter (mm)
    double t_mm;           // Wall thickness (mm)
    double L_m;            // Length (m)

    // Material properties
    Material material;

    // Derived properties (INTERNAL calculation in base SI: meters, kilograms)
    double I_m4;           // Moment of inertia (m⁴) - for calculation
    double A_m2;           // Cross-sectional area (m²) - for calculation
    double mu_kg_m;        // Mass per unit length (kg/m)
    double r_gyration_mm;  // Radius of gyration (mm) - for display

    // Dynamic properties
    double damping_ratio;  // Damping ratio (ζ, dimensionless, typically 0.001-0.02 for steel)

    // Calculate derived properties
    void calculate_properties() {
        // Convert inputs to base SI (meters)
        double D_m = D_mm / 1000.0;       // mm → m
        double t_m = t_mm / 1000.0;       // mm → m
        double D_inner_m = D_m - 2.0 * t_m;

        // Area: A = π(D²-(D-2t)²)/4
        A_m2 = PI * (D_m*D_m - D_inner_m*D_inner_m) / 4.0;  // m²

        // Moment of inertia: I = π(D⁴-(D-2t)⁴)/64
        I_m4 = PI * (std::pow(D_m, 4) - std::pow(D_inner_m, 4)) / 64.0;  // m⁴

        // Mass per unit length: μ = ρ * A
        // ρ in kg/m³, A in m² → μ in kg/m
        mu_kg_m = material.density_kg_m3 * A_m2;  // kg/m

        // Radius of gyration (convert back to mm for display)
        r_gyration_mm = std::sqrt(I_m4 / A_m2) * 1000.0;  // m → mm
    }
};

// ============================================================================
// VORTEX SHEDDING RESULTS
// ============================================================================

// Distribution results for visualization
struct DistributionData {
    std::vector<double> x_m;           // Position along beam (m)
    std::vector<double> moment_kNm;    // Bending moment M(x) (kN·m)
    std::vector<double> stress_MPa;    // Bending stress σ(x) (MPa)
    double udl_kN_m;                   // Equivalent uniform distributed load (kN/m)
    double max_moment_kNm;             // Maximum moment
    double max_stress_MPa;             // Maximum stress
};

struct ModeResult {
    int mode_number;
    double omega;              // Natural frequency (rad/s)
    double freq_hz;            // Natural frequency (Hz)
    double V_critical_ms;      // Critical wind speed (m/s)
    double F_static_kN;        // Static force approximation (kN)
    double omega_axial;        // Natural frequency with axial load (rad/s)
    double amplification;      // Dynamic amplification factor |H(f)|

    // Intermediate calculations (for visualization and validation)
    double omega_D2;           // ωD² parameter for Strouhal number selection (m²·rad/s)
    double qh_Pa;              // Velocity pressure (Pa)
    double alpha;              // Axial load parameter (dimensionless)
    double strouhal;           // Strouhal number S = ωD/V (dimensionless)
    double sqrt_L_D;           // √(L/D) ratio (dimensionless)
    double mass_ratio;         // C2ρD²/M (dimensionless)
    double damping_param;      // ζ - C2ρD²/M (dimensionless)

    // Distribution data for color mapping
    DistributionData distribution;
};

struct VortexResults {
    std::string bc_name;       // Boundary condition name
    std::vector<ModeResult> modes;  // Results for each mode
};

// Damping uncertainty range results
// Shows how results vary across typical damping range (0.005 to 0.02)
struct DampingRange {
    double damping_min;        // Minimum damping ratio (typically 0.005)
    double damping_nominal;    // Nominal damping ratio (typically 0.01)
    double damping_max;        // Maximum damping ratio (typically 0.02)

    ModeResult result_min;     // Results at minimum damping (worst case - highest forces)
    ModeResult result_nominal; // Results at nominal damping
    ModeResult result_max;     // Results at maximum damping (best case - lowest forces)

    // Helper to get range as string
    std::string get_stress_range_str() const {
        char buf[128];
        snprintf(buf, sizeof(buf), "%.1f [%.1f-%.1f]",
                 result_nominal.distribution.max_stress_MPa,
                 result_max.distribution.max_stress_MPa,  // Max damping → min stress
                 result_min.distribution.max_stress_MPa); // Min damping → max stress
        return std::string(buf);
    }

    std::string get_force_range_str() const {
        char buf[128];
        snprintf(buf, sizeof(buf), "%.3f [%.3f-%.3f]",
                 result_nominal.F_static_kN * result_nominal.amplification,
                 result_max.F_static_kN * result_max.amplification,
                 result_min.F_static_kN * result_min.amplification);
        return std::string(buf);
    }
};

// ============================================================================
// PHYSICS CALCULATIONS
// ============================================================================

namespace VortexPhysics {

// Calculate natural frequency for a given mode
// ω = A * sqrt(E * I / (μ * L⁴))
// Units (SI): [rad/s] = [1] * sqrt([Pa]*[m⁴] / ([kg/m]*[m⁴]))
//                     = sqrt([N/m²]*[m⁴] / ([kg/m]*[m⁴]))
//                     = sqrt([kg·m/s²/m²]*[m⁴] / ([kg]*[m³]))
//                     = sqrt([kg·m³/s²] / [kg·m³])
//                     = sqrt(1/s²) = rad/s ✓
inline double calculate_natural_frequency(double E_Pa, double I_m4, double mu_kg_m, double L_m, double A_coeff) {
    // ω = A * sqrt(E*I / (μ*L⁴))
    double omega = A_coeff * std::sqrt(E_Pa * I_m4 / (mu_kg_m * std::pow(L_m, 4)));
    return omega;  // rad/s
}

// Calculate critical wind speed using Strouhal number relationship
// V = f * D / S where S is the Strouhal number (≈ 0.2 for circular cylinders)
// f = ω / (2π) is the frequency in Hz
inline double calculate_critical_wind_speed(double omega, double D_m) {
    const double STROUHAL = 0.2;  // Standard Strouhal number for circular cylinders

    // V = (ω / 2π) * D / S = ω * D / (2π * S)
    double V = omega * D_m / (2.0 * PI * STROUHAL);  // m/s

    return V;  // m/s
}

// Calculate static force approximation from vortex shedding
// F = C1 / (sqrt(L/D) * (ζ - C2*(ρ*D²)/M)^0.5) * qh * D * L
// where qh = 0.5 * ρ * V²
// All inputs in SI base units (m, kg, m/s, kg/m³), output in N (convert to kN)
inline double calculate_static_force(double V_ms, double D_m, double L_m, double damping, double mu_kg_m, double rho_air_kg_m3 = 1.225) {
    // Constants
    const double C1 = 3.0;
    const double C2 = 0.6;

    // Air density at sea level: 1.225 kg/m³
    // Velocity pressure: qh = 0.5 * ρ * V² (Pa = N/m²)
    double qh = 0.5 * rho_air_kg_m3 * V_ms * V_ms;  // Pa

    // Denominator calculation - CORRECTED to use total mass M = μ*L
    double sqrt_L_D = std::sqrt(L_m / D_m);
    double M_total = mu_kg_m * L_m;  // Total mass (kg)
    double mass_term = C2 * rho_air_kg_m3 * D_m * D_m / M_total;
    double denom_inner = damping - mass_term;

    // Handle case where damping is very low
    if (denom_inner <= 0) {
        denom_inner = 1e-6;  // Prevent division by zero
    }

    double denom = sqrt_L_D * std::sqrt(denom_inner);

    // Static force: F = (C1 / denom) * qh * D * L
    // Units: [1/(1·1)] * [Pa] * [m] * [m] = [N/m²] * [m²] = N
    double F_N = (C1 / denom) * qh * D_m * L_m;  // N

    return F_N;  // N (will convert to kN for display)
}

// Calculate natural frequency with axial load effect
// ω_a = ω * sqrt(1 ± α²/n²)
// α = F*L²/(E*I*π²)
// All inputs in SI base units
inline double calculate_axial_frequency(double omega, double F_N, double L_m, double E_Pa, double I_m4, int mode) {
    double alpha_squared = (F_N * L_m * L_m) / (E_Pa * I_m4 * PI * PI);
    double mode_squared = mode * mode;

    // Use + for tension, - for compression
    // For vortex shedding, typically assume tension from wind drag
    double factor = 1.0 + alpha_squared / mode_squared;

    if (factor < 0) factor = 0;  // Can't have imaginary frequency

    double omega_axial = omega * std::sqrt(factor);
    return omega_axial;  // rad/s
}

// Calculate dynamic amplification factor for SDOF system
// |H(f)| = 1 / sqrt((1-r²)² + (2ζr)²)
// where r = f/fn (frequency ratio), typically use r = 1 for maximum
// From Figure 7: SDOF Dynamic amplification factor
inline double calculate_amplification(double damping, double freq_ratio = 1.0) {
    double r = freq_ratio;
    double zeta = damping;

    double term1 = (1.0 - r*r) * (1.0 - r*r);
    double term2 = (2.0 * zeta * r) * (2.0 * zeta * r);

    double H = 1.0 / std::sqrt(term1 + term2);

    return H;
}

// Calculate moment distribution along beam
// For vortex-induced vibration following modal shape, use mode-dependent coefficient
// The moment distribution follows the modal load pattern, not simple UDL
inline double calculate_moment_at_x(double x_m, double L_m, double F_kN, const std::string& bc_name, int mode) {
    // Modal load creates different moment distribution than UDL
    // Use empirically-derived modal shape coefficients from reference data
    double w_kN_m = F_kN / L_m;  // Equivalent UDL

    // Mode-dependent moment coefficient (from M/UDL/L² ratio analysis)
    // Mode 1: M_max/(w×L²) ≈ 0.125 (gives M/UDL ≈ 26.1 for L=14.45m)
    // Mode 2: M_max/(w×L²) ≈ 0.0434 (gives M/UDL ≈ 8.7 for L=14.45m)
    double mode_coeff;
    if (mode == 1) {
        mode_coeff = 0.125;  // 1/8
    } else if (mode == 2) {
        mode_coeff = 0.0434;  // ≈1/23
    } else {
        // For higher modes, interpolate or use conservative estimate
        mode_coeff = 0.125 / (mode * mode);  // Decreases with mode²
    }

    // For simplicity, use parabolic distribution with mode-corrected peak
    // M(x) = mode_coeff × w × L² × (4x/L)(1 - x/L)
    double x_norm = x_m / L_m;
    return mode_coeff * w_kN_m * L_m * L_m * 4.0 * x_norm * (1.0 - x_norm);  // kN·m
}

// Calculate bending stress from moment
// σ = M * c / I where c = D/2 (distance to extreme fiber)
inline double calculate_stress_from_moment(double M_kNm, double D_mm, double I_m4) {
    // Convert M from kN·m to N·m
    double M_Nm = M_kNm * 1000.0;

    // Distance to extreme fiber (m)
    double c_m = (D_mm / 1000.0) / 2.0;  // mm → m, then /2

    // σ = Mc/I (Pa)
    double stress_Pa = (M_Nm * c_m) / I_m4;

    // Convert to MPa
    return stress_Pa / 1.0e6;  // Pa → MPa
}

// Calculate equivalent uniform distributed load
// w = 8F/L for simply supported beam with center load equivalent
inline double calculate_UDL(double F_kN, double L_m) {
    return F_kN / L_m;  // Simplified: total force / length (kN/m)
}

// Calculate full distribution for visualization (100 points along beam)
inline DistributionData calculate_distribution(double F_kN, double L_m, double D_mm,
                                               double I_m4, const std::string& bc_name, int mode) {
    DistributionData dist;
    const int num_points = 100;

    dist.udl_kN_m = calculate_UDL(F_kN, L_m);
    dist.max_moment_kNm = 0.0;
    dist.max_stress_MPa = 0.0;

    for (int i = 0; i <= num_points; i++) {
        double x = (static_cast<double>(i) / num_points) * L_m;
        dist.x_m.push_back(x);

        double M = calculate_moment_at_x(x, L_m, F_kN, bc_name, mode);
        dist.moment_kNm.push_back(M);

        double sigma = calculate_stress_from_moment(M, D_mm, I_m4);
        dist.stress_MPa.push_back(sigma);

        if (M > dist.max_moment_kNm) dist.max_moment_kNm = M;
        if (sigma > dist.max_stress_MPa) dist.max_stress_MPa = sigma;
    }

    return dist;
}

// Analyze a single mode
inline ModeResult analyze_mode(const HSS_Member& member, const BoundaryCondition& bc, int mode, double axial_force_kN = 0.0) {
    ModeResult result;
    result.mode_number = mode;

    // Convert material E from GPa to Pa for calculation
    double E_Pa = member.material.E_GPa * 1.0e9;  // GPa → Pa

    // Convert D from mm to m for calculations
    double D_m = member.D_mm / 1000.0;  // mm → m

    // 1. Natural frequency (without axial effect)
    result.omega = calculate_natural_frequency(
        E_Pa,
        member.I_m4,
        member.mu_kg_m,
        member.L_m,
        bc.A_coeffs[mode]
    );
    result.freq_hz = result.omega / (2.0 * PI);

    // 2. Critical wind speed (use omega_axial if axial force present)
    double F_axial_N = axial_force_kN * 1000.0;  // kN → N

    if (axial_force_kN != 0.0) {
        // Calculate frequency with axial load effect
        result.omega_axial = calculate_axial_frequency(
            result.omega,
            F_axial_N,
            member.L_m,
            E_Pa,
            member.I_m4,
            mode
        );
        result.alpha = (F_axial_N * member.L_m * member.L_m) / (E_Pa * member.I_m4 * PI * PI);

        // Use axial-modified frequency for wind speed calculation
        result.omega_D2 = result.omega_axial * D_m * D_m;  // m²·rad/s
        result.V_critical_ms = calculate_critical_wind_speed(result.omega_axial, D_m);
    } else {
        // No axial force - use regular frequency
        result.omega_axial = result.omega;
        result.alpha = 0.0;
        result.omega_D2 = result.omega * D_m * D_m;  // m²·rad/s
        result.V_critical_ms = calculate_critical_wind_speed(result.omega, D_m);
    }

    // 3. Static force
    double F_N = calculate_static_force(
        result.V_critical_ms,
        D_m,
        member.L_m,
        member.damping_ratio,
        member.mu_kg_m
    );
    result.F_static_kN = F_N / 1000.0;  // N → kN

    // 4. Dynamic amplification (at resonance, r = 1.0)
    result.amplification = calculate_amplification(member.damping_ratio, 1.0);

    // 6. Calculate intermediate values for validation
    const double rho_air = 1.225;  // kg/m³
    result.qh_Pa = 0.5 * rho_air * result.V_critical_ms * result.V_critical_ms;  // Velocity pressure (Pa)
    result.strouhal = (result.omega * D_m) / result.V_critical_ms;  // Strouhal number S = ωD/V
    result.sqrt_L_D = std::sqrt(member.L_m / D_m);  // √(L/D) ratio

    double M_total = member.mu_kg_m * member.L_m;  // Total mass (kg)
    const double C2 = 0.6;
    result.mass_ratio = C2 * rho_air * D_m * D_m / M_total;  // C2ρD²/M
    result.damping_param = member.damping_ratio - result.mass_ratio;  // ζ - C2ρD²/M

    // 7. Calculate moment/stress/UDL distributions for visualization
    // Apply dynamic amplification to get actual force (not just static)
    double F_dynamic_kN = result.F_static_kN * result.amplification;

    result.distribution = calculate_distribution(
        F_dynamic_kN,  // Use amplified dynamic force, not static
        member.L_m,
        member.D_mm,
        member.I_m4,
        bc.name,
        mode  // Pass mode number for modal shape coefficient
    );

    return result;
}

// Analyze a mode with damping uncertainty range
// Computes results for minimum, nominal, and maximum damping values
// Typical range: 0.005 (bare steel, low damping) to 0.02 (with attachments, bolted connections)
inline DampingRange analyze_mode_with_damping_range(
    const HSS_Member& member,
    const BoundaryCondition& bc,
    int mode,
    double axial_force_kN = 0.0,
    double damping_min = 0.005,
    double damping_max = 0.02
) {
    DampingRange range;
    range.damping_min = damping_min;
    range.damping_nominal = member.damping_ratio;
    range.damping_max = damping_max;

    // Create modified members with different damping ratios
    HSS_Member member_min = member;
    member_min.damping_ratio = damping_min;

    HSS_Member member_max = member;
    member_max.damping_ratio = damping_max;

    // Calculate results for all three damping values
    range.result_min = analyze_mode(member_min, bc, mode, axial_force_kN);
    range.result_nominal = analyze_mode(member, bc, mode, axial_force_kN);
    range.result_max = analyze_mode(member_max, bc, mode, axial_force_kN);

    return range;
}

// Analyze all boundary conditions and modes
inline std::vector<VortexResults> analyze_member(const HSS_Member& member, int num_modes = 2, double axial_force_kN = 0.0) {
    std::vector<VortexResults> all_results;

    for (const auto& bc : BOUNDARY_CONDITIONS) {
        VortexResults bc_results;
        bc_results.bc_name = bc.name;

        for (int mode = 1; mode <= num_modes && static_cast<size_t>(mode) < bc.A_coeffs.size(); mode++) {
            ModeResult mode_result = analyze_mode(member, bc, mode, axial_force_kN);
            bc_results.modes.push_back(mode_result);
        }

        all_results.push_back(bc_results);
    }

    return all_results;
}

// Modal shape function for visualization
// Returns deflection at position x along length L
// Mode shapes for different boundary conditions:
// Fixed-Fixed: y(x) = (1 - cos(2πnx/L))
// Fixed-Hinged: y(x) = sin(πnx/L) - sinh(βnx/L) (approximate)
// Hinged-Hinged: y(x) = sin(πnx/L)
// Cantilever: y(x) = sin((2n-1)πx/2L) (approximate)
inline double modal_shape(double x, double L, const std::string& bc_name, int mode) {
    double xi = x / L;  // Normalized position [0, 1]

    // Use substring matching since BC names include descriptions like "(Simple)", "(Built-in)"
    if (bc_name.find("Hinged-Hinged") != std::string::npos) {
        // Simply supported: pure sine wave (symmetric)
        return std::sin(mode * PI * xi);
    } else if (bc_name.find("Fixed-Fixed") != std::string::npos) {
        // Built-in: approximate as (1 - cos(2πnξ))
        return 1.0 - std::cos(2.0 * PI * mode * xi);
    } else if (bc_name.find("Fixed-Hinged") != std::string::npos) {
        // Fixed at left, hinged at right: amplitude grows toward right
        return std::sin(mode * PI * xi) * (0.5 + 0.5 * xi);
    } else if (bc_name.find("Hinged-Free") != std::string::npos) {
        // Hinged at left, free at right: strong bias toward free end
        return std::sin((2.0 * mode - 1.0) * PI * xi / 2.0) * (1.0 + xi);
    } else if (bc_name.find("Cantilever") != std::string::npos || bc_name.find("Fixed-Free") != std::string::npos) {
        // Cantilever: fixed at left, free at right
        return std::sin((2.0 * mode - 1.0) * PI * xi / 2.0);
    } else if (bc_name.find("Free-Free") != std::string::npos) {
        // Free-free: cosine mode (symmetric)
        return std::cos(mode * PI * xi);
    }

    // Fallback: simple sine wave
    return std::sin(mode * PI * xi);
}

} // namespace VortexPhysics

#endif // VORTEX_PHYSICS_H
