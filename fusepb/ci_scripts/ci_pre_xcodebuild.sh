#!/bin/sh

# Build the external products before Xcode copies them into FuseX.app.
set -eu
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

cd "${CI_PRIMARY_REPOSITORY_PATH:?Xcode Cloud repository path is required}"

# Use the same fixed output folders and universal binaries as local releases.
# The app's embedding phases apply the final signing identity on copy.
make -f Makefile.darwin 3rdparty \
    ARCHS="arm64 x86_64" \
    MACOSX_DEPLOYMENT_TARGET=12.0 \
    FUSEGENERATOR_DEVELOPMENT_TEAM= \
    FUSEGENERATOR_CODE_SIGN_IDENTITY=- \
    FUSEGENERATOR_CODE_SIGN_STYLE=Manual \
    XCODEBUILD_SIGN_ARGS="CODE_SIGN_IDENTITY=- CODE_SIGN_STYLE=Manual DEVELOPMENT_TEAM="

for product in \
    audiofile/build/Deployment/audiofile.framework \
    libgcrypt/build/Deployment/gcrypt.framework \
    mbedtls/build/Deployment/mbedtls.framework \
    libssh2/build/Deployment/libssh2.framework \
    FuseGenerator/build/Deployment/FuseQLThumbnail.appex \
    FuseGenerator/build/Deployment/FuseQLPreview.appex \
    FuseImporter/build/Deployment/FuseImporter.mdimporter; do
    if [ ! -d "3rdparty/$product" ]; then
        echo "Missing dependency product: 3rdparty/$product" >&2
        exit 1
    fi
done
