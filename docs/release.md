# CI and releases

Adapted from the REV-246 project. **Both workflows are manual-only** (`workflow_dispatch`), so they cost no GitHub minutes until you start them. Nothing runs on push or pull request.

## Plugin CI (`.github/workflows/plugin-ci.yml`)

- Runs on macOS, Windows and Ubuntu 24.04: configure (JUCE and Catch2 fetched by CMake, cached), build, `ctest`.
- macOS also runs `auval` (AU `aufx Yr57 Yrev`) and `pluginval` at strictness 10; Windows and Linux run `pluginval` on the VST3.
- Start it: Actions tab, or `gh workflow run plugin-ci.yml --ref <branch>`.
- To switch it on for every push or PR, uncomment the `push` / `pull_request` triggers at the top of the file.

## Release (`.github/workflows/plugin-release.yml`)

- Refuses a commit whose CI has not passed (unless `skip_ci_check` is ticked).
- Builds AU + VST3 for macOS (arm64, and a Universal arm64 + x86_64 build for Intel Macs and Rosetta hosts), VST3 for Windows and Linux; fails if a macOS binary needs anything newer than macOS 11 or the Windows DLL imports the Visual C++ runtime (it is linked statically); signs and notarizes macOS bundles and the `.pkg`; packages zip plus installer (`.pkg`, NSIS `.exe`, `.deb`); creates a **draft** GitHub Release tagged `v<version>`.
- The version comes from `project(YREV57 VERSION x.y.z ...)` in `CMakeLists.txt`. Versions starting `0.` are marked pre-release.
- Installer sources are in `installer/` (`macos/`, `windows/YREV57.nsi`, `linux/`, and `stage_docs.sh`, which writes the readme and licences into every package from `README.md`'s "Install" and "Using it" sections, `THIRD_PARTY_NOTICES.md` and `LICENSE`).

### Secrets needed (repo settings, Actions secrets) before the first release

| Secret | What |
| --- | --- |
| `APPLE_CERT_P12_BASE64`, `APPLE_CERT_PASSWORD` | Developer ID **Application** certificate (.p12, base64) and its password |
| `APPLE_SIGNING_IDENTITY` | Name of that certificate, as `codesign` expects |
| `APPLE_INSTALLER_CERT_P12_BASE64`, `APPLE_INSTALLER_CERT_PASSWORD` | Developer ID **Installer** certificate and password |
| `APPLE_INSTALLER_SIGNING_IDENTITY` | Name of the installer certificate |
| `APPLE_TEAM_ID`, `APPLE_ID`, `APPLE_APP_SPECIFIC_PASSWORD` | For `notarytool` |

The same Apple secrets can be reused from REV-246. Windows and Linux builds are unsigned.

## Before the first release

- Set a real maintainer email in `installer/linux/control` (currently a placeholder).
- The plugin identifiers are `PLUGIN_MANUFACTURER_CODE Yrev`, `PLUGIN_CODE Yr57`, bundle ID `org.yrev57.YREV57`. Change them before release if you want different ones: they cannot change afterwards without breaking saved projects.
- Replace the README "Install" and "Using it" text, which the installers show.
- Differences from REV-246: JUCE comes from FetchContent (no submodule), the project is at the repo root (no `plugin/` folder), and the CI also builds the Catch2 tests with `-DYREV_BUILD_TESTS=ON`.

## Local checks

```bash
cmake -S . -B build -DYREV_BUILD_TESTS=ON && cmake --build build --target YREV57Tests && ctest --test-dir build --output-on-failure
installer/macos/build_installer.sh 1.0.0 build/YREV57_artefacts/Release .   # unsigned .pkg
```
