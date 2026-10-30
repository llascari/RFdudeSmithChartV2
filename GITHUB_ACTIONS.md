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
their CPU architecture. These checks do not replace clean-machine desktop
testing. The original v0.1.0 Linux release
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
After all four platform jobs pass, builds of `main` (push or manual dispatch)
automatically publish an immutable pre-release tagged
`build-<run-id>-<attempt>`. Version-tag builds publish under the `v<version>` tag,
which must match the CMake project version. Pull requests, other branches, and
any failed platform job cannot publish. Diagnostic `-tests-failed` artifacts
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
are not notarized. Passing CI is not exhaustive clean-machine
installation/uninstallation certification.

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
