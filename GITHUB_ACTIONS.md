# Build installers with GitHub Actions

This repository includes a reusable GitHub Actions workflow for building,
testing, and packaging the application on Linux, Windows, and macOS. The
Windows job creates the setup installer, and separate macOS jobs create Intel
and Apple Silicon disk images; you do not need those computers, a local Qt SDK,
or GitHub Copilot.

## Run the workflow from your own fork

1. Sign in to GitHub and fork this repository, or create your own GitHub
   repository containing the project files. A local clone by itself cannot run
   GitHub Actions; the repository must be pushed to GitHub.
2. In your repository, open **Settings -> Actions -> General** and make sure
   Actions are allowed. GitHub commonly disables Actions in newly created
   forks until the owner explicitly enables them.
3. Ensure `.github/workflows/build.yml` is present on the repository's default
   branch. The manual-run control is only available after GitHub has loaded the
   workflow from that branch.
4. Open the **Actions** tab, choose **Build and test**, select **Run
   workflow**, choose the branch to build, and confirm.
5. When the run finishes, open it and download the Linux, Windows, and
   architecture-specific macOS artifacts. The Windows artifact contains the
   setup `.exe`; Linux contains a `.deb`; macOS artifacts contain `.dmg` files.
   A Windows artifact ending in `-tests-failed` was packaged despite a test
   failure and should not be presented as a verified build.

Linux packages are extracted and checked for private Qt runtime paths, then
smoke-tested under Xvfb before upload. macOS builds explicitly set and verify
their CPU architecture. Separate installer jobs then test the actual downloads
on runners without the build job's Qt SDK; see the coverage below.
The original v0.1.0 Linux release
installer is unsafe and has been replaced on the release page with Debian
revision `0.1.0-1`: see the warning in [README.md](README.md).

No secrets, Copilot subscription, or paid signing credentials are required by
the current workflow. A run started in your fork belongs to your fork, not this
upstream repository. If you open a pull request against the upstream
repository, its own workflow may run there according to its settings; that run
belongs to the upstream repository. Standard GitHub-hosted runners are
currently free for public repositories; private-repository usage can draw on
the owner's included Actions allowance or be billed. Artifact storage and
billing policies can change, so check GitHub's current
[Actions billing documentation](https://docs.github.com/en/billing/concepts/product-billing/github-actions)
and your account's Actions settings.

## Automatic runs

The workflow also runs when a commit is pushed to `main`, when a `v*` tag is
pushed, and for pull requests. A fork that uses a different default branch
should update the `push.branches` entry if it wants automatic builds on pushes
to that branch. Manual runs remain available from the workflow's branch
selector.

Artifacts are temporary workflow-run downloads retained for 30 days.
After all four build jobs and all installer jobs pass, builds of `main` (push or manual dispatch)
automatically publish an immutable pre-release tagged
`build-<run-id>-<attempt>`. Version-tag builds publish under the `v<version>` tag,
which must match the CMake project version. Pull requests, other branches, and
any failed build or installer job cannot publish. Diagnostic `-tests-failed` artifacts
are never release inputs.

The publishing job downloads only this run's four verified installer artifacts
(Linux x64, Windows x64, macOS Intel, and macOS Apple Silicon), requires the
complete expected set, and attaches installers, `SHA256SUMS.txt`, and
`build-info.json` with the exact source SHA and run URL. Assets are uploaded
to a draft first, then made public as a pre-release only after all uploads
succeed. Upload failure leaves a draft, not a partial public release. Existing
releases/assets are never overwritten, including the original release's KDE
recovery notes; an existing version release makes publication fail rather than
replace its assets. A rerun on `main` uses a new attempt-specific tag.
Automatic pre-releases are not marked as stable/latest; find them on the
repository's **Releases** page.

Only the publishing job has `contents: write`, using the built-in
`GITHUB_TOKEN`. No personal token or signing secrets are needed. The same
behavior applies within forks once Actions and release-writing permissions
are allowed. These installers remain unsigned and unsupported; the macOS DMGs
are not notarized. Passing CI is not exhaustive clean-machine certification.

## Installer safety gates

These checks operate on the final DEB, setup EXE, and both DMGs, not the build
directory. Installer jobs have read-only repository permissions and bounded
timeouts. Their logs are retained for seven days, including on failure.
Build artifacts are candidates until the entire workflow succeeds; the public
pre-release cannot publish until every installer matrix entry passes.

| Platform | Required checks |
|---|---|
| Linux | Before extraction: package identity/architecture, no package replacement/conflict metadata, approved archive paths/types, root ownership, safe modes, no escaping links or maintainer scripts. Every ELF runtime search path must be relative and private. |
| Linux lifecycle | Disposable Ubuntu 22.04, 24.04, 26.04 and Debian 12/13 containers with distro `kdialog` and Qt/KDE dependencies. Install, open an example in a visible XCB window, reinstall, purge. Hash distro Qt/KDE files and loader configuration; verify Qt loader cache, KDE canary startup, and existing package versions remain unchanged. Inspect the running app's actual Qt/plugin mappings. No installation is performed on the maintainer's host. |
| Windows | Separate Windows 2022 runner, no Qt SDK installation, Qt/Python removed from process PATH. Silent Inno Setup install into a path containing spaces, installed example/native window, loaded Qt/Windows plugin paths, Start Menu target, reinstall, uninstall. Shared system Qt DLL hashes and persistent PATH must remain unchanged. |
| macOS | Separate native Intel and Apple Silicon runners, no Qt SDK installation. Mount final DMG read-only, copy app into a path containing spaces, unmount before launching. Inspect every Mach-O's architecture/dependencies/RPATH and bundle symlinks. Require private Qt/Cocoa loading, help output and a visible native window; capture desktop, remove copied app. |

### Coverage limits and remaining release work

- Xvfb plus a KDE application is a meaningful regression test for the Linux
  library-clobbering failure, **not** a booted Kubuntu/Plasma session. Real
  desktop/VM tests still need to cover Wayland/XWayland, graphics drivers,
  themes, and installation while Plasma/Dolphin are already running.
- Runtime dependencies must be satisfied by the DEB's own `Depends`, not a
  duplicate CI dependency list. The initial matrix exposed a missing
  `libopengl0` dependency on Ubuntu 22.04/24.04 and Debian 12; it is now declared.
- Linux reinstall tests same-revision replacement, not arbitrary historical
  upgrade paths or recovery from an interrupted package-manager operation.
- Hosted Windows runners already contain Microsoft runtimes. This verifies
  Qt independence, not a compiler/redist-free Windows installation.
  Verify the official VC++ redistributable on a pristine Windows VM.
- macOS testing uses the current runner OS, not a minimum-OS matrix. Direct
  executable launch does not test browser quarantine, Finder launch, Gatekeeper
  or notarization. No protection is disabled by these checks.
- Windows/macOS installers remain unsigned. Signing/notarization require
  maintainer credentials and separate release work, not a simulated CI pass.

The design follows [Qt plugin deployment](https://doc.qt.io/qt-6/deployment-plugins.html)
and [`qt.conf`](https://doc.qt.io/qt-6/qt-conf.html),
[Debian shared-library policy](https://www.debian.org/doc/debian-policy/ch-sharedlibs.html)
and [package relationships](https://www.debian.org/doc/debian-policy/ch-relationships.html),
[Qt Windows deployment](https://doc.qt.io/qt-6/windows-deployment.html),
[Qt macOS deployment](https://doc.qt.io/qt-6/macos-deployment.html), and
[Apple notarization guidance](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution).

## Prompt for Copilot to adapt a fork

If your fork needs a change to the build recipe, ask Copilot in that repository:

> Review `GITHUB_ACTIONS.md`, `.github/workflows/build.yml`, and the CMake/
> packaging files. Make the smallest necessary changes so this repository can
> build and test the project on GitHub-hosted Linux, Windows, and macOS runners,
> and upload usable installer artifacts for all supported architectures.
> Preserve the existing application and
> packaging behavior. Do not add secrets, paid services, signing,
> or Copilot-specific dependencies. Preserve the existing
> gated pre-release publishing behavior. Do not push commits, create tags
> or releases, or change repository visibility. Explain any required GitHub
> Settings actions to the repository owner, and run any safe local checks
> available.

For the unchanged workflow, the owner usually only needs to enable Actions in
the repository settings and use **Actions -> Build and test -> Run workflow**.
Copilot is not needed to run it.
