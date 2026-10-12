#!/usr/bin/env python3
"""Validate the exported notarized app before putting it into a DMG."""

import os
import json
from pathlib import Path
import plistlib
import subprocess


def main():
    extracted = Path("cloud-output/extracted")
    extracted.mkdir(parents=True, exist_ok=True)
    subprocess.run(["ditto", "-x", "-k", "cloud-output/notarized.zip", str(extracted)], check=True)
    apps = list(extracted.rglob("FuseX.app"))
    if len(apps) != 1:
        raise ValueError(f"Expected one FuseX.app in the notarized artifact, found {len(apps)}")
    app = apps[0]
    with (app / "Contents/Info.plist").open("rb") as source:
        info = plistlib.load(source)
    if info["CFBundleShortVersionString"] != os.environ["RELEASE_VERSION"]:
        raise ValueError("App version does not match the source release version")
    metadata = json.loads(Path("cloud-output/build.json").read_text())
    if str(info["CFBundleVersion"]) != str(metadata["build_number"]):
        raise ValueError("Exported app does not use the increasing Xcode Cloud build number")
    subprocess.run(["codesign", "--verify", "--deep", "--strict", "--verbose=2", str(app)], check=True)
    subprocess.run(["xcrun", "stapler", "validate", str(app)], check=True)
    subprocess.run(["spctl", "--assess", "--type", "execute", "--verbose=2", str(app)], check=True)
    subprocess.run(["lipo", "-verify_arch", "arm64", "x86_64", str(app / "Contents/MacOS/FuseX")], check=True)
    Path("dist").mkdir(exist_ok=True)
    subprocess.run(["ditto", str(app), "dist/FuseX.app"], check=True)
    print(f"Validated notarized FuseX {info['CFBundleShortVersionString']} ({info['CFBundleVersion']})")


if __name__ == "__main__":
    main()
