#!/usr/bin/env python3
"""Publish approved stable-release appcasts without rebuilding release assets."""
import argparse
import base64
from datetime import datetime
from email.utils import format_datetime
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
from urllib.parse import quote
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'fusepb/ci_scripts'))
from release_version import release_version, VERSION_PATTERN

WORK = ROOT / 'build/update-feeds'
SPARKLE = '{http://www.andymatuschak.org/xml-namespaces/sparkle}'
FEEDS = {'macos': 'appcast.xml', 'windows': 'updates/windows/appcast.xml'}


def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def release(repo, tag, token=None):
    data = json.loads(subprocess.check_output([
        'gh', 'api', f'repos/{repo}/releases/tags/{quote(tag, safe="")}',
    ], env=dict(os.environ, GH_TOKEN=token) if token else None))
    require_stable(data, tag)
    return data


def require_stable(data, tag):
    release_version(tag)
    if data['tag_name'] != tag or data['draft'] or data['prerelease'] or not data['published_at']:
        raise ValueError('Only a published stable FuseX release can enter the update feeds')


def version_tuple(version):
    match = re.fullmatch(VERSION_PATTERN, version)
    if not match:
        raise ValueError(f'Unrecognized feed version: {version}')
    return tuple(map(int, match.groups()))


def installer_name(tag, platform):
    return f'FuseX-{tag}.dmg' if platform == 'macos' else f'fusex-{tag}-windows-x64-setup.exe'


def verify_digest(path, asset):
    digest = 'sha256:' + hashlib.sha256(path.read_bytes()).hexdigest()
    if asset.get('digest') != digest or asset['size'] != path.stat().st_size:
        raise ValueError(f'Asset digest/size mismatch or unavailable digest: {path.name}')


def feed_item(path):
    items = ET.parse(path).findall('./channel/item')
    if not items:
        raise ValueError(f'No update items in {path}')
    return items


def item_version(item, platform):
    # Existing Windows feeds use the version directly; macOS uses an Apple counter.
    value = item.findtext(SPARKLE + ('shortVersionString' if platform == 'macos' else 'version'))
    if not value:
        enclosure = item.find('enclosure')
        value = enclosure.get(SPARKLE + 'version') if enclosure is not None else None
    return value


def can_publish(candidate, existing, platform):
    """Reject rollback/counter reuse; unchanged releases are idempotent."""
    version = item_version(candidate, platform)
    unchanged = False
    for old in existing:
        previous = item_version(old, platform)
        if version_tuple(previous) > version_tuple(version):
            raise ValueError(f'Refusing downgrade from {previous} to {version}')
        if platform == 'macos':
            current_counter = int(candidate.findtext(SPARKLE + 'version'))
            old_counter = int(old.findtext(SPARKLE + 'version'))
            if current_counter < old_counter or (current_counter == old_counter and previous != version):
                raise ValueError('Refusing to decrease or reuse the Sparkle build counter')
        if previous == version:
            new_asset, old_asset = candidate.find('enclosure'), old.find('enclosure')
            for attribute in ('url', 'length', SPARKLE + 'edSignature'):
                if new_asset.get(attribute) != old_asset.get(attribute):
                    raise ValueError('This feed version already exists with different asset metadata')
            unchanged = True
    return not unchanged


def validate_generated(path, data, platform, tag):
    item, = feed_item(path)
    if item_version(item, platform) != tag:
        raise ValueError('Generated appcast has the wrong release version')
    archive = WORK / installer_name(tag, platform)
    enclosure = item.find('enclosure')
    expected = f'https://github.com/{os.environ["GH_REPO"]}/releases/download/{tag}/{archive.name}'
    if enclosure is None or enclosure.get('url') != expected or int(enclosure.get('length', '0')) != archive.stat().st_size:
        raise ValueError('Generated appcast URL/size does not match the published asset')
    if platform == 'macos':
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
        metadata = json.loads((WORK / 'macos-build.json').read_text())
        if metadata['release_version'] != tag or int(item.findtext(SPARKLE + 'version')) != int(metadata['build_number']):
            raise ValueError('Appcast version/counter does not match the Xcode Cloud release')
        with (ROOT / 'fusepb/Info-FuseX.plist').open('rb') as source:
            public_key = plistlib.load(source)['SUPublicEDKey']
        # Test the signature over the exact downloaded DMG with the app's trusted key.
        Ed25519PublicKey.from_public_bytes(base64.b64decode(public_key, validate=True)).verify(
            base64.b64decode(enclosure.get(SPARKLE + 'edSignature', ''), validate=True), archive.read_bytes())


def download(data, tag, platform):
    WORK.mkdir(parents=True, exist_ok=True)
    names = [installer_name(tag, platform)]
    if platform == 'macos':
        names.append('macos-build.json')
    assets = {asset['name']: asset for asset in data['assets']}
    for name in names:
        if name not in assets:
            raise ValueError(f'Missing published {platform} asset: {name}')
        with (WORK / name).open('wb') as target:
            run('gh', 'api', f'repos/{os.environ["GH_REPO"]}/releases/assets/{assets[name]["id"]}',
                '-H', 'Accept: application/octet-stream', stdout=target)
        verify_digest(WORK / name, assets[name])
    (WORK / 'release.json').write_text(json.dumps(data))


def generate(data, tag, platform):
    archive = WORK / installer_name(tag, platform)
    output = WORK / 'generated.xml'
    if platform == 'windows':
        env = dict(os.environ, PACKAGE_VERSION=tag, RELEASE_TAG=tag,
                   APPCAST_PUB_DATE=format_datetime(datetime.fromisoformat(data['published_at'].replace('Z', '+00:00'))))
        run('bash', str(ROOT / 'data/win32/generate-appcast.sh'), str(archive), str(output), env=env)
    else:
        key = os.environ.get('SPARKLE_PRIVATE_KEY', '').strip()
        if not key:
            raise ValueError('SPARKLE_PRIVATE_KEY is required for macOS feed signing')
        key_file = Path(os.environ['RUNNER_TEMP']) / 'fusex-sparkle-key'
        try:
            with key_file.open('w', opener=lambda name, flags: os.open(name, flags, 0o600)) as target:
                target.write(key + '\n')
            run('make', '-f', 'Makefile.darwin', 'appcast', f'FUSEX_VERSION={tag}',
                f'APPCAST_ARCHIVE={archive}', f'SPARKLE_ED_KEY_FILE={key_file}',
                f'SPARKLE_GENERATE_APPCAST={ROOT / "build/sparkle/bin/generate_appcast"}', cwd=ROOT)
            shutil.copyfile(ROOT / 'build/appcast/appcast.xml', output)
        finally:
            key_file.unlink(missing_ok=True)
    validate_generated(output, data, platform, tag)


def publish(data, tag, platform):
    current = release(os.environ['GH_REPO'], tag, os.environ['RELEASE_READ_TOKEN'])
    names = [installer_name(tag, platform)] + (['macos-build.json'] if platform == 'macos' else [])
    assets = {a['name']: a for a in current['assets']}
    for name in names:
        verify_digest(WORK / name, assets[name])
    generated = WORK / 'generated.xml'
    validate_generated(generated, data, platform, tag)
    website = ROOT / 'build/update-website'
    target = website / FEEDS[platform]
    candidate, = feed_item(generated)
    # On another website writer's push, fetch and recheck rollback before retrying.
    for attempt in range(3):
        run('git', '-C', str(website), 'fetch', 'origin')
        run('git', '-C', str(website), 'remote', 'set-head', 'origin', '--auto')
        run('git', '-C', str(website), 'reset', '--hard', 'origin/HEAD')
        if target.exists() and not can_publish(candidate, feed_item(target), platform):
            print('Already published; leaving existing feed unchanged')
            return
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(generated, target)
        run('git', '-C', str(website), 'config', 'user.name', 'FuseX Update Publisher')
        run('git', '-C', str(website), 'config', 'user.email', 'fusex-updates@users.noreply.github.com')
        run('git', '-C', str(website), 'add', '--', FEEDS[platform])
        run('git', '-C', str(website), 'commit', '-m', f'Update FuseX {platform} to {tag}')
        branch = subprocess.check_output(['git', '-C', str(website), 'symbolic-ref', 'refs/remotes/origin/HEAD'], text=True).strip().removeprefix('refs/remotes/origin/')
        pushed = subprocess.run(['git', '-C', str(website), 'push', 'origin', f'HEAD:refs/heads/{branch}'])
        if pushed.returncode == 0:
            sha = subprocess.check_output(['git', '-C', str(website), 'rev-parse', 'HEAD'], text=True).strip()
            with open(os.environ['GITHUB_STEP_SUMMARY'], 'a') as summary:
                summary.write(f'Published {platform} update {tag}: https://github.com/speccytools/speccytools.github.io/commit/{sha}\n')
            return
    raise RuntimeError('Website push failed after three attempts')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['plan', 'download', 'generate', 'publish'])
    args = parser.parse_args()
    tag = release_version(os.environ['RELEASE_TAG'])
    platform = os.environ['PLATFORM']
    if args.command == 'plan':
        release(os.environ['GH_REPO'], tag)
        sha = subprocess.check_output(['git', 'rev-parse', '--verify', '--end-of-options', f'refs/tags/{tag}^{{commit}}'], text=True).strip()
        run('git', 'merge-base', '--is-ancestor', sha, 'origin/main')
        choices = ['windows', 'macos'] if platform == 'both' else [platform]
        matrix = {'include': [{'platform': p, 'runner': 'macos-26' if p == 'macos' else 'ubuntu-24.04'} for p in choices]}
        with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
            output.write(f'matrix={json.dumps(matrix)}\n')
    elif args.command == 'download':
        download(release(os.environ['GH_REPO'], tag), tag, platform)
    else:
        data = json.loads((WORK / 'release.json').read_text())
        require_stable(data, tag)
        if args.command == 'generate':
            generate(data, tag, platform)
        else:
            publish(data, tag, platform)


if __name__ == '__main__':
    main()
