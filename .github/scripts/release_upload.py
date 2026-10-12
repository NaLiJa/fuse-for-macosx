#!/usr/bin/env python3
"""Create a reviewable draft release and add assets without replacing existing files."""
import json
from pathlib import Path
import subprocess
import sys
import os
from urllib.parse import quote


def gh(*args, **kwargs):
    return subprocess.run(['gh', *args], check=True, **kwargs)


def verify_counter(candidate, earlier):
    for tag, metadata in earlier:
        if metadata['build_run_id'] == candidate['build_run_id'] and tag == candidate['release_version']:
            continue  # Retry of the same immutable release.
        if int(metadata['build_number']) >= int(candidate['build_number']):
            raise ValueError(f"Build {candidate['build_number']} does not exceed released build {metadata['build_number']} ({tag})")


def main():
    tag, directory = sys.argv[1:]
    assets = sorted(p for p in Path(directory).iterdir() if p.is_file())
    if not assets:
        raise ValueError('No release assets to upload')
    repo = os.environ['GH_REPO']
    provenance = Path(directory) / 'macos-build.json'
    if provenance.exists():
        candidate = json.loads(provenance.read_text())
        pages = json.loads(subprocess.check_output(['gh', 'api', f'repos/{repo}/releases', '--paginate', '--slurp'], text=True))
        earlier = []
        for page in pages:
            for release in page:
                for asset in release['assets']:
                    if asset['name'] == 'macos-build.json':
                        metadata = json.loads(subprocess.check_output(['gh', 'api', f"repos/{repo}/releases/assets/{asset['id']}", '-H', 'Accept: application/octet-stream']))
                        earlier.append((release['tag_name'], metadata))
        verify_counter(candidate, earlier)
    exists = subprocess.run(['gh', 'release', 'view', tag], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if exists.returncode:
        created = subprocess.run(['gh', 'release', 'create', tag, '--verify-tag', '--draft', '--title', f'FuseX {tag}', '--notes', 'Release assets are uploaded independently for Windows, Linux, and macOS. Review all platform results before publishing.'])
        if created.returncode:
            # Another platform may have created the same draft meanwhile.
            gh('release', 'view', tag, stdout=subprocess.DEVNULL)
    # Reruns are safe only when the existing asset has identical bytes.
    existing = json.loads(subprocess.check_output(['gh', 'api', f'repos/{repo}/releases/tags/{quote(tag, safe="")}'], text=True))['assets']
    known = {a['name']: a for a in existing}
    import hashlib
    for asset in assets:
        digest = 'sha256:' + hashlib.sha256(asset.read_bytes()).hexdigest()
        if asset.name in known:
            if known[asset.name].get('digest') != digest:
                raise ValueError(f'{asset.name} already exists with a different or unverifiable digest; refusing to replace it')
            print(f'Already uploaded: {asset.name}')
            continue
        gh('release', 'upload', tag, str(asset))


if __name__ == '__main__':
    main()
