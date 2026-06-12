#!/bin/bash
# Generate a WinSparkle appcast.xml for a FuseX Windows installer.
#
# Usage:
#   generate-appcast.sh SETUP_EXE [OUTPUT_XML]
#
# Environment:
#   GITHUB_RELEASES_REPO         GitHub repo for release assets (default: speccytools/fusex)
#   RELEASE_TAG                  GitHub release tag (default: PACKAGE_VERSION from config.h)
#   PACKAGE_VERSION              Override version string (else read config.h)
#
# Download URLs follow GitHub releases layout, e.g.:
#   https://github.com/speccytools/fusex/releases/download/1.8.0-fusex-0.4/fusex-1.8.0-fusex-0.4-win32-setup.exe
#
# See: https://github.com/vslavik/winsparkle/wiki/Appcast-Feeds

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$( cd "$SCRIPT_DIR/../.." && pwd )"

DEFAULT_OUTPUT="$REPO_ROOT/build/appcast-windows/appcast.xml"
GITHUB_RELEASES_REPO="${GITHUB_RELEASES_REPO:-speccytools/fusex}"

SETUP_EXE="${1:-}"
OUTPUT_XML="${2:-$DEFAULT_OUTPUT}"

if [[ -z "$SETUP_EXE" ]]; then
  echo "Usage: $0 SETUP_EXE [OUTPUT_XML]" >&2
  exit 1
fi

if [[ ! -f "$SETUP_EXE" ]]; then
  echo "Error: installer not found: $SETUP_EXE" >&2
  exit 1
fi

if [[ -z "${PACKAGE_VERSION:-}" && -f "$REPO_ROOT/config.h" ]]; then
  PACKAGE_VERSION="$( sed -n 's/^#define PACKAGE_VERSION "\(.*\)"/\1/p' "$REPO_ROOT/config.h" | head -1 )"
fi

if [[ -z "${PACKAGE_VERSION:-}" ]]; then
  echo "Error: PACKAGE_VERSION not set and config.h not found" >&2
  exit 1
fi

RELEASE_TAG="${RELEASE_TAG:-$PACKAGE_VERSION}"
SETUP_FILENAME="fusex-${PACKAGE_VERSION}-win32-setup.exe"
SETUP_BASENAME="$( basename "$SETUP_EXE" )"

if [[ "$SETUP_BASENAME" != "$SETUP_FILENAME" ]]; then
  echo "Error: installer basename mismatch." >&2
  echo "  Expected: $SETUP_FILENAME (from config.h PACKAGE_VERSION=$PACKAGE_VERSION)" >&2
  echo "  Got:      $SETUP_BASENAME" >&2
  echo "Re-run ./configure after updating configure.ac, then rebuild dist-win32." >&2
  exit 1
fi

DOWNLOAD_URL="https://github.com/${GITHUB_RELEASES_REPO}/releases/download/${RELEASE_TAG}/${SETUP_FILENAME}"
# WinSparkle accepts length="0" when size is unknown (see Appcast-Feeds wiki).
PUB_DATE="$( date -u '+%a, %d %b %Y %H:%M:%S +0000' )"

mkdir -p "$( dirname "$OUTPUT_XML" )"

cat > "$OUTPUT_XML" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <title>FuseX Windows Updates</title>
    <description>Most recent FuseX releases for Windows</description>
    <language>en</language>
    <item>
      <title>Version ${PACKAGE_VERSION}</title>
      <sparkle:version>${PACKAGE_VERSION}</sparkle:version>
      <pubDate>${PUB_DATE}</pubDate>
      <enclosure url="${DOWNLOAD_URL}"
                 sparkle:os="windows-x64"
                 sparkle:installerArguments="/S"
                 length="0"
                 type="application/octet-stream" />
    </item>
  </channel>
</rss>
EOF

echo "Appcast written: $OUTPUT_XML"
echo "  enclosure: $DOWNLOAD_URL"
