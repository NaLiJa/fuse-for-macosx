#!/usr/bin/env python3
"""Stamp a release checkout; committed project versions remain local defaults."""
import argparse
import os
from pathlib import Path
import re

# Last locally published Sparkle build. Cloud releases must be newer.
MINIMUM_BUILD_NUMBER = 8
VERSION_PATTERN = r"([0-9]+)\.([0-9]+)\.([0-9]+)-fusex-([0-9]+)\.([0-9]+)"


def release_version(tag):
    tag = tag.removeprefix('refs/tags/')
    if not re.fullmatch(VERSION_PATTERN, tag):
        raise ValueError('Expected a release tag such as 1.10.0-fusex-0.2')
    return tag


def build_number(number):
    if not re.fullmatch(r'[1-9][0-9]*', str(number)) or int(number) <= MINIMUM_BUILD_NUMBER:
        raise ValueError('Xcode Cloud build number must exceed 8. Set its next build number to at least 9 in product Settings.')
    return int(number)


def replace(path, pattern, value):
    text, count = re.subn(pattern, lambda _: value, path.read_text())
    if not count:
        raise ValueError(f'Cannot stamp version in {path}')
    path.write_text(text)


def stamp(root, tag, number=None):
    version = release_version(tag)
    major, minor, micro, _, nano = re.fullmatch(VERSION_PATTERN, version).groups()
    configure = root / 'configure.ac'
    replace(configure, r'm4_define\(\[fuse_version\],\s*\[[^\]]+\]\)', f'm4_define([fuse_version], [{version}])')
    for name, value in zip(('major', 'minor', 'micro', 'nano'), (major, minor, micro, nano)):
        replace(configure, rf'm4_define\(\[fuse_{name}_version\],\s*\[[^\]]+\]\)', f'm4_define([fuse_{name}_version], [{value}])')
    config = root / 'fusepb/config.h'
    replace(config, r'#define VERSION "[^"]+"', f'#define VERSION "{version}"')
    replace(config, r'#define FUSE_RC_VERSION [^\n]+', f'#define FUSE_RC_VERSION {major},{minor},{micro},{nano}')
    project = root / 'fusepb/FuseX.xcodeproj/project.pbxproj'
    replace(project, r'MARKETING_VERSION = [^;]+;', f'MARKETING_VERSION = "{version}";')
    if number is not None:
        replace(project, r'CURRENT_PROJECT_VERSION = [^;]+;', f'CURRENT_PROJECT_VERSION = {build_number(number)};')
    print(f'Release version: {version}' + (f' (build {number})' if number else ''))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', default=os.environ.get('CI_TAG') or os.environ.get('CI_GIT_REF', ''))
    parser.add_argument('--build-number', default=os.environ.get('CI_BUILD_NUMBER'))
    parser.add_argument('--root', type=Path, default=Path('.'))
    args = parser.parse_args()
    # Existing branch builds remain available for Cloud development trials.
    if args.tag.startswith('refs/heads/') or not args.tag:
        print('Branch build: keeping local version defaults')
        return
    stamp(args.root, args.tag, args.build_number)


if __name__ == '__main__':
    main()
