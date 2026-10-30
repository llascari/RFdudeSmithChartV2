# Deployment and public-release guide

This is the maintainer's guide to making RFdude Smith Chart V2 downloadable
on Windows, macOS, and Linux. It separates existing functionality from proposed
release work. The original V2 source is licensed under MIT; this guide does not
enable publishing.

**Recommended starting point:** publish versioned downloads on GitHub Releases:
a Windows x64 setup installer, separate Intel and Apple Silicon macOS DMGs,
and a Linux x64 Debian installer.
Public macOS downloads should be signed and notarized; Windows signing is also
recommended. Finish the licensing and clean-machine checks below before release.

## 1. What Qt deployment means

Qt is the desktop framework providing windows, dialogs, drawing, and input.
This project uses **Qt Widgets with C++**, not a web server, browser app, or
Python program. End users should not need a compiler, CMake, or a Qt SDK.
There is no hosting service to operate beyond hosting the download files.
LTspice is optional and is not bundled.

There are three separate steps:

1. **Build:** compile the application for a particular OS and CPU architecture.
2. **Deploy/package:** collect the executable, Qt shared libraries, plugins,
   other required runtime dependencies, and notices into a distributable.
3. **Publish:** upload tested, versioned packages with installation instructions.

Copying only the executable from the build directory is generally insufficient.
Qt loads some plugins at runtime, including its platform/window-system plugin.
Keep the complete packaged directory structure together.

The project already uses CMake/CPack and Qt's
[`qt_generate_deploy_app_script`](https://doc.qt.io/qt-6.8/qt-generate-deploy-app-script.html).
With `RFDUDE_BUNDLE_QT=ON`, installation collects runtime dependencies;
CPack runs that installation into a package staging area and creates an archive
or disk image. You do not need to run `windeployqt` or `macdeployqt` separately
for the current unsigned packaging path.

Use native builds: Windows on Windows, macOS on macOS, Linux on Linux.
One source tree does not mean one executable runs on all three.

## 2. What is already implemented

See [CMakeLists.txt](CMakeLists.txt) and the
[build workflow](.github/workflows/build.yml).

| Area | Current repository behavior |
|---|---|
| Application version | `0.1.0`, set in the CMake `project()` declaration |
| CI Qt version | Pinned to Qt `6.8.3`; not automatically upgraded |
| Windows job | Windows 2022 runner, x64 MSVC 2022 Qt kit, Inno Setup `.exe` installer |
| Linux job | Ubuntu 22.04 runner, x64 Qt kit, `.deb` package |
| macOS jobs | Temporarily disabled to conserve Actions usage; local build/package support remains available |
| CI triggers | Pushes to `main` or `v*` tags, pull requests, and manual dispatch |
| CI downloads | Per-platform workflow artifacts retained for 30 days |
| Windows after test failure | A successful build is still packaged; installer artifact name ends in `-tests-failed` and is not CI-verified |
| Public releases | No GitHub Release publishing; version-tag pushes build artifacts only |
| Signing | No configured Windows signing or macOS Developer ID signing/notarization |
| Updates | No automatic updater; users install a newer download manually |

Workflow artifacts are temporary CI outputs, not the public download page.
They require GitHub sign-in and appropriate repository access. If the repository
becomes public, its CI outputs are not inherently private.
GitHub Releases provide a more suitable versioned download location.
The workflow is enabled in the repository. A Windows artifact marked
`-tests-failed` is provided for early testing only; use a Windows artifact
without that suffix for a build that passed the Windows test job.
Fork owners can enable and manually run the reusable workflow without a
Windows development machine; see [GITHUB_ACTIONS.md](GITHUB_ACTIONS.md).

**macOS architecture warning:** runner labels can change. Before restoring
macOS CI, verify runner availability, set `CMAKE_OSX_ARCHITECTURES` explicitly,
and inspect the packaged executable architecture. The previous jobs used
`macos-14` for Apple Silicon and `macos-15-intel` for Intel.

### Planning: release downloads and Actions cost

For local installer downloads, run from the repository root with Python 3 and
an authenticated GitHub CLI:

```sh
python3 tools/download-installers.py RUN_ID
```

The run must have successful Linux and Windows jobs. The default destination is
`~/Downloads/RFdudeSmithChartV2/YYYY-MM-DD-NN/`, using the local download date
and the next ascending, unused two-digit daily sequence (01 through 99).
Existing folders are never overwritten. Use `--destination` to override the
root. Both installers and a `build-info.json` file containing the source
commit, CI URL, and SHA-256 checksums are saved together.

Keep distributable packages out of the source tree and Git history. Attach
versioned Windows, macOS, and Linux packages to a GitHub Release instead; the
published release follows repository access, so its downloads remain restricted
while the repository is private and become public if the repository is made
public. Actions workflow artifacts are temporary build/test downloads, not
permanent release assets; this workflow currently retains them for 30 days.

GitHub Actions does not use Copilot credits. Standard GitHub-hosted runners are
free for public repositories. On private repositories, runner usage draws on
the account's included monthly compute amount and may be billed after that is
used, depending on the plan and billing settings. As of October 2026, published
standard-runner rates are approximately $0.006/minute for Linux x64,
$0.010/minute for Windows x64, and $0.062/minute for macOS. For scale, one
10-minute job on each of the three operating systems is roughly $0.78 before
any included allowance; GitHub rounds job time up to whole minutes. Artifact
storage is a separate allowance from runner compute.

These are estimates, not a quote; rates, plan allowances, and billing rules
can change. Check
[GitHub Actions billing](https://docs.github.com/en/actions/concepts/billing-and-usage)
and [runner pricing](https://docs.github.com/en/billing/reference/actions-runner-pricing)
for current details before enabling paid overage. Building and uploading
packages does not by itself publish a release: a future release workflow can
attach all successful platform packages to a draft release for review before
publication.

## 3. Build a distributable manually

Run commands from the repository root. Use a fresh `build/package` directory
for each platform/architecture/Qt kit; do not reuse a cache for a different kit.
These commands produce **unsigned development packages**, not finished releases.
They assume the required compiler and Qt kit are installed.

The project minimum is CMake 3.21 and Qt 6.5; for release reproducibility use
the CI's Qt 6.8.3 kit and a compatible CMake version. Review Qt security fixes
and platform support before choosing the Qt version for a public release.

### Windows x64

Install Visual Studio 2022 or Build Tools with **Desktop development with C++**
and a Windows SDK, CMake, Inno Setup 6, and the Qt 6.8.3 **MSVC 2022 64-bit** kit.
Run in a VS 2022 developer terminal. Replace the Qt path below with your kit:

```powershell
cmake -S . -B build/package -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64" -DRFDUDE_BUNDLE_QT=ON
cmake --build build/package --config Release --parallel 2
ctest --test-dir build/package -C Release --output-on-failure
cpack --config build/package/CPackConfig.cmake -C Release -B build/packages
```

After testing, deploy to a staging directory and build the installer:

```powershell
$stage = "$PWD/stage/windows"
cmake --install build/package --config Release --prefix $stage
iscc "/DAppStage=$stage" "/DAppVersion=0.1.0" packaging/windows/RFdudeSmithChartV2.iss
```

Distribute the generated `*-setup.exe` from `packages/`. It installs the app,
creates a Start Menu entry, offers a desktop shortcut, and registers an
uninstaller. The CI installs Inno Setup and performs these steps automatically.
Qt deployment does not by itself prove every dependency is present.

### Linux x64

Use Ubuntu 22.04 as the current packaging baseline, with a C++20 compiler,
CMake, the official Qt 6.8.3 `gcc_64` kit, and `patchelf`. The workflow lists
additional build/display-library packages. Ubuntu 22.04's default distro Qt is
older than the project's Qt minimum; do not assume `apt install qt6-base-dev`
on that release supplies the required version.

Replace the Qt path:

```sh
cmake -S . -B build/package -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/gcc_64" -DRFDUDE_BUNDLE_QT=ON
cmake --build build/package --parallel 2
ctest --test-dir build/package --output-on-failure
cpack --config build/package/CPackConfig.cmake -C Release -B build/packages
python3 -m unittest discover -s tests -p LinuxPackageTests.py
python3 tools/check_linux_package.py build/packages/*.deb
```

The validation command needs `xvfb-run` and `xauth`. It extracts rather than
installs the package, rejects files outside the private runtime and approved
desktop/data locations, rejects maintainer scripts and other unexpected control
entries, checks Qt library resolution for the executable and every bundled
shared library/plugin, and launches the packaged
command under Xvfb. CI runs this check before uploading Linux installers.

After deployment, the build sets relative runtime search paths on the private
copies of shared libraries/plugins. This also supports distribution Qt builds
whose libraries lack the relative paths provided by official Qt kits; no host
Qt files are modified. A package that fails validation must not be copied to
the installer drop folder or distributed.

Bundled Linux packages must keep all Qt runtime files, including `qt.conf`,
inside `/usr/lib/rfdudesmithchartv2`. `/usr/bin/RFdudeSmithChartV2` is only a
launcher; desktop integration and examples remain under `/usr/share`.
Never install a global `/usr/bin/qt.conf`, Qt libraries in `/usr/lib`, or
plugins in `/usr/plugins`. The original v0.1.0 Linux release did so and can
break KDE/Dolphin; do not distribute or install that asset. The corrected
Debian revision is `0.1.0-1`. The published Linux asset and checksums were
replaced after CI validation and successful testing on both affected Kubuntu
26.04 LTS computers, including subsequent uninstall. The original source tag
and Windows installer are unchanged; the release notes identify the corrected
Linux build commit and provide recovery instructions. This does not constitute
exhaustive clean-machine compatibility testing.
On affected machines, remove `rfdudesmithchartv2`, run
`sudo ldconfig`, and restart the desktop/reboot. Do not remove system Qt
packages or use `autoremove` as part of this recovery.

Distribute the resulting `.deb` from `build/packages/`. On Ubuntu, double-click
the package to open the software installer, authenticate, then launch RFdude
Smith Chart V2 from the applications menu. Bundling Qt does not bundle an
entire Linux OS. glibc, display services, graphics drivers, and some system
libraries remain host requirements. Building on a newer distribution can
introduce dependencies unavailable on older ones. Test the actual package on
every advertised distribution/version, not just the build host.

The application embeds a PNG copy of the launcher artwork for running windows,
including launches directly from the executable. It also sets the desktop-entry
ID `rfdude-smith-chart-v2`; the installed launcher declares the matching X11
window class so desktop panels can associate running windows with it.
After upgrading, close any old running instance and relaunch. If a pinned
launcher still shows a stale icon, remove and re-pin it from the applications
menu.

The artwork source is
[packaging/linux/rfdude-smith-chart-v2.svg](packaging/linux/rfdude-smith-chart-v2.svg).
When changing it, regenerate the embedded PNG with `rsvg-convert` (librsvg):

```sh
rsvg-convert --width 256 --height 256 \
  --output packaging/linux/rfdude-smith-chart-v2.png \
  packaging/linux/rfdude-smith-chart-v2.svg
```

The PNG is checked in; building or installing does not require librsvg or a Qt
SVG plugin.

### macOS

Install Xcode with command-line tools, CMake, and the Qt 6.8.3 macOS kit.
Build on a Mac with a matching architecture for straightforward build/test runs.
For Apple Silicon:

```sh
cmake -S . -B build/package -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/macos" \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DRFDUDE_BUNDLE_QT=ON
cmake --build build/package --parallel 2
ctest --test-dir build/package --output-on-failure
cpack --config build/package/CPackConfig.cmake -C Release -B build/packages
```

On Intel, use `-DCMAKE_OSX_ARCHITECTURES=x86_64` and a separate build directory.
The Qt kit and all dependencies must contain the requested architecture.
Distribute the resulting `.dmg`; it contains the application bundle.
Inspect the built executable with the command below, and repeat the check on
the app extracted from the final DMG:

```sh
lipo -archs build/package/RFdudeSmithChartV2.app/Contents/MacOS/RFdudeSmithChartV2
```

Separate Intel/Apple Silicon downloads are easier to establish initially.
A universal app is a later option: build with
`-DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"`, ensure every bundled binary supports
both architectures, and test on both kinds of Mac.
Choose an explicit `CMAKE_OSX_DEPLOYMENT_TARGET` compatible with the Qt kit and
SDK, then validate that minimum macOS version. Do not equate the CI host OS
with the application's minimum supported OS.

## 4. Platform-specific release work and user experience

### Windows

**Initial installation experience:** download and open the Windows x64
`*-setup.exe`, follow the short installer prompts, then launch from the Start
Menu or desktop shortcut.

- Verify the Microsoft Visual C++ runtime on a clean Windows installation.
  If needed, direct users to Microsoft's official x64 redistributable or
  include its supported installation in a later installer. Audit what Qt's
  deployment tool actually copies; do not collect arbitrary DLLs from a
  development machine. See the [runtime guidance](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170).
- For public releases, sign the application and any installer using a trusted
  Windows code-signing certificate or signing service, with timestamping.
  Signing identifies the publisher but does not guarantee SmartScreen will
  never show a reputation warning. Do not make disabling protection part of
  normal installation instructions.
- Sign the final deployed EXE before archiving; sign an installer after building
  it. Check signatures and test the download on another machine.
- The current Inno Setup installer provides Start Menu and desktop shortcuts
  and a Windows uninstaller. Start without file associations or automatic
  updates unless there is demand.

### macOS

**Intended release installation experience:** download the matching DMG, open
it, and drag the app into **Applications**. A DMG is a disk image, not inherently
a signed/notarized installer or a guarantee of Gatekeeper acceptance.
The existing CPack image layout may need customization for a polished
drag-to-Applications presentation.

For normal public distribution outside the Mac App Store:

1. Obtain Apple Developer Program membership and a **Developer ID Application**
   signing identity.
2. Deploy Qt and finish all modifications to the app bundle.
3. Sign nested code and the app in the correct inside-out order, with hardened
   runtime and the entitlements actually required. Do not rely on a blanket
   `codesign --deep` signing command as a release strategy.
4. Create the final DMG, sign it as appropriate, submit it with `xcrun notarytool`,
   wait for acceptance, and staple the ticket to the distributable.
5. Validate signing/notarization and test the browser-downloaded DMG on a clean
   Mac, including a Gatekeeper assessment and launch.

This requires additional packaging/signing automation: simply signing a build
directory and rerunning the current CPack command may redeploy/modify the app.
The release pipeline must sign the final staged bundle before creating the DMG.
Use [Apple's notarization procedure](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
and [Qt's macOS deployment guidance](https://doc.qt.io/qt-6.8/macos-deployment.html).
Unsigned downloads can be blocked; bypassing Gatekeeper is not the target UX.

### Linux

**Initial installation experience:** on Ubuntu, double-click the `.deb` and
choose **Install** in the software installer. After authentication, launch the
app from the applications menu; remove it from the software manager when done.

- Test in a graphical desktop session, including X11 and Wayland/XWayland where
  advertised. Passing offscreen tests does not validate window-system plugins.
- Document any remaining distro package prerequisites from clean-machine tests.
  An `xcb` plugin error can mean missing dependencies, not a missing plugin file.
- For a managed desktop distribution channel, consider **Flatpak/Flathub**.
  It adds manifests, sandbox/portal testing, and a release process.
- The current `.deb` targets Ubuntu 22.04 and compatible newer Debian-based
  distributions. It is not a universal Linux installer; RPM-based systems need
  a separate package and validation.

## 5. Publish the first downloadable version

Start with a **manual draft GitHub Release**; automate after the packages and
installation steps have been validated.

1. Finish the license/distribution review below. Decide whether the repository
   and downloads will be public or remain limited to authorized testers.
2. Set the version in [CMakeLists.txt](CMakeLists.txt), commit the release
   changes, and use an immutable version tag such as `v0.1.0`. Pushes of `v*`
   tags run the build workflow and upload artifacts from that exact commit.
   This does not publish a GitHub Release; create one manually and attach the
   tested packages, or add a separately reviewed release-publishing job later.
3. Require passing tests and clean-machine checks for every advertised platform.
   Apply platform signing/notarization before computing final checksums.
4. Download CI artifacts while retained. Unpack GitHub's outer artifact ZIP;
   the Windows setup `.exe`, Linux `.deb`, and macOS DMG inside are the actual
   distributables.
5. Rename assets to identify version, OS, and CPU, for example:

   ```text
   RFdudeSmithChartV2-0.1.0-windows-x64-setup.exe
   RFdudeSmithChartV2-0.1.0-macos-arm64.dmg
   RFdudeSmithChartV2-0.1.0-macos-x64.dmg
   RFdudeSmithChartV2-0.1.0-linux-x64.deb
   SHA256SUMS.txt
   ```

   These are recommended release names, not the current default CPack names.
   Generate SHA-256 checksums from the final assets (`sha256sum` on Linux,
   `shasum -a 256` on macOS, `Get-FileHash -Algorithm SHA256` on Windows).
6. On GitHub, open **Releases -> Draft a new release**, select the tag, attach
   all tested assets and checksums, and mark the first prototype a **pre-release**.
   Include supported/tested OS versions, CPU choices, installation steps,
   signing status, prerequisites, known limits, and changes since the last version.
7. Publish only when the complete set is ready. Link the README to the release
   page, not to an expiring Actions artifact. GitHub's automatically generated
   "Source code" archives are not ready-to-run applications.

Public users can download public Release assets without building the source.
Private repository releases remain access-controlled. GitHub Pages or a
separate website can be added later; neither is needed for the initial downloads.

### Subsequent automation

Add a dedicated release workflow, keeping PR builds separate:

- Trigger on version tags and optionally manual dispatch; verify the tag agrees
  with the CMake version. Pin Qt/toolchains and explicit runner architectures.
- Build, test, deploy, sign, and package natively for each supported target.
- Collect all platform assets in a final job that creates/updates a **draft**
  release, with checksums and release notes. Do not publish a partial release
  if a platform job fails.
- Grant `contents: write` only to the release publishing job. Keep signing keys
  and tokens in protected secrets/environments, never in the repository.
  Require approval for signing/publishing and do not expose secrets to PR builds.
- Keep manual approval for publication initially. Retain the exact dependency
  versions, source, build metadata, and symbols needed to diagnose each release.

No signing credentials, paid service, updater, or automated publishing is
introduced by this guide.

## 6. Open-source and redistribution checklist

The original RFdude Smith Chart V2 source code and supplied legacy `.lls`
examples are licensed under MIT (see [LICENSE](LICENSE)). The original
application's source code and third-party material are not covered. Public
source and downloadable binaries are separate decisions.

Before either public-source or public-binary distribution:

- Confirm rights to application code, legacy-derived material, test fixtures,
  names, icons, and any contributed content. Do not assume the reference
  application's permission automatically licenses every asset for redistribution.
- Review the **exact Qt modules, version, and license route** being shipped.
  The current application dynamically links Qt Core/Gui/Widgets; tests also use
  Qt Test. These modules offer LGPL/GPL/commercial options, but not every Qt
  module has the same licensing.
- Under LGPL distribution, dynamic linking is helpful but not sufficient alone.
  Provide the required notices/license texts, a compliant way to obtain the
  corresponding Qt source (including modifications), and the information/rights
  necessary to replace or relink covered libraries. Assess signing and
  installation restrictions as part of this. An upstream source URL alone
  should not be assumed to discharge every source-distribution obligation.
- Inventory all bundled third-party libraries and plugins, not just Qt, and
  distribute their required notices/source as applicable. Existing files in
  [licenses/](licenses/) are a starting point, not a completed dependency audit.
- Confirm final packages actually contain the notices and required compliance
  material. Do not assume CPack or Qt's deployment tools generate all of it.
Before making the repository public, review its full Git history and CI outputs
for credentials, private material, and files lacking redistribution rights.
Making the repository public exposes more than the current checkout.

Qt LGPL use does not by itself require a permissive-licensed application to
become GPL, provided the applicable LGPL obligations are met. Static linking
adds compliance complexity and is not recommended as a deployment shortcut here.
Obtain qualified licensing advice when needed; this is a technical checklist,
not legal advice. See [Qt licensing](https://doc.qt.io/qt-6/licensing.html).

## 7. Release acceptance checklist

For each package, test on a machine/VM without a Qt SDK, developer toolchain,
or developer-specific environment paths:

- [ ] Download the actual candidate asset and verify its checksum.
- [ ] Extract/install as a normal user in a path containing spaces.
- [ ] Launch graphically; verify Windows runtime or macOS Gatekeeper behavior.
- [ ] Verify CPU architecture and minimum advertised OS version.
- [ ] Open a sample project, edit/tune, save, close, and reopen it.
- [ ] Export a PNG and an LTspice schematic; LTspice need not be installed.
- [ ] Verify fonts, dialogs, rendering, and useful high-DPI behavior.
- [ ] Verify bundled dependencies/notices and documented prerequisites.
- [ ] Check replacement/upgrade instructions; project files must not be stored
      in the application folder where replacement could delete them.
- [ ] Confirm the release contains every advertised platform and accurate notes.

Automated tests remain necessary, but they do not prove the downloaded packages
work on clean desktops. Publish a tested support matrix rather than promising
all Windows, all macOS, or all Linux systems.
