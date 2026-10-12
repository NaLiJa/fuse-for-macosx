#!/usr/bin/env python3
"""Validate release inputs without executing code from the requested source ref."""

import os
import re
import subprocess
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "fusepb/ci_scripts"))
from release_version import release_version


def git(*args):
    return subprocess.check_output(["git", *args], text=True).strip()


def source_info(source_ref):
    if not source_ref.startswith("refs/tags/"):
        raise ValueError("Releases require refs/tags/<release version>")
    tag = release_version(source_ref)
    subprocess.run(["git", "check-ref-format", source_ref], check=True)
    sha = git("rev-parse", "--verify", "--end-of-options", source_ref + "^{commit}")
    subprocess.run(["git", "merge-base", "--is-ancestor", sha, "origin/main"], check=True)
    git("show", f"{sha}:fusepb/ci_scripts/release_version.py")
    version = tag
    return {"sha": sha, "version": version, "tag": tag}


if __name__ == "__main__":
    info = source_info(os.environ["SOURCE_REF"])
    with open(os.environ["GITHUB_OUTPUT"], "a") as output:
        for key, value in info.items():
            output.write(f"{key}={value}\n")
    print(f"Reviewed source: {info['sha']}, version {info['version']}")
