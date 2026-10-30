# Build installers with GitHub Actions

This repository includes a reusable GitHub Actions workflow for building and
testing the application on Linux and Windows. The Windows job runs on a
GitHub-hosted Windows machine and creates the setup installer; you do not need
a local Windows computer, Visual Studio, Qt SDK, or GitHub Copilot.

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
5. When the run finishes, open it and download the Linux and Windows
   artifacts. The Windows artifact contains the setup `.exe`; the Linux
   artifact contains the `.deb`. A Windows artifact ending in
   `-tests-failed` was packaged despite a test failure and should not be
   presented as a verified build.

Linux packages are extracted and checked for private Qt runtime paths, then
smoke-tested under Xvfb before upload. This does not replace clean-machine
Kubuntu/KDE and Ubuntu desktop testing. The original v0.1.0 Linux release
installer is unsafe: see the warning in [README.md](README.md).

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

Artifacts are temporary workflow-run downloads, not published releases. This
workflow retains installer artifacts for 30 days and does not create or publish
a GitHub Release. To distribute a versioned release from your own repository,
review and test the installers, then create a release there and attach the
artifacts and checksums yourself. You need write access to that repository.

## Prompt for Copilot to adapt a fork

If your fork needs a change to the build recipe, ask Copilot in that repository:

> Review `GITHUB_ACTIONS.md`, `.github/workflows/build.yml`, and the CMake/
> packaging files. Make the smallest necessary changes so this repository can
> build and test the project on GitHub-hosted Linux and Windows runners, and
> upload usable installer artifacts. Preserve the existing application and
> packaging behavior. Do not add secrets, paid services, release publishing,
> signing, or Copilot-specific dependencies. Do not push commits, create tags
> or releases, or change repository visibility. Explain any required GitHub
> Settings actions to the repository owner, and run any safe local checks
> available.

For the unchanged workflow, the owner usually only needs to enable Actions in
the repository settings and use **Actions -> Build and test -> Run workflow**.
Copilot is not needed to run it.
