#!/usr/bin/env python3
"""Set the AppStream release shown by Flatpak to the requested release tag."""

import argparse
from datetime import date
from pathlib import Path
import re
import xml.etree.ElementTree as ET


def stamp_metadata(path, version, release_date):
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+-fusex-[0-9]+\.[0-9]+', version):
        raise ValueError('Expected a FuseX release tag')
    date.fromisoformat(release_date)
    text = path.read_text()
    releases = ET.fromstring(text).find('releases')
    if releases is None:
        raise ValueError('AppStream metadata has no releases')
    if len(releases) and releases[0].get('version') == version:
        return
    # Keep committed release notes, dates, formatting, and copyright comments.
    entry = (f'\n    <release date="{release_date}" version="{version}">\n'
             '      <description><p>FuseX release.</p></description>\n'
             '    </release>')
    text, count = re.subn(r'<releases>', lambda match: match.group() + entry, text, count=1)
    if count != 1:
        raise ValueError('Cannot insert AppStream release')
    ET.fromstring(text)
    path.write_text(text)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', required=True)
    parser.add_argument('--date', default=date.today().isoformat())
    parser.add_argument('--root', type=Path, default=Path('.'))
    args = parser.parse_args()
    stamp_metadata(args.root / 'data/org.speccytools.FuseX.metainfo.xml', args.version, args.date)
