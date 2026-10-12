"""Exercise approval gates, rollback prevention, asset integrity and feed generators."""
import base64
import contextlib
import hashlib
import io
import os
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

import update_feeds as feeds

TAG = '1.10.0-fusex-0.3'
REPO = 'speccytools/fusex'


def item(version=TAG, platform='windows', counter=10, size=3, signature='signature'):
    result = ET.Element('item')
    ET.SubElement(result, feeds.SPARKLE + 'version').text = str(counter) if platform == 'macos' else version
    if platform == 'macos':
        ET.SubElement(result, feeds.SPARKLE + 'shortVersionString').text = version
    ET.SubElement(result, 'enclosure', {
        'url': f'https://github.com/{REPO}/releases/download/{version}/{feeds.installer_name(version, platform)}',
        'length': str(size), feeds.SPARKLE + 'edSignature': signature,
    })
    return result


def write_feed(path, candidate):
    root = ET.Element('rss')
    ET.SubElement(root, 'channel').append(candidate)
    ET.ElementTree(root).write(path)


class UpdateFeedTests(unittest.TestCase):
    def test_only_published_stable_fusex_releases(self):
        valid = dict(tag_name=TAG, draft=False, prerelease=False, published_at='2026-10-12T12:00:00Z')
        feeds.require_stable(valid, TAG)
        for change in ({'draft': True}, {'prerelease': True}, {'published_at': None}, {'tag_name': 'other'}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                feeds.require_stable(dict(valid, **change), TAG)
        with self.assertRaises(ValueError):
            feeds.require_stable(dict(valid, tag_name='1.10.0'), '1.10.0')

    def test_versions_compare_numerically(self):
        self.assertTrue(feeds.can_publish(item('1.10.0-fusex-0.10'), [item('1.10.0-fusex-0.9')], 'windows'))
        with self.assertRaises(ValueError):
            feeds.can_publish(item(), [item('1.10.0-fusex-0.4')], 'windows')

    def test_mac_counter_cannot_decrease_or_be_reused(self):
        old = item('1.10.0-fusex-0.2', 'macos', 10)
        for counter in (9, 10):
            with self.subTest(counter=counter), self.assertRaises(ValueError):
                feeds.can_publish(item(platform='macos', counter=counter), [old], 'macos')
        self.assertTrue(feeds.can_publish(item(platform='macos', counter=11), [old], 'macos'))

    def test_identical_retries_leave_feed_untouched(self):
        for platform in ('windows', 'macos'):
            self.assertFalse(feeds.can_publish(item(platform=platform), [item(platform=platform)], platform))
        with self.assertRaises(ValueError):
            feeds.can_publish(item(size=4), [item()], 'windows')
        # All history is checked even when the first item is already this release.
        with self.assertRaises(ValueError):
            feeds.can_publish(item(), [item(), item('1.10.0-fusex-0.4')], 'windows')

    def test_asset_digest_and_size_are_required(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'asset'
            path.write_bytes(b'abc')
            asset = dict(size=3, digest='sha256:' + hashlib.sha256(b'abc').hexdigest())
            feeds.verify_digest(path, asset)
            for invalid in (dict(asset, size=4), dict(asset, digest=None)):
                with self.assertRaises(ValueError):
                    feeds.verify_digest(path, invalid)
            path.write_bytes(b'abd')
            with self.assertRaises(ValueError):
                feeds.verify_digest(path, asset)

    def test_windows_generator_accepts_local_and_ci_names(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for suffix in ('win32', 'windows-x64'):
                archive = root / f'fusex-{TAG}-{suffix}-setup.exe'
                archive.write_bytes(b'installer')
                output = root / 'appcast.xml'
                subprocess.run(['bash', str(feeds.ROOT / 'data/win32/generate-appcast.sh'), str(archive), str(output)],
                               env=dict(os.environ, PACKAGE_VERSION=TAG, RELEASE_TAG=TAG,
                                        APPCAST_PUB_DATE='Mon, 12 Oct 2026 12:00:00 +0000'),
                               check=True, stdout=subprocess.DEVNULL)
                generated, = feeds.feed_item(output)
                enclosure = generated.find('enclosure')
                self.assertTrue(enclosure.get('url').endswith(archive.name))
                self.assertEqual(enclosure.get('length'), '9')
                self.assertEqual(generated.findtext('pubDate'), 'Mon, 12 Oct 2026 12:00:00 +0000')
            wrong = root / 'arbitrary.exe'
            wrong.write_bytes(b'installer')
            rejected = subprocess.run(['bash', str(feeds.ROOT / 'data/win32/generate-appcast.sh'), str(wrong)],
                                      env=dict(os.environ, PACKAGE_VERSION=TAG), capture_output=True)
            self.assertNotEqual(rejected.returncode, 0)

    def test_generated_mac_signature_uses_apps_trusted_key(self):
        from cryptography.exceptions import InvalidSignature
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
        from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
        key = Ed25519PrivateKey.generate()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'fusepb').mkdir()
            with (root / 'fusepb/Info-FuseX.plist').open('wb') as target:
                plistlib.dump({'SUPublicEDKey': base64.b64encode(key.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)).decode()}, target)
            archive = root / feeds.installer_name(TAG, 'macos')
            archive.write_bytes(b'abc')
            (root / 'macos-build.json').write_text('{"release_version": "' + TAG + '", "build_number": 10}')
            signed = item(platform='macos', signature=base64.b64encode(key.sign(b'abc')).decode())
            output = root / 'generated.xml'
            write_feed(output, signed)
            with patch.object(feeds, 'ROOT', root), patch.object(feeds, 'WORK', root), patch.dict(os.environ, GH_REPO=REPO):
                feeds.validate_generated(output, {}, 'macos', TAG)
                archive.write_bytes(b'abd')
                with self.assertRaises(InvalidSignature):
                    feeds.validate_generated(output, {}, 'macos', TAG)
                archive.write_bytes(b'abc')
                signed.find(feeds.SPARKLE + 'version').text = '11'
                write_feed(output, signed)
                with self.assertRaises(ValueError):
                    feeds.validate_generated(output, {}, 'macos', TAG)
                signed.find('enclosure').set('url', 'https://example.org/fake.dmg')
                write_feed(output, signed)
                with self.assertRaises(ValueError):
                    feeds.validate_generated(output, {}, 'macos', TAG)

    def test_publisher_pushes_once_and_retry_is_idempotent(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            website = root / 'build/update-website'
            website.mkdir(parents=True)
            remote = root / 'remote.git'
            def git(*args):
                return subprocess.check_output(['git', *args], stderr=subprocess.DEVNULL, text=True).strip()
            git('init', '--bare', '--initial-branch=main', str(remote))
            git('-C', str(website), 'init', '--initial-branch=main')
            git('-C', str(website), 'config', 'user.name', 'Test')
            git('-C', str(website), 'config', 'user.email', 'test@example.org')
            (website / 'index.html').write_text('website')
            git('-C', str(website), 'add', '.')
            git('-C', str(website), 'commit', '-m', 'Initial website')
            git('-C', str(website), 'remote', 'add', 'origin', str(remote))
            git('-C', str(website), 'push', '-u', 'origin', 'main')
            work = root / 'build/update-feeds'
            work.mkdir()
            archive = work / feeds.installer_name(TAG, 'windows')
            archive.write_bytes(b'abc')
            write_feed(work / 'generated.xml', item())
            data = {'assets': [{'name': archive.name, 'size': 3, 'digest': 'sha256:' + hashlib.sha256(b'abc').hexdigest()}]}
            with patch.object(feeds, 'ROOT', root), patch.object(feeds, 'WORK', work), patch.object(feeds, 'release', return_value=data), patch.dict(os.environ, GH_REPO=REPO, RELEASE_READ_TOKEN='test', GITHUB_STEP_SUMMARY=str(root / 'summary')):
                with contextlib.redirect_stdout(io.StringIO()):
                    feeds.publish(data, TAG, 'windows')
                    first = git('-C', str(remote), 'rev-parse', 'main')
                    feeds.publish(data, TAG, 'windows')
                self.assertEqual(first, git('-C', str(remote), 'rev-parse', 'main'))
                self.assertEqual(git('-C', str(remote), 'show', '--pretty=', '--name-only', 'main'), 'updates/windows/appcast.xml')
                self.assertTrue((website / 'index.html').exists())


if __name__ == '__main__':
    unittest.main()
