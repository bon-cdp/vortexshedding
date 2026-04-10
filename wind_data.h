// Torsor - Wind Climate Database for Vortex Shedding Analysis
// Provides location-specific wind speed distributions and warning thresholds

#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

namespace WindData {

// Wind climate data structure
struct WindClimate {
    std::string location;
    std::string region;          // Geographic region
    double weibull_k;            // Shape parameter (dimensionless)
    double weibull_c;            // Scale parameter (m/s)
    double mean_speed_ms;        // Mean wind speed (m/s)
    double percentile_50;        // Median wind speed (m/s)
    double percentile_75;        // 75th percentile (m/s)
    double percentile_90;        // 90th percentile (m/s)
    double percentile_95;        // 95th percentile (m/s)
    double percentile_99;        // 99th percentile (m/s)
    std::string standard;        // Reference standard
    std::string description;     // Location details

    // Custom uniform-distribution support.
    // When uniform == true, the Weibull parameters are ignored and the wind
    // is treated as uniformly distributed between v_min and v_max.
    bool   uniform = false;
    double v_min   = 0.0;
    double v_max   = 0.0;
};

// Calculate Weibull percentile: V_p = c * (-ln(1-p))^(1/k)
inline double weibull_percentile(double c, double k, double percentile) {
    return c * std::pow(-std::log(1.0 - percentile), 1.0 / k);
}

// Calculate mean wind speed from Weibull: mean = c * Gamma(1 + 1/k)
inline double weibull_mean(double c, double k) {
    // Approximation: Gamma(1+1/k) ≈ 0.5772 + 0.9/k for typical k values
    double gamma_approx = 0.5772 + 0.9 / k;
    return c * gamma_approx;
}

// Find percentile of a given wind speed in Weibull distribution
inline double weibull_cdf(double v, double c, double k) {
    if (v <= 0.0) return 0.0;
    return 1.0 - std::exp(-std::pow(v / c, k));
}

// Unified CDF / inverse-CDF that handle either a Weibull climate or a
// custom uniform climate, depending on the climate's `uniform` flag.
inline double climate_cdf(const WindClimate& c, double v) {
    if (c.uniform) {
        if (c.v_max <= c.v_min) return 0.0;
        if (v <= c.v_min) return 0.0;
        if (v >= c.v_max) return 1.0;
        return (v - c.v_min) / (c.v_max - c.v_min);
    }
    return weibull_cdf(v, c.weibull_c, c.weibull_k);
}

inline double climate_percentile(const WindClimate& c, double p) {
    if (c.uniform) {
        if (c.v_max <= c.v_min) return c.v_min;
        return c.v_min + p * (c.v_max - c.v_min);
    }
    return weibull_percentile(c.weibull_c, c.weibull_k, p);
}

// Global wind climate database (major international ports)
// Data sources: ASCE 7, Eurocode, local meteorological agencies
const std::vector<WindClimate> PORT_CLIMATES = {
    // US Gulf Coast
    {
        "Houston Ship Channel, TX",
        "US Gulf Coast",
        2.1,  // k (moderate variability)
        6.8,  // c (m/s)
        6.0,  // mean
        6.1,  // 50th
        8.0,  // 75th
        10.2, // 90th
        11.8, // 95th
        15.0, // 99th
        "ASCE 7-22",
        "Major petrochemical port - hurricane exposure"
    },
    {
        "Port of New Orleans, LA",
        "US Gulf Coast",
        1.9,  // k
        7.2,  // c
        6.4,
        6.5,
        8.6,
        11.0,
        12.8,
        16.3,
        "ASCE 7-22",
        "Mississippi River delta - high wind exposure"
    },

    // US Atlantic Coast
    {
        "Port of Norfolk, VA",
        "US Atlantic",
        2.0,
        7.0,
        6.2,
        6.3,
        8.3,
        10.6,
        12.3,
        15.6,
        "ASCE 7-22",
        "Mid-Atlantic - moderate exposure"
    },
    {
        "Port of New York/New Jersey",
        "US Atlantic",
        2.2,
        6.5,
        5.8,
        5.8,
        7.6,
        9.7,
        11.2,
        14.2,
        "ASCE 7-22",
        "Urban coastal - sheltered by buildings"
    },

    // US West Coast
    {
        "Port of Long Beach, CA",
        "US Pacific",
        2.3,
        5.2,
        4.6,
        4.6,
        6.0,
        7.6,
        8.8,
        11.2,
        "ASCE 7-22",
        "Southern California - mild climate"
    },

    // Canada West Coast
    {
        "Port of Vancouver, BC",
        "Canadian Pacific",
        2.1,
        6.0,
        5.3,
        5.4,
        7.1,
        9.0,
        10.4,
        13.2,
        "NBCC 2020",
        "British Columbia - moderate maritime climate"
    },

    // Europe
    {
        "Port of Rotterdam, Netherlands",
        "North Sea",
        2.0,
        7.8,
        6.9,
        7.0,
        9.2,
        11.7,
        13.6,
        17.3,
        "Eurocode EN 1991-1-4",
        "Major European port - high wind exposure"
    },
    {
        "Port of Hamburg, Germany",
        "Baltic Sea",
        1.9,
        7.5,
        6.6,
        6.7,
        8.9,
        11.3,
        13.1,
        16.7,
        "Eurocode EN 1991-1-4",
        "Inland port - moderate exposure"
    },

    // Asia-Pacific
    {
        "Port of Singapore",
        "Southeast Asia",
        1.7,
        5.5,
        4.9,
        4.9,
        6.5,
        8.3,
        9.6,
        12.2,
        "ISO 4354",
        "Tropical equatorial - low wind, monsoon exposure"
    },
    {
        "Port of Shanghai, China",
        "East China Sea",
        2.0,
        6.2,
        5.5,
        5.6,
        7.4,
        9.4,
        10.9,
        13.9,
        "GB 50009 (China)",
        "Typhoon region - seasonal high winds"
    },
    {
        "Port of Busan, South Korea",
        "Sea of Japan",
        2.1,
        6.5,
        5.7,
        5.8,
        7.6,
        9.7,
        11.3,
        14.3,
        "KDS 41 (Korea)",
        "Typhoon exposure - moderate winds"
    },
    {
        "Port of Tokyo, Japan",
        "Pacific Ocean",
        2.0,
        6.8,
        6.0,
        6.1,
        8.0,
        10.2,
        11.8,
        15.0,
        "AIJ-2004 (Japan)",
        "Typhoon region - high exposure"
    },

    // Middle East
    {
        "Port of Dubai, UAE",
        "Persian Gulf",
        1.8,
        5.8,
        5.1,
        5.2,
        6.9,
        8.8,
        10.2,
        13.0,
        "Dubai Municipality",
        "Desert climate - moderate coastal winds"
    },

    // Australia
    {
        "Port of Melbourne, Australia",
        "Southern Ocean",
        2.1,
        7.2,
        6.4,
        6.5,
        8.5,
        10.8,
        12.6,
        16.0,
        "AS/NZS 1170.2",
        "Southern exposure - consistent winds"
    },

    // South America
    {
        "Port of Santos, Brazil",
        "South Atlantic",
        1.9,
        6.0,
        5.3,
        5.4,
        7.1,
        9.1,
        10.5,
        13.4,
        "NBR 6123 (Brazil)",
        "Tropical - moderate exposure"
    },

    // Generic reference climates
    {
        "Low Wind (Generic)",
        "Sheltered/Inland",
        2.0,
        4.5,
        4.0,
        4.0,
        5.3,
        6.7,
        7.8,
        9.9,
        "Generic",
        "Sheltered inland locations"
    },
    {
        "Moderate Wind (Generic)",
        "Coastal/Open",
        2.0,
        6.5,
        5.8,
        5.9,
        7.7,
        9.8,
        11.4,
        14.5,
        "Generic",
        "Typical coastal exposure"
    },
    {
        "High Wind (Generic)",
        "Exposed/Offshore",
        2.0,
        8.5,
        7.5,
        7.6,
        10.0,
        12.8,
        14.8,
        18.8,
        "Generic",
        "Highly exposed offshore structures"
    },

    // User-driven uniform distribution. v_min / v_max are filled in by the
    // GUI from the AppState; the percentile fields are recomputed at use time.
    {
        "Custom (Uniform Distribution)",
        "User-defined",
        2.0,                                // weibull_k (unused)
        6.5,                                // weibull_c (unused)
        12.5,                               // mean (placeholder, recomputed)
        12.5, 18.75, 22.5, 23.75, 24.75,    // percentiles (placeholder, recomputed)
        "User-defined",
        "Uniform wind speed distribution between user-specified min and max",
        true,    // uniform
        0.0,     // v_min (default, overridden by GUI)
        25.0     // v_max (default, overridden by GUI)
    }
};

// Warning levels for vortex shedding risk
enum WarningLevel {
    SAFE,
    CAUTION,
    WARNING,
    HIGH_RISK,
    CRITICAL
};

struct VortexWarning {
    WarningLevel level;
    std::string level_name;
    std::string message;
    std::vector<std::string> recommendations;
    double wind_percentile;  // What percentile is V_critical at this location?
    double stress_ratio;     // Max stress / yield strength
};

// Assess vortex shedding risk at a location
inline VortexWarning assess_risk(
    double V_critical_ms,
    double max_stress_MPa,
    double yield_strength_MPa,
    double L_m,
    double D_mm,
    const WindClimate& location
) {
    VortexWarning warning;

    // Calculate what percentile the critical wind speed falls at
    // (handles both Weibull and uniform climates).
    warning.wind_percentile = climate_cdf(location, V_critical_ms) * 100.0;

    // Calculate stress ratio
    warning.stress_ratio = max_stress_MPa / yield_strength_MPa;

    // Cap: if V_critical is beyond the 99.99th percentile of the local wind
    // climate, the mode is practically unreachable - skip the risk decision
    // tree to avoid false-positive flags on high secondary modes whose
    // critical wind speeds the local climate will never produce.
    if (warning.wind_percentile >= 99.99) {
        warning.level      = SAFE;
        warning.level_name = "UNREACHABLE";
        warning.message    = "V_critical beyond 99.99th percentile of local wind";
        warning.recommendations.push_back(
            "Mode is not excited by realistic wind speeds at this location - no action required");
        return warning;
    }

    // Calculate slenderness
    double slenderness = L_m / (D_mm / 1000.0);

    // Determine warning level based on multiple factors
    bool frequent_resonance = (warning.wind_percentile < 75.0);  // V_critical below 75th percentile
    bool high_stress = (warning.stress_ratio > 0.5);
    bool critical_stress = (warning.stress_ratio > 0.8);
    bool very_frequent = (warning.wind_percentile < 50.0);  // Below median

    // Decision tree for warning level
    if (critical_stress) {
        warning.level = CRITICAL;
        warning.level_name = "CRITICAL";
        warning.message = "Stress exceeds 80% of yield strength";
        warning.recommendations.push_back("IMMEDIATE ACTION: Increase member size or add substantial bracing");
        warning.recommendations.push_back("Consider higher grade steel (e.g., A572-50 → A992)");
        warning.recommendations.push_back("Perform detailed fatigue analysis before deployment");
    } else if (very_frequent && high_stress) {
        warning.level = HIGH_RISK;
        warning.level_name = "HIGH RISK";
        warning.message = "Resonance occurs in median wind conditions with high stress";

        if (slenderness > 50) {
            warning.recommendations.push_back("Add lateral bracing at L/3 points to change boundary conditions");
            warning.recommendations.push_back("Consider tuned mass damper or helical strakes");
        } else {
            warning.recommendations.push_back("Increase wall thickness (stocky member - bracing less effective)");
            warning.recommendations.push_back("Consider aerodynamic fairing to disrupt vortex formation");
        }
        warning.recommendations.push_back("Field retrofit: Bracing preferred over member replacement (limited sizes available)");
    } else if (frequent_resonance || high_stress) {
        warning.level = WARNING;
        warning.level_name = "WARNING";
        warning.message = "Resonance possible in common wind conditions";

        warning.recommendations.push_back("Monitor vibration during commissioning");
        warning.recommendations.push_back("Inspect welds quarterly for fatigue cracking");

        if (slenderness > 40) {
            warning.recommendations.push_back("Consider lateral bracing if vibration observed");
        }

        if (warning.stress_ratio > 0.3) {
            warning.recommendations.push_back("Perform S-N curve fatigue analysis for design life");
        }
    } else if (warning.wind_percentile < 90.0) {
        warning.level = CAUTION;
        warning.level_name = "CAUTION";
        warning.message = "Resonance occurs in strong winds (occasional)";
        warning.recommendations.push_back("Monitor during storm events");
        warning.recommendations.push_back("Document vibration behavior during commissioning");

        if (warning.stress_ratio > 0.25) {
            warning.recommendations.push_back("Consider periodic inspection for fatigue");
        }
    } else {
        warning.level = SAFE;
        warning.level_name = "ACCEPTABLE";
        warning.message = "Vibration risk within acceptable limits";
        warning.recommendations.push_back("No immediate action required");
        warning.recommendations.push_back("Include in routine structural inspection schedule");
    }

    return warning;
}

// Simplified fatigue life estimation (S-N curve approach)
// Returns estimated years to fatigue failure
inline double estimate_fatigue_life(
    double stress_amplitude_MPa,
    double freq_hz,
    double duty_cycle = 1.0  // Fraction of time at resonance (1.0 = continuous)
) {
    // Basquin equation: Nf = C * (Δσ)^(-m)
    // For steel welds: m ≈ 3, C ≈ 2e12 (MPa^3·cycles)
    const double m = 3.0;
    const double C = 2.0e12;  // Conservative for welded steel

    if (stress_amplitude_MPa < 10.0) {
        return 1000.0;  // Effectively infinite life
    }

    // Cycles to failure
    double Nf = C / std::pow(stress_amplitude_MPa, m);

    // Cycles per year
    double cycles_per_year = freq_hz * 3600.0 * 24.0 * 365.0 * duty_cycle;

    // Years to failure
    double years = Nf / cycles_per_year;

    return std::min(years, 1000.0);  // Cap at 1000 years
}

} // namespace WindData
