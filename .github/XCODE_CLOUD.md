# FuseX releases

Push a release tag such as `1.10.0-fusex-0.3` after merging its source into
`main`. The tag supplies the version inside every package; no version commit is
required. The workflow listens to all tags and validates the complete version
at runtime. Non-release tags fail validation without building packages.

`release.yml` runs one stock FuseX build per platform:

| Platform | Build | Output |
| --- | --- | --- |
| Windows x64 | MinGW cross-build on Ubuntu, adapted from PR #88 | NSIS installer and portable ZIP |
| Linux x64 | GTK build in the GNOME 50 Flatpak SDK, adapted from PR #88 | Flatpak bundle |
| macOS universal | Xcode Cloud Archive and Notarize, then DMG packaging on a short GitHub macOS job | DMG containing the signed, stapled app |

Windows and Linux upload independently to a GitHub draft release. The macOS
workflow is dispatched separately and adds its assets to the same draft.
Apple billing, approval delays, build failures, or timeout cannot stop the
other platforms. SHA-256 checksums accompany each package. Review platform
results and publish the draft when ready. Automatic update-feed publication
and Sparkle/WinSparkle feed signing remain in the existing local deploy scripts;
this pipeline does not have those private signing keys or write access to the
website repository.

## Apple configuration

Keep the existing Xcode Cloud workflow manual, using the FuseX scheme's
**Archive / Deployment** action with **Notarize** enabled. Disable automatic
branch/tag start conditions there to avoid duplicate Apple builds. GitHub
selects the exact tag through the App Store Connect API. The tag commit must
already be reachable from `main` and contain the release-version hook.

The protected GitHub environment `macos-release` needs:

- Secrets `APP_STORE_CONNECT_API_KEY` (full `.p8` contents),
  `APP_STORE_CONNECT_KEY_ID`, and `APP_STORE_CONNECT_ISSUER_ID`.
- Optional variable `XCODE_CLOUD_WORKFLOW_ID` to override the confirmed default
  `528DCA26-4074-471F-A83B-D477B47DC2F0`. The ID is public configuration;
  the workflow also accepts it from an existing secret of the same name.
  Apple authentication secrets remain required.
- Required owner approval and deployment branch limited to **main**.

Do not grant the environment access to all tags. The tag workflow has no Apple
secrets; it dispatches `macos_cloud_release.yml` **on main**. That workflow uses
reviewed tooling from main, validates the requested tag's ancestry, and only
passes API secrets to the request/download step. Protect changes to main and
its workflow files. Environment approval must cover the selected source tag,
not just the worker's main branch.

## Versions and build numbers

The release hook updates `configure.ac`, Cocoa's `config.h`, and Xcode's
marketing version in the temporary build checkout. Defaults in the repository
remain useful for local builds. The four-part Windows resource version uses
upstream major/minor/micro plus the FuseX revision's final component, matching
the existing version convention.

macOS uses **CI_BUILD_NUMBER**, the product-wide counter owned by Xcode Cloud.
Apple increases it for each build, including retries. GitHub reruns that start
a fresh Apple build get a fresh number; resuming a run keeps its original
number. The downloaded app's `CFBundleVersion` must match Apple's run number.
Publishing an older resumed build after a newer release is rejected.

The last local Sparkle build was **8**. If the Cloud counter is still below 9,
set the product's **Xcode Cloud → Settings → Build Number → Next Build Number**
to **9** (or any higher unused number) once. Do not reset it later. Releases
with build numbers at or below 8 fail explicitly rather than publishing a
colliding update. The macOS worker is serialized to avoid simultaneous release
publication; pending GitHub concurrency runs can still be superseded, so use
manual dispatch to recover a missed release if pushing several tags together.

## Recovery and checks

The workflows must be merged into main before automatic release dispatch can
work. Create a new tag on the merged source; old release tags lack the new
hook. Never move an existing published tag to try the pipeline.

`Release FuseX` also supports manual dispatch with an existing release tag.
For a Mac-only retry, run `macOS release (Xcode Cloud)` on **main** with
`source_ref=refs/tags/<tag>`. Leave `build_run_id` blank for a new build, or use
the Apple build ID saved in the failed run's summary / `xcode-cloud-output`
artifact to resume. Enable `publish` to add assets to the release; leave it
disabled to validate and download artifacts without publishing.

The worker accepts only a `STAPLED_NOTARIZED_ARCHIVE`, checks the Apple
workflow and source commit, and verifies version, build number, code
signature, stapling, Gatekeeper assessment, and both arm64/x86_64 slices before
packaging. The DMG container is not separately signed/notarized; the app inside
is signed and stapled, as in the working local release process.

Existing asset names are immutable. An identical rerun skips already uploaded
files when GitHub supplies their matching SHA-256 digest. Different files or
an unavailable digest cause an error instead of overwriting a release.

Validation:

```sh
python3 -m unittest discover -s .github/scripts -p 'test_*.py'
actionlint .github/workflows/release.yml .github/workflows/package-windows-x64.yml .github/workflows/package-flatpak.yml .github/workflows/macos_cloud_release.yml
```

The Linux build runs `make check`. Cross-built Windows executables cannot run
natively on the Linux builder; validate the installer and launch on Windows
before publishing the draft. Hosted Flatpak export and Apple's API credentials
still need a first real tag run after merge.
