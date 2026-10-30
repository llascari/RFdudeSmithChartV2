# RFdude Smith Chart V2

**Free, unsupported software, provided as-is.** I do not plan to maintain this
project, respond to feedback or support requests, or accept pull requests.

A new cross-platform implementation of Lance Lascari's RFdude Smith Chart
application, built with Qt 6 Widgets and C++20. The legacy application is a
read-only behavior reference, not a build dependency.

## Acknowledgments

Lance Lascari created the original RFdude Smith Chart application and directed
this new V2 project. GitHub Copilot was the principal implementation assistant
for this phase and performed most of the implementation work, including
substantial code, testing, and documentation contributions, under Lance's
direction and review. The V2 project is a new implementation; the original
application's source code is not included. Copilot is an AI tool, not a project
maintainer or copyright holder.

**Status: initial prototype; not full legacy feature parity.** The original
RFdude Smith Chart V2 code and supplied legacy `.lls` examples are licensed
under the [MIT License](LICENSE). The original application's source code and
third-party materials are not included in that license. See [NOTICE.txt](NOTICE.txt)
for attribution, scope, and third-party notices.

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
./build/RFdudeSmithChartV2
```

Open the supplied legacy example directly:

```sh
./build/RFdudeSmithChartV2 examples/legacy-example.lls
```

Open the included RFdude project example:

```sh
./build/RFdudeSmithChartV2 examples/network.rfs_45Mfilter.json
```

Packaged installs include both examples under
`share/RFdudeSmithChartV2/examples` inside the application install location.
Use **File -> Open examples...** to browse them without finding the install
directory yourself. Examples open as templates: **Save** asks for a filename
for your own copy rather than overwriting the bundled file.

Component dialogs use **Tab / Shift+Tab** to cycle between visible input fields,
skipping action buttons. **Return / keypad Enter** accepts the current valid
component, including a selection in an open dropdown. **Cancel** requires a
mouse click; **Escape** does not discard the component edit.

## Build and test

Requirements: CMake 3.21+, a C++20 compiler, Qt 6.5+ Widgets and Test development
packages. Use the same compiler/architecture as the installed Qt distribution.
On Ubuntu 24.04+, the development dependencies are typically:

```sh
sudo apt install build-essential cmake qt6-base-dev qt6-base-dev-tools
```

Configure and build as above, then:

```sh
ctest --test-dir build --output-on-failure
```

For a Visual Studio multi-configuration build, add `--config Release` to the
build command and `-C Release` to CTest. Tests use Qt's offscreen platform; they
do not launch or manipulate the legacy program or touch its repository.

The [GitHub workflow](.github/workflows/build.yml) builds, tests, and packages
Linux x64 and Windows x64 builds using Qt 6.8.3. macOS CI builds, tests, and
packaging are temporarily disabled to conserve Actions usage; local macOS
build support remains available.
Download packages from an Actions run's **Artifacts** section; artifacts are
retained for 30 days and are not published GitHub Releases.
The Windows download is a setup `.exe`; the Linux download is a `.deb` package
that installs into the desktop app menu.
The Windows package may still be uploaded if its build succeeds but UI tests
fail; its `-tests-failed` artifact suffix means it has not passed CI tests.
The workflow is enabled in repository settings.
To run the same installer builds in your own fork, see
[Build installers with GitHub Actions](GITHUB_ACTIONS.md).

## Downloads

Versioned Linux and Windows installers are published on the
[GitHub Releases page](https://github.com/llascari/RFdudeSmithChartV2/releases).
They are unsigned and unsupported; review the release notes before installing.

## VS Code: local desktop building and debugging

This is a desktop Qt/C++ CMake project, not an STM32 or embedded firmware
project. No board, probe, flashing, or debug server is involved.

The checked-in VS Code launch configuration targets Linux with GDB. Install
the dependencies above plus `gdb`, and open the repository root as a folder in
VS Code. Install the recommended **CMake Tools** and **C/C++** extensions
if prompted.
If CMake Tools asks for a configure preset, select **Desktop Debug**.

- **Build:** press **Ctrl+Shift+B**. This configures and builds the Debug preset.
- **Debug:** open Run and Debug, select **Desktop Qt app (GDB)**, and press
  **F5**. It builds first, then launches the desktop app under GDB.
- **Breakpoints:** click beside a source line in the editor before pressing F5.
  For a first test, set one at `MainWindow window;` in `src/main.cpp`.
  When paused, inspect Variables, Watch, and Call Stack; use **F10** to step
  over, **F11** to step into, **F5** to continue, and **Shift+F5** to stop.
- **Run without debugging:** press **Ctrl+F5**.
- **Test:** use **Tasks: Run Task** from the Command Palette, then select
  **CMake: test Debug**. It builds first and runs all five local test suites
  (GUI tests use the offscreen platform).
  CMake Tools also exposes the tests in Test Explorer after configuration.

To open a project at startup while debugging, set `args` in
`.vscode/launch.json` to `["examples/legacy-example.lls"]`. By default it
opens a new document. The application uses your normal desktop display;
only the automated tests use Qt's offscreen platform.

CMake generates the build files; the compiler builds the executable; GDB
runs it with breakpoints. The **Debug** preset includes debug symbols and
uses `build/debug/`, separate from the manual **Release** build in `build/`.
These same local operations can be run in a terminal:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/RFdudeSmithChartV2
```

The presets and VS Code configuration are shared in Git; generated output is
ignored. Windows/macOS require a native debugger launch configuration; the
checked-in F5 configuration is for Linux. Nothing here enables GitHub Actions
or runs remote builds.

## First-milestone functionality

- Impedance/admittance Smith chart with resize-safe, antialiased rendering.
- Series/shunt R, L, C and compound series/parallel RLC elements.
- Lossless through transmission lines and open/shorted shunt stubs.
- Ideal transformers, lossless coax with electrical/physical length input, and
  microstrip elements with a dimension calculator and dielectric loss.
- Frequency-scaled line lengths, highlighted trajectories, endpoint impedances,
  goal marker, cursor inspection, and sampled maximum-Q overlays.
- VSWR, reflection magnitude, return loss, and mismatch loss relative to the
  real chart reference impedance.
- Component insertion after selection, editing, deletion, and undo/redo.
- Bottom schematic pane showing the existing RLC, line, and stub networks,
  with synchronized selection and double-click editing.
- Versioned JSON save/load with atomic writes and unsaved-change protection.
- Read-only import of the supported legacy `.lls` archive layouts.
- PNG export of the chart, complete schematic, and frequency response with network settings, the
  ordered circuit description (connections, values, and resulting impedances),
  and RF metrics.
- Editable LTspice schematic export with ideal components and an AC test source.
- Legacy-style MatchMe and simple Chebyshev filter synthesis wizards, with
  component previews and undoable network replacement.
- Two-port S11/S21/S22 magnitude response, linear/log frequency sweeps, a linked
  analysis-frequency marker, and separate frequency-response PNG export.
- Touchstone 1.x `.s2p` circuit blocks and `.s1p` frequency-dependent loads,
  embedded in projects, with complex interpolation and real-reference conversion.

### Keyboard and mouse

In the component dialog, the active input has a thick accent-colored border
and highlighted text area. Tab and Shift+Tab follow the visible form order:
type, connection, values, and OK/Cancel, wrapping around; arrow keys change
the focused dropdown selection.

| Input | Action |
|---|---|
| `C`, `L`, `R` | Add capacitor, inductor, resistor; choose series/shunt in dialog |
| `T` | Add through line or open/shorted shunt stub |
| `X` | Add compound RLC; choose series/parallel type and tuning parameter |
| `I` | Add ideal transformer (source-side : load-side turns ratio) |
| `K` | Add coaxial cable or coax stub |
| `B` | Open microstrip calculator / add microstrip element |
| Left / Right | Select previous / next component (wraps) |
| Up / Down | Multiply selected value by `1 +/- tuning step / 100` |
| Mouse wheel over chart | Tune selected value, one step per wheel notch |
| Ctrl + Up/Down or wheel | Tune frequency |
| Shift + Up/Down or wheel | Tune real reference impedance |
| `E` / double-click | Edit selected component |
| Delete | Delete selected component |
| Click chart trajectory / table row / schematic component | Select component |
| Right-click inside chart | Inspect impedance; use as starting/goal impedance |
| `S` | Network setup, starting/goal impedance, title, and frequency presets |
| `W` | MatchMe wizard: lumped low-pass/high-pass or quarter-wave transformers |
| Ctrl+W | Simple Chebyshev low-pass/high-pass filter wizard |
| Ctrl+J | Open frequency-response window |
| `F`, `Z`, `P` | Setup focused on frequency, reference impedance, tuning step |
| F1 / F2 / F3 | Recall configured frequency preset |
| `A`, `G`, `Q`, `M`, `V` | Toggle admittance, polar values, Q curves, labels, VSWR circle |
| Ctrl+N / O / S | New / open / save (platform-standard equivalents on macOS) |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo (platform-standard equivalents) |

Values use MHz, ohms, nH, pF, and degrees. A line's electrical length is specified
at its own reference frequency, not necessarily the current analysis frequency.
The ideal shorted-stub trajectory starts at 90 degrees (an open shunt branch)
and moves toward the specified electrical length, matching the legacy concept.
Up then Down is deliberately **not** an exact inverse: the original percentage
semantics are preserved.

Component dialogs open with the primary value selected for typing. Press
**Enter** (or keypad Enter) to accept the component, or **Esc** to cancel.
For compound RLC components, the initially selected field is the tuning
parameter. The network setup dialog also uses OK as its default action.

### Bottom schematic

The schematic shows the source on the left and the starting-impedance load on
the right, consistent with LTspice export. Component order is reversed visually
because Smith-chart transformations start at the load and work toward the input;
component numbers still match the table and chart. The goal is not a termination.
The source resistor equals chart Z0; the load box represents the configured
complex starting impedance at the current analysis frequency.

The default pane is compact: 140 logical pixels per component and 200 high,
leaving more room for the Smith chart. Short value labels omit secondary
parameters; hover for the complete description, or use the component table.
The same compact layout is used in PNG reports, with full parameters retained
in the report's detailed table. A 960-pixel schematic area fits five components
plus source/load on one row; a 1200-pixel area fits six.

Drag the horizontal divider to resize the pane, and use its horizontal scrollbar
for long networks. A vertical scrollbar is available when the pane is too short
to show all labels. Selecting a component in the table or chart, or with the
keyboard, scrolls its schematic into view. Click a schematic component to select
it, or double-click to edit it. Selection alone does not modify the project.
Existing keyboard editing/tuning shortcuts also work with the schematic focused;
the schematic's scrollbars/wheel navigate the view rather than tune values.

Compound RLC symbols show their internal series/parallel topology independently
of their series/shunt connection. The schematic is a view of the existing
network, not a free-form wiring editor. PNG reports include the complete
schematic between the chart and circuit description, independent of the pane's
scroll position. Long schematics wrap into numbered, left-to-right continuation
sections at component boundaries, keeping symbols and values readable rather
than shrinking the entire circuit. Exported schematics use black on white
without selection highlighting. LTspice export uses the same circuit orientation.

## Transformers, coax, and microstrip

### Ideal transformer

Use **Add -> Ideal transformer...** (`I`). The ratio is explicitly
**source-side turns / load-side turns**, shown as `N : 1`. The transformation
is `Zsource = N^2 * Zload`, including complex impedances. Thus `2 : 1` transforms
a 50-ohm load to 200 ohms; `0.5 : 1` transforms it to 12.5 ohms. The model is
lossless, frequency-independent, and has no magnetizing inductance, leakage,
or parasitic capacitance. It is a through element only. Up/Down tunes the turns
ratio; schematic windings have corresponding polarity dots.

### Coaxial cable

Use **Add -> Coaxial cable...** (`K`). Set characteristic impedance and select
electrical length in degrees at a reference frequency, or physical length in
**mm**. Physical length uses either velocity factor `v/c` (default 0.66) or a
dielectric constant with `VF = 1/sqrt(Dk)`. Propagation is nondispersive and
lossless; conductor/dielectric cable attenuation is not modeled.

Through lines and open/shorted shunt stubs are supported. Up/Down tunes whichever
length input mode is active. Electrical and physical inputs are stored separately;
switching modes uses that mode's stored value, not an automatic conversion.
Velocity factor/Dk affects the RF result only in physical-length mode.

### Microstrip calculator and elements

Use **Network -> Microstrip calculator / add element...**, **Add -> Microstrip...**,
or `B`. All dimensions default to **mm**. The initial substrate is **Dk 4.2**,
**1.5 mm** high with **tan(delta) / DF 0.02**; strip width defaults to 3 mm and
length to 10 mm. Substrate height is the distance from the strip to its ground
plane, not necessarily the total thickness of a multilayer PCB.
The resizable calculator separates its inputs, calculated results, and model
limits; preview values use concise precision without reducing input precision.

- Enter width, substrate height, and Dk to calculate characteristic impedance
  and effective Dk immediately.
- Enter a target impedance and click **Calculate width from target impedance**
  to synthesize width. Invalid or unattainable geometries report an error.
- Set physical length, or electrical length at the reference frequency.
  The preview reports electrical length and matched dielectric loss in dB at
  that reference frequency. The target impedance is a calculator input; the
  element's actual impedance always comes from its stored geometry.
- Select through, open stub, or shorted stub; **OK** inserts/applies the element,
  while **Cancel** leaves the network unchanged. Up/Down tunes the active length.
- Geometry, substrate Dk/DF, and both length inputs survive save/reopen.

The impedance/effective-Dk calculation is the **Hammerstad-Jensen quasi-static,
zero-thickness** model for a single strip above an infinite ground plane.
Supported width/height ratios are 0.01-100 and Dk is 1-128. Width synthesis uses
bounded bisection over this range, not a guessed impedance-to-width conversion.

Microstrip includes a **weak dielectric-loss, quasi-TEM propagation model** with
real characteristic impedance. For `q = (effectiveDk - 1)/(Dk - 1)`,
`beta = 2*pi*f*sqrt(effectiveDk)/c` and
`alpha = beta*Dk*q*DF/(2*effectiveDk)` in Np/m. The implementation uses the
equivalent filling factor directly, including the finite limiting case at Dk=1.
The distributed line uses complex propagation `alpha + j*beta`; this affects
both through lines and stubs, rather than adding an arbitrary lumped resistor.
DF must be 0-0.1; zero recovers the lossless model. Matched line attenuation is
`8.68589*alpha*length` dB, not the chart's input mismatch-loss metric.

**Limits:** no copper-thickness correction or conductor/roughness loss, solder
mask, radiation, discontinuity, open-end extension, or frequency dispersion.
Dk and DF are treated as frequency-independent. Use an EM solver or more complete
line model when the quasi-static/weak-loss assumptions are inappropriate.

## Matching and filter synthesis

Both wizards are available in the **Network** menu and on the toolbar. They
follow the original Windows program's MatchMe and simple-filter workflows,
not a general-purpose optimizer. Change the controls to preview the complete
component list and endpoint. **Replace network** explicitly applies the preview;
Cancel/Esc leaves the document unchanged. Replacement is one undoable operation,
including start/goal, title, frequency, and display changes.

### MatchMe (W)

- Choose **Lumped low-pass**, **Lumped high-pass**, or **Quarter-wave transformer**.
- Specify **1-10 sections**, complex starting and goal impedances, and the design
  frequency. Defaults come from network setup, **not the selected component or
  current endpoint**. The existing components will be replaced, not appended.
- Intermediate resistances follow equal-ratio steps. Low-pass sections use
  series L/shunt C; high-pass sections use series C/shunt L. Step-down networks
  reverse the ladder order. Quarter-wave sections use 90-degree lossless lines
  referenced to the design frequency.
- Series elements cancel the starting reactance and add the goal reactance.
  Both resistances must be positive. The preview reports unsupported
  component values or numerical limits and disables replacement.
- Like the original, synthesis enables Q curves and the chart-Z0 VSWR circle.

Unlike the old equal-resistance example fallback, this wizard never changes
the requested starting resistance to invent a match. Already identical
impedances need no components; equal resistances with differing reactances
need only reactance compensation. All nonzero reactances are accounted for,
rather than using the old 2%-of-resistance compensation threshold.

### Simple filter (Ctrl+W)

- Choose **Chebyshev low-pass or high-pass**, **shunt-first or series-first**,
  ripple cutoff in MHz, passband ripple in dB, order, and termination resistance.
- As in the original, requested orders are **3-15**; even values round **up to
  the next odd order**, visibly shown in the preview. Ripple range is
  **0.0001-10 dB**, defaulting to **0.1 dB**. The default is a third-order,
  shunt-first low-pass.
- Synthesis sets real starting/goal impedances and chart Z0 to the specified
  resistance. Elements alternate series/shunt starting at the starting
  impedance; the odd-order equal-termination ladder is symmetric.
- The analysis frequency is explicitly set to the ripple cutoff, and the
  title records the effective order, response, cutoff, and ripple. The original
  left analysis frequency unchanged when a different cutoff was entered.
- The calculation uses the legacy Chebyshev prototype recurrence with an exact
  dB conversion instead of its rounded `17.37` constant.

Ripple/cutoff describe **matched-source/load insertion loss**, not the chart's
mismatch-loss metric. These are ideal LC ladders: there is no finite-Q loss
model, band-pass/band-stop transform, Butterworth/Bessel selection, or automatic
value rounding. Generated components remain editable and can be saved, tuned,
or exported to LTspice like manually entered networks.

## Frequency response

Open **Network -> Frequency response...** (`Ctrl+J`), the toolbar action, or
**View -> Frequency response...**. The nonmodal window stays open while editing
the circuit. S11 is red, S21 blue, and S22 dashed green; coincident traces overlap.
All existing elements, including transformer ratios and microstrip dielectric
loss, are included.

- Set start/stop in **MHz**, 2-2001 points, linear/log frequency scale, and a
  real reference impedance shared by both ports; click **Apply sweep**.
- Port 1 is the source/input end; port 2 is the load end. The component order
  matches the source-to-load schematic, not the load-to-input table ordering.
- The starting impedance, goal impedance, and schematic source resistor are
  **external terminations**, not included in the plotted two-port.
- S11 assumes port 2 is terminated in the selected port reference. It equals
  the Smith chart's endpoint reflection only when the chart's starting load
  and chart reference both match that port reference. S22 similarly assumes
  port 1 is matched. S21 is transmission under these matched-port conditions,
  not the chart's mismatch-loss metric.
- The port reference initially copies chart Z0, then remains independent.
  Editing chart Z0 does not silently change the response reference.
- Click the plot to set the main analysis frequency. The Smith chart, table,
  and metrics update through the normal undoable document operation. Conversely,
  tuning or recalling the main frequency moves the response marker. Marker
  values are computed at the exact frequency, not interpolated from sweep samples;
  an out-of-range marker is reported explicitly without moving the sweep limits.
- Circuit edits, undo/redo, and new/open update an open response window.
  Selecting a component or changing plot controls does not modify the circuit.
  Closing/reopening the window retains controls during this application session;
  sweep settings are not saved in project JSON.
- **Export plot PNG...** writes the plot, title, applied range/port settings, and
  marker readout. Pending, unapplied controls do not appear in the report.
  **File -> Export chart / schematic / response PNG...** combines the Smith
  chart, full schematic, response plot, complete component table, and metrics
  in one image. It uses the same applied response settings, including when the
  response window is hidden. Before the first response view/export, a default
  sweep is created. Invalid response settings report an export error rather
  than silently omitting the plot; apply a valid sweep in the response window.
- **Terminated input: S11 only** includes the starting/measured load and plots
  the circuit's input reflection. S21/S22 are not defined in this view and are
  hidden, not fabricated. This is the default when importing a one-port load.
  Select **Two-port circuit: S11/S21/S22** to exclude that load again.

The plot shows `20*log10(|S|)` with a **-100 dB** floor, clipping deeper nulls only
visually. Its upper limit is normally 0 dB and expands for measured gain.
Exact zero is shown as `-inf dB` in the marker readout. The engine
retains complex S11/S21/S12/S22 internally, but this first UI shows magnitude
only, not phase or group delay. Invalid sweeps clear stale traces and display
an error. A finite sample grid may miss narrow resonances; increase point count
or narrow the range as needed.

The engine cascades element scattering matrices rather than multiplying raw
ABCD matrices, avoiding overflow for long attenuating cascades and supporting
exactly blocking stubs. Numerical tests compare known transformer/line results,
matched-line dielectric attenuation, analytic Chebyshev insertion loss,
reciprocity/passivity, and the existing terminated-impedance calculation.

## Touchstone measurements

Use **File -> Import Touchstone...** or **Network -> Import Touchstone block /
load...**. The import is undoable and opens the frequency-response view.

- A `.s2p` file inserts a measured two-port after the selected table component.
  The schematic shows a box with **port 1 toward source/input** and **port 2
  toward the load**, plus its filename. S12 is retained separately from S21;
  active and nonreciprocal data are not forced to be passive or reciprocal.
  Cascading and terminated Smith-chart calculations use the measured matrix.
  Active impedances may lie outside the Smith-chart unit circle; VSWR/mismatch
  loss and sampled-Q values are marked undefined where their passive definitions
  do not apply. This is not a stability analysis.
- A `.s1p` file replaces the starting load with its measured S11. Existing
  circuit components are retained. The load appears as **S1P** in the schematic.
  Use **Network -> Use constant starting load**, or uncheck **Use imported
  one-port load** in Setup, to return to the stored constant start impedance.
  Using a chart point as the starting impedance also removes the measured load.
- Accepted files use Touchstone **1.x** S data, **RI**, **MA**, or **DB** pairs,
  Hz/kHz/MHz/GHz, and one equal, real positive reference resistance.
  Comments and wrapped records are supported; two-port order is
  **S11, S21, S12, S22**. Standard omitted-option defaults are GHz/S/MA/R50.
  Version-2 keyword blocks, mixed-mode/multiport data, noise data, and non-S
  formats are rejected explicitly rather than guessed. Limits: 16 MiB per file,
  100,000 samples, strictly increasing positive frequencies.
- Interpolation is linear in **complex Cartesian S values versus frequency**,
  before conversion to the chosen real port reference. There is **no
  extrapolation**. Import adjusts analysis frequency into the datasets' common
  range; disjoint ranges fail without changing the document. Existing sweep
  limits are narrowed to the common range when datasets change. Unapplied
  controls do not override the applied sweep.
- Single-frequency data is supported: it displays one measured point, not an
  extrapolated curve. A dashed Smith-chart connector joins a measured block's
  input/output impedance endpoints; it is not a fractional physical trajectory.
- Dataset name, reference, range, and sample count are shown in full in tooltips
  and report descriptions. All complex samples are embedded in saved projects;
  the original files are not needed to reopen them. Measured values are read-only
  and not component-tunable; delete/reimport to replace a block. Synthesis
  replacement removes the measured load along with the old network.
- LTspice export of measured blocks/loads is deliberately rejected; there is no
  silent ideal-element substitution. PNG export supports them. There is no
  separate measured-data overlay mode, port reversal, phase/group-delay UI, or
  broadband rational fitting in this implementation.

## Project files and legacy compatibility

Save new projects as `.rfs.json`. Format version 2 contains explicit component
types, connections, units in field names, display settings, frequency presets,
transformer ratio, cable input modes, and microstrip geometry/substrate/loss.
Version 1 files still load with the original element behavior; saving upgrades
them to version 2, which older builds cannot open.
Projects containing measured blocks or loads use **version 3**, embedding their
samples; version 1/2 projects remain readable. Projects without measured data
continue to write version 2. Older builds cannot read version 3.
Unknown versions and invalid values are rejected with an error; failed opens do
not replace the current document. Saves use a temporary file and atomic commit.

Legacy `.lls` files are import-only. The importer supports the 2003-era layout
with reserved fields and the 2006/2007 extension with three frequency presets.
It supports ANSI/UTF-16 MFC strings and ignores unused uninitialized component
fields. The supplied original example is exercised by automated tests. Other
historical layouts are rejected rather than guessed; Mathcad `.mcd` files are
not RFdude project files. Imported files always require Save As in the new format.

### LTspice export

Choose **File -> Export LTspice schematic...** to save an editable `.asc` file,
then open that file in LTspice if it is installed.
Export does not launch LTspice, save the project, or clear its modified state.
LTspice is not required to build the application or export a schematic, and
no LTspice binaries or symbol files are bundled.

The schematic uses standard LTspice resistor, capacitor, inductor, voltage-source,
and ideal transmission-line symbols. Series/shunt connections and compound RLC
topologies are preserved. Component instance numbers match the chart's list;
compound elements become individually editable R/L/C parts with the same number.
Transmission-line delay is calculated from each line's own length/reference
frequency. Zero-length through lines become wires, open stubs are omitted, and
shorted stubs become ground connections.

Ideal transformers export as a standard dependent-voltage/current-source pair,
enforcing both the voltage ratio and the inverse current ratio without an
approximate magnetizing inductance. Coax exports using its derived impedance
and delay. Microstrip exports as a transmission line plus a matched symmetric
resistive T attenuator whose ABCD matrix matches the dielectric-loss model
**at the exported analysis frequency only**. Geometry/material settings remain
in annotations. A zero-DF microstrip exports without the loss pad.

The diagram runs from the source on the left to the starting-impedance load on
the right. Thus component order is reversed visually: the chart starts at the
load and applies components outward toward the source. The 1 V AC source has a
series resistance equal to chart Z0. The goal impedance is an annotation only.
A complex starting impedance becomes a series R/L or R/C termination that
matches **at the exported analysis frequency**; its behavior away from that
frequency is not a constant complex impedance.

Press **Run** in LTspice, then open **View -> SPICE Error Log** to see `ZinRe`
and `ZinIm`, the real and imaginary endpoint impedance in ohms, excluding the
source resistance. `GammaRe` and `GammaIm` give the reflection coefficient
relative to chart Z0. For an ideal-open endpoint, the schematic omits the
undefined finite-impedance measurements and reports reflection instead.
The included AC analysis runs at the current chart frequency.
You can edit it into a sweep, but update the measurement frequency as appropriate.
For microstrip, the fixed loss pad will not track the model's frequency-dependent
dielectric attenuation away from the exported frequency.
Inductors explicitly use zero series resistance to match the ideal RF model.
These are editable simulation schematics, not PCB netlists or finite-Q models.

Local integration checks have been run with LTspice 26.1.1 and XVII under Wine.
Normal tests do not require Wine or LTspice. To opt into the numerical comparison
tests on Linux after building the Debug preset:

```sh
RFDUDE_LTSPICE_EXE="/path/to/LTspice.exe" ./build/debug/tests/ltspice_tests
```

### Deliberate limits and differences

- No network reversal or printing yet. Touchstone support is limited as above.
  Lumped components remain ideal, with no
  finite-Q model; line loss is currently limited to microstrip dielectric loss.
- Only a **real, positive chart Z0** is supported. Start may be a finite passive
  impedance (including a short); goal must have positive resistance. Component
  R/L/C values must be positive. Unsupported legacy values report an error.
- The VSWR circle and metrics use **chart Z0**, not the old goal-centered circle
  approximation. The goal is displayed separately as a purple cross.
- Mismatch loss is reflection loss, **not insertion loss**.
- Maximum `|X|/R` is sampled along trajectories; it is not an exact optimizer,
  a component quality factor, or a bandwidth prediction. Display curves cap at Q=10.
- Ideal opens/shorts are handled in calculations without dividing by infinity.
  Trajectory samples are bounded (256 to 8192 segments per component); very long
  lines or extreme values may require further rendering refinement.
- Undo history is bounded to 200 changes. There is no autosave recovery yet.

## Packaging

For a step-by-step explanation of Qt deployment, per-platform package creation,
public downloads through GitHub Releases, signing/notarization, and open-source
readiness, see the [Deployment and public-release guide](DEPLOYMENT.md).
It distinguishes existing development packaging from release work still needed.

Normal `cmake --install build --prefix <directory>` installs the executable and
notices; Qt must already be available on that machine. To bundle Qt:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DRFDUDE_BUNDLE_QT=ON
cmake --build build --parallel 2
cpack --config build/CPackConfig.cmake -C Release -B build/packages
```

Bundling on Linux additionally requires `patchelf`. Prefer an official Qt kit
for portable packages; distro Qt can pull in many system libraries. Linux
portability is constrained by the build machine's glibc baseline; building on
Ubuntu 22.04 is intended to support that baseline and newer systems, not every
Linux distribution.

Windows downloads are setup `.exe` installers with Start Menu and desktop
shortcuts; Linux downloads are `.deb` packages that add the application to the
desktop menu. macOS packages are unsigned DMGs. These remain development
artifacts: Windows signing, macOS signing/notarization, clean-machine
validation, and complete dependency-license review remain release work.

## Layout

- `src/RfModel.*`: portable RF model and calculations, no Qt dependency.
- `src/FrequencyResponse.*`: portable complex two-port analysis and frequency sweeps.
- `src/FrequencyResponseDialog.*`, `src/ResponsePlotWidget.*`: response controls,
  linked frequency marker, dB plotting, and response PNG export.
- `src/Synthesis.*`: portable legacy-style matching and filter calculations.
- `src/SynthesisDialogs.*`: synthesis controls, previews, and validation.
- `src/DocumentIO.*`: JSON persistence and bounded legacy archive reader.
- `src/LtspiceExport.*`: editable LTspice schematic generation and atomic export.
- `src/SmithChartWidget.*`: chart rendering and mouse interaction.
- `src/SchematicWidget.*`: scrollable circuit rendering and synchronized selection.
- `src/Editors.*`, `src/MainWindow.*`: desktop UI and document workflow.
- `tests/`: calculation, archive, and Qt interaction/rendering tests.
- `.github/workflows/`: native platform builds and installer packages.
