// Torsor - Vortex Shedding Calculator Test Suite
// Validates calculations against Excel reference values

#include "vortex_physics.h"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace VortexPhysics;

// Test tolerance (5% error allowed for engineering calculations)
const double TOLERANCE = 0.05;  // 5%

bool isClose(double actual, double expected, double tolerance = TOLERANCE) {
    if (expected == 0.0) return std::abs(actual) < 1e-10;
    double error = std::abs((actual - expected) / expected);
    return error <= tolerance;
}

void printTest(const std::string& name, double actual, double expected, const std::string& units) {
    bool passed = isClose(actual, expected);
    double error = (expected != 0.0) ? std::abs((actual - expected) / expected) * 100.0 : 0.0;

    std::cout << (passed ? "[PASS] " : "[FAIL] ") << name << "\n";
    std::cout << "  Expected: " << std::fixed << std::setprecision(4) << expected << " " << units << "\n";
    std::cout << "  Actual:   " << std::fixed << std::setprecision(4) << actual << " " << units << "\n";
    std::cout << "  Error:    " << std::fixed << std::setprecision(2) << error << "%\n\n";
}

void runTestCase(const std::string& testName, const std::string& bcName, int bcIndex, int mode,
                 double expected_omega, double expected_V, double expected_UDL,
                 double expected_M, double expected_stress,
                 int& totalPassed, int& totalTests) {

    std::cout << testName << "\n";
    std::cout << "----------------------------------------\n\n";

    // Create member (same for all test cases)
    HSS_Member member;
    member.D_mm = 406.4;           // 16 inches
    member.t_mm = 9.398;           // 0.37 inches
    member.L_m = 14.4526;          // 569 inches
    member.material = MATERIALS[0]; // A36 Steel
    member.damping_ratio = 0.01;   // 1%
    member.calculate_properties();

    // Get boundary condition
    const BoundaryCondition& bc = BOUNDARY_CONDITIONS[bcIndex];

    // Analyze mode
    ModeResult result = analyze_mode(member, bc, mode);

    // Print results
    std::cout << "Calculated: ω=" << std::fixed << std::setprecision(2) << result.omega
              << " rad/s, V=" << result.V_critical_ms << " m/s, UDL="
              << result.distribution.udl_kN_m << " kN/m\n\n";

    // Test each value
    bool omega_pass = isClose(result.omega, expected_omega);
    bool V_pass = isClose(result.V_critical_ms, expected_V);
    bool UDL_pass = isClose(result.distribution.udl_kN_m, expected_UDL);
    bool M_pass = isClose(result.distribution.max_moment_kNm, expected_M);
    bool stress_pass = isClose(result.distribution.max_stress_MPa, expected_stress);

    std::cout << (omega_pass ? "[PASS]" : "[FAIL]") << " ω: " << result.omega << " vs " << expected_omega << "\n";
    std::cout << (V_pass ? "[PASS]" : "[FAIL]") << " V: " << result.V_critical_ms << " vs " << expected_V << "\n";
    std::cout << (UDL_pass ? "[PASS]" : "[FAIL]") << " UDL: " << result.distribution.udl_kN_m << " vs " << expected_UDL << "\n";
    std::cout << (M_pass ? "[PASS]" : "[FAIL]") << " M: " << result.distribution.max_moment_kNm << " vs " << expected_M << "\n";
    std::cout << (stress_pass ? "[PASS]" : "[FAIL]") << " σ: " << result.distribution.max_stress_MPa << " vs " << expected_stress << "\n\n";

    totalTests += 5;
    if (omega_pass) totalPassed++;
    if (V_pass) totalPassed++;
    if (UDL_pass) totalPassed++;
    if (M_pass) totalPassed++;
    if (stress_pass) totalPassed++;
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Torsor Vortex Shedding Test Suite\n";
    std::cout << "Validating against Excel reference\n";
    std::cout << "Table 1: No Axial Force\n";
    std::cout << "========================================\n\n";

    int totalPassed = 0;
    int totalTests = 0;

    // Test Case 1: Excel "Mode 1 (fixed-fixed)" → Actually Fixed-Hinged
    runTestCase("TEST 1: Excel 'fixed-fixed' (actually Fixed-Hinged), Mode 1", "Fixed-Hinged", 4, 1,
                52.21, 16.89, 18.73, 489.02, 425.77,
                totalPassed, totalTests);

    // Test Case 2: Excel "Mode 1 (fixed-hinged)" → Actually Fixed-Fixed
    runTestCase("TEST 2: Excel 'fixed-hinged' (actually Fixed-Fixed), Mode 1", "Fixed-Fixed", 2, 1,
                75.95, 24.56, 39.62, 1034.62, 900.80,
                totalPassed, totalTests);

    // Test Case 3: Fixed-Hinged, Mode 2
    runTestCase("TEST 3: Fixed-Hinged BC, Mode 2", "Fixed-Hinged", 4, 2,
                169.52, 54.82, 197.40, 1718.31, 1496.06,
                totalPassed, totalTests);

    std::cout << "\n========================================\n";
    std::cout << "Table 2: With Axial Load\n";
    std::cout << "(Small axial effect - values ~1% different)\n";
    std::cout << "========================================\n\n";

    // Test Case 4: Fixed-Hinged M1 with axial (from Table 2, Column 1)
    runTestCase("TEST 4: Fixed-Hinged BC, Mode 1 (with axial)", "Fixed-Hinged", 4, 1,
                52.29, 16.92, 18.81, 491.15, 427.62,  // ω from f=8.33Hz
                totalPassed, totalTests);

    // Test Case 5: Fixed-Fixed M1 with axial (from Table 2, Column 2)
    runTestCase("TEST 5: Fixed-Fixed BC, Mode 1 (with axial)", "Fixed-Fixed", 2, 1,
                76.07, 24.61, 39.79, 1039.12, 904.72,  // ω from f=12.11Hz
                totalPassed, totalTests);

    // Test Case 6: Fixed-Hinged M2 with axial (from Table 2, Column 3)
    runTestCase("TEST 6: Fixed-Hinged BC, Mode 2 (with axial)", "Fixed-Hinged", 4, 2,
                169.65, 54.85, 197.61, 1720.18, 1497.69,  // ω from f=27.00Hz
                totalPassed, totalTests);

    // Summary
    std::cout << "========================================\n";
    std::cout << "SUMMARY: " << totalPassed << "/" << totalTests << " tests passed\n";
    std::cout << "========================================\n";

    return (totalPassed == totalTests) ? 0 : 1;  // Return 0 if all tests pass
}
