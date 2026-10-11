#!/bin/sh

# Xcode Cloud invokes this hook before building FuseX.xcodeproj.
set -eu

# Match the Homebrew locations used by the project's libfusex build phase.
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
export HOMEBREW_NO_AUTO_UPDATE=1

# Both FuseX and libspectrum bootstrap their build with autoreconf.
brew install autoconf automake libtool pkg-config

# Fail in preparation with a clear error if a required tool is unavailable.
for tool in autoreconf automake glibtoolize pkg-config; do
    command -v "$tool"
done
