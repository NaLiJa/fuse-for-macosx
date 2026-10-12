# FuseX releases

Push a release tag such as `1.10.0-fusex-0.3` after merging its source into
`main`. The tag supplies the version inside every package; no version commit is
required. The workflow listens only to `*-fusex-*` tags and validates the complete
version at runtime. Upstream tags do not trigger release builds.

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
results and publish the draft when ready. Publishing a release does not update
Sparkle/WinSparkle: test the downloads, then explicitly approve feed publication
with the separate manual workflow below.

## Approving automatic updates

After testing a published stable release, open **Actions → Publish update feeds →
Run workflow**, select **main**, enter its tag, and choose **windows**, **macos**,
or **both**. There is no automatic trigger on tags or release publication.
Drafts, prereleases, tags outside the FuseX version format, and tag commits not
reachable from main are rejected. If a tested prerelease is ready for ordinary
users, first promote it to a stable release.

The `update-feeds` environment needs:

- Variable `UPDATE_FEEDS_APP_ID`: the GitHub App's numeric ID.
- Secret `UPDATE_FEEDS_APP_PRIVATE_KEY`: the GitHub App's full PEM private key.
- Secret `SPARKLE_PRIVATE_KEY`: the existing Sparkle Ed25519 private key exported
  using Sparkle's `bin/generate_keys -x <file>`, then copied from that file into
  the secret. This is separate from Apple signing and App Store Connect keys.
  Keep the existing key: installed apps trust its public key. Windows-only
  publication does not need this secret.
- Deployment branch limited to **main**; configure yourself as a required
  reviewer if you want a second approval after manual dispatch. If approving your
  own dispatch, leave **Prevent self-review** disabled.

Install the GitHub App only on `speccytools/speccytools.github.io`, with repository
**Contents: Read and write**. No webhook or organisation-wide access is needed.
The workflow requests a short-lived token scoped to that repository. Its normal
`GITHUB_TOKEN` reads FuseX assets but cannot write to the website repository.
Website branch rules must permit the app's push; the workflow never force-pushes.

The publisher downloads the exact existing assets, verifies GitHub's SHA-256
digests and sizes, and reuses `make -f Makefile.darwin appcast` and the Windows
appcast generator. macOS uses the pinned Sparkle tools and checks the resulting
DMG signature against `SUPublicEDKey`, as well as the Apple build counter in
`macos-build.json`. Windows preserves the existing unsigned WinSparkle feed
format; adding Windows signature enforcement requires a separate app/key change.

The resulting feeds are committed to `appcast.xml` and
`updates/windows/appcast.xml` in the website's default branch. Each platform also
uploads its generated XML as a workflow artifact. Website hosting must deploy
that branch, as it does for the existing manually pushed feeds.

Platforms run independently, so Mac failures do not prevent Windows publication.
The workflow serializes website writes, retries concurrent external pushes,
rejects version/counter downgrades, and leaves an already published identical
release unchanged. For a failed platform, rerun manually with only that platform
selected. GitHub concurrency can replace a pending run if several are queued;
manually dispatch again if an intended publication was superseded.

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
Select **main** to use updated workflow instructions. Linux uses the packaging
manifest from that workflow revision while building the exact tagged source;
this permits packaging fixes without moving an existing tag.
For a Mac-only retry, run `macOS release (Xcode Cloud)` on **main** with
`source_ref=refs/tags/<tag>`. Leave `build_run_id` blank for a new build, or use
the Apple build ID saved in the failed run's summary / `xcode-cloud-output`
artifact to resume. Enable `publish` to add assets to the release; leave it
disabled to validate and download artifacts without publishing.

The worker preserves the configured workflow ID's casing and uses the exact
resource ID returned by Apple. If a direct workflow lookup returns 404, it
lists products and workflows visible to the API key and matches only the
configured UUID. If no match is visible, check the workflow URL and the API
key's Apple team, issuer, and Xcode Cloud access; no build is started.

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
actionlint .github/workflows/release.yml .github/workflows/package-windows-x64.yml .github/workflows/package-flatpak.yml .github/workflows/macos_cloud_release.yml .github/workflows/update-feeds.yml
```

The Linux build runs `make check`. Cross-built Windows executables cannot run
natively on the Linux builder; validate the installer and launch on Windows
before publishing the draft. Hosted Flatpak export and Apple's API credentials
still need a first real tag run after merge.
