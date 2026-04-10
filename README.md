# Torsor — Vortex Shedding Calculator (Windows)

A native Windows desktop tool for analyzing vortex-induced vibration in HSS round members.
Calculates natural frequencies, critical wind speeds, dynamic forces, bending stresses,
and risk levels across six standard boundary conditions, then exports professional Word
reports with the EMS-Tech logo.

---

## Running Torsor on a Windows machine

The release is two files:

| File | Purpose |
| ---- | ------- |
| `torsor.exe` | The application. Statically linked — no installer, no DLLs, no .NET, no admin rights. |
| `image.png`  | EMS-Tech logo. Embedded in the Word reports if it sits next to the .exe. |

**To deploy:** copy both files into the same folder on the target machine and double-click
`torsor.exe`. That's it. Works on Windows 10 and Windows 11.

If `image.png` is missing, the application still runs and reports still generate — they
just won't have the logo at the top.

---

## The interface

```
+----------------------------------------------------------------+
|  Material  Diameter  Thickness  Length     Calculated  Color by |
|  Damping   Axial F   Location              I, A, mu, r          |
|  Wind range (Custom Uniform only):  [V_min] to [V_max] m/s      |
+----------------------------------------------------------------+
|  Mode shape panels (6 boundary conditions, 2 modes each)        |
+----------------------------------------------------------------+
|  Color legend (gradient bar)                                    |
+----------------------------------------------------------------+
|  Wind speed distribution (Weibull or Uniform)                   |
+----------------------------------------------------------------+
|  Risk warning banner                                            |
+----------------------------------------------------------------+
|  Status                              [Print Report...]  [Quit]  |
+----------------------------------------------------------------+
```

### Inputs (top of window)

Edit any field — the analysis recalculates on every keystroke. All units are metric.

| Field | Range / Default | Notes |
| ----- | --------------- | ----- |
| **Material** | A36 / A572-50 / A500 HSS / 316 SS / 6061 Al / Custom | E and ρ from the standard database |
| **Diameter (mm)** | > 1, default 200 | Outer diameter of the HSS round |
| **Thickness (mm)** | > 0.1, default 6 | Clamped to ≤ D/2 |
| **Length (m)** | > 0.1, default 6 | Free span between supports |
| **Damping (ζ)** | 0.0001 – 0.5, default 0.010 | Steel typical 0.005 (bare) to 0.02 (bolted/with attachments) |
| **Axial Force (kN)** | any, default 0 | Positive = tension. Affects natural frequency |
| **Location** | 18 wind climates + Custom Uniform | Drives wind distribution and risk assessment |
| **Wind range** | Only active for "Custom (Uniform Distribution)" | Min/max bounds for the uniform PDF |

The wind range fields are greyed out unless `Location` is set to `Custom (Uniform Distribution)`.

**Calculated** column to the right shows the derived section properties (I, A, μ, r)
recomputed live as you edit.

**Color by** radio group switches the variable used to color the mode shape curves:
UDL, bending moment, or stress.

**Show uncertainty** checkbox: when on, each mode's text line under the plot panels
includes a `[lo-hi]` stress bracket showing the σ range across the standard damping
sweep (ζ from 0.005 to 0.020). Off shows just the nominal value.

### Mode shape panels

Six panels, one per boundary condition: Cantilever, Hinged-Hinged, Fixed-Fixed,
Free-Free, Fixed-Hinged, Hinged-Free. Each shows:

- The first two mode shapes drawn as colored polylines, where the color tracks the
  active "Color by" variable along the beam.
- Mode info text below: `Mode #`, frequency `f`, critical wind speed `V`, dynamic
  force `F`, peak stress `σ` (with optional uncertainty bracket).

The color scale is global across all panels so colors are comparable.

### Color legend

Shows what the active variable is, the color gradient, and the global maximum value.

### Wind speed distribution

Plots the wind PDF for the selected location with vertical lines at the 50th, 75th
and 90th percentiles. Each boundary condition's critical wind speed is drawn as a
solid magenta vertical line — if those magenta lines fall to the left of the
percentile lines you're getting resonance in common winds.

The plot auto-scales to the 99.99th percentile of the local wind, so unreachable
mode 2 critical speeds don't squash the visible part of the distribution.

If the location is `Custom (Uniform Distribution)`, this becomes a flat-top
rectangle PDF between your V_min and V_max.

### Risk warning banner

The colored strip below the wind distribution. Shows the worst-case warning level
across all six boundary conditions:

| Level | Meaning |
| ----- | ------- |
| `ACCEPTABLE` (green) | Vibration risk within limits |
| `CAUTION` (cyan) | Resonance occurs only in strong winds |
| `WARNING` (yellow) | Resonance possible in common winds |
| `HIGH RISK` (orange) | Resonance in median winds with high stress |
| `CRITICAL` (red) | Stress exceeds 80% of yield |
| `UNREACHABLE` (green) | V_critical above 99.99th percentile of local wind — mode cannot be excited |

A mode is auto-classified as `UNREACHABLE` when the wind speed needed to excite it
exceeds the 99.99th percentile of the local wind climate. This prevents secondary
modes that would require wind speeds the location never produces from triggering
false-positive flags.

### Navigation

| Action | Shortcut |
| ------ | -------- |
| Move between input fields | `Tab` |
| Scroll the view | Mouse wheel, `PgUp` / `PgDn` / `Home` / `End`, or scroll bar |
| Open print dialog | Click `Print Report...` |
| Exit | Click `Quit` or close the window |

---

## Generating reports

Click **Print Report...** to open the section-selection dialog (STAAD-style):

```
Sections to include in report:
  [x] Title page with EMS-Tech logo
  [x] Input parameters and member geometry
  [x] Calculated section properties
  [x] Detailed results table (all BCs, both modes)
  [x] Damping uncertainty table
  [x] Risk assessment per boundary condition
  [x] Engineering recommendations
  [x] Mode shape plots (rendered as image)
  [x] Wind speed distribution plot (rendered as image)

Output format:
  (o) Word document (.docx)
  ( ) Plain text (.txt)

                          [Generate Report...]  [Cancel]
```

Uncheck anything you don't want, pick a format, click `Generate Report...`. A
standard Save dialog opens with a default filename like
`vortex_report_20260410_153045.docx`.

### Word documents (.docx)

- Real Office Open XML — opens in Word, LibreOffice, Google Docs, etc. without
  any conversion or compatibility warnings.
- Embeds the EMS-Tech logo at the top.
- Includes the data tables you selected, formatted with borders.
- Embeds the live mode shape and wind distribution plots as PNG images,
  rendered at the moment you click Generate (so the report reflects your
  current inputs exactly).

### Plain text (.txt)

- ASCII report with the same content as the Word version (minus images).
- Useful for quick sharing or version control.
- Honors the same section checkboxes.

---

## Worked example

1. Open `torsor.exe`.
2. Material: `A572-50 Steel`. Diameter: `300`. Thickness: `8`. Length: `12`.
3. Damping: `0.01`. Axial: `0`. Location: `Port of Rotterdam, Netherlands`.
4. Watch the warning banner — if it goes red/orange, you have a resonance issue.
5. Click `Color by → Stress` and look at the mode shape panels — red regions show
   where stress concentrates.
6. Try `Length: 8` and observe how shorter members raise frequency and shift
   critical wind speeds out of the main wind distribution.
7. Click `Print Report...`, leave all sections checked, format `.docx`,
   `Generate Report...`, and save anywhere.
8. Open the saved `.docx` in Word.

---

## Building from source

Required: **MSYS2** with `mingw-w64-x86_64-gcc` (g++ 14 or newer). The code uses
nothing beyond Windows built-in libraries (gdi32, gdiplus, comctl32, comdlg32,
ole32, shlwapi).

```cmd
build.bat          :: builds torsor.exe (statically linked, ~3 MB)
build_test.bat     :: builds and runs the physics test suite (test_vortex.exe)
```

### Manual build command

```cmd
g++ -std=c++20 -O2 -mwindows torsor_win.cpp -o torsor.exe ^
    -lgdi32 -lgdiplus -lcomctl32 -lcomdlg32 -lole32 -lshlwapi ^
    -static -static-libgcc -static-libstdc++ ^
    -Wl,--subsystem,windows
```

### Source layout

| File | Purpose |
| ---- | ------- |
| `torsor_win.cpp`   | Windows GUI (Win32 + GDI + GDI+) and DOCX writer |
| `vortex_physics.h` | Physics engine — natural frequencies, vortex shedding, distributions |
| `wind_data.h`      | Wind climate database, Weibull/uniform CDF, risk assessment |
| `vortex.cpp`       | Original mac/linux FTXUI build (kept for reference) |
| `vortex_report.h`  | Original libharu PDF report (unused on Windows) |
| `test_vortex.cpp`  | Physics test suite (30 tests against Excel reference values) |
| `test_docx.cpp`    | Standalone DOCX writer test harness |
| `build.bat`        | Windows GUI build script |
| `build_test.bat`   | Physics test build/run script |
| `image.png`        | EMS-Tech logo embedded in reports |

The physics headers are pure standard library and compile unchanged on Windows,
mac, and Linux. The 30/30 test suite verifies the calculations against Excel
reference values to within 5%.

---

## Troubleshooting

**The window opens but I can't see the bottom panels.**
Use the scroll bar on the right edge, mouse wheel, or `PgDn` to scroll. The
content height is fixed at ~1010 px, so smaller windows scroll.

**The reports come out without a logo.**
`image.png` needs to be in the same folder as `torsor.exe`. Reports still
generate without it — only the title-page logo is missing.

**The Wind range fields are greyed out.**
That's intentional. They're only active when `Location` is set to
`Custom (Uniform Distribution)`.

**A "CRITICAL" warning appears for an unrealistic wind speed.**
It shouldn't — modes whose critical wind speed lies above the 99.99th percentile
of the local climate are now auto-classified as `UNREACHABLE`. If you still see
a false positive, the V_critical is below the 99.99th percentile (i.e. the local
wind really can produce it), and the warning is the genuine engineering signal.

**Word complains the .docx is corrupt.**
Shouldn't happen — the format is verified by python-docx in the test harness.
If it does, run `test_docx.exe` (build with `g++ -std=c++20 -O2 test_docx.cpp -o
test_docx.exe -lgdi32 -lgdiplus -lcomctl32 -lcomdlg32 -lole32 -lshlwapi -static
-static-libgcc -static-libstdc++`) to regenerate `test_output.docx` and inspect
it manually.
