"""Exercise tag versions, Apple counters and release provenance in real fixtures."""
import contextlib
import io
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from xcode_cloud_source import source_info
from release_version import stamp, release_version, build_number
from release_upload import verify_counter

ROOT = Path(__file__).resolve().parents[2]


class ReleaseTests(unittest.TestCase):
    def test_stamp_updates_build_contents_not_just_filename(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ('configure.ac', 'fusepb/config.h', 'fusepb/FuseX.xcodeproj/project.pbxproj'):
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / name, target)
            with contextlib.redirect_stdout(io.StringIO()):
                stamp(root, 'refs/tags/1.11.2-fusex-0.7', 42)
            configure = (root / 'configure.ac').read_text()
            self.assertIn('m4_define([fuse_version], [1.11.2-fusex-0.7])', configure)
            self.assertIn('m4_define([fuse_minor_version], [11])', configure)
            self.assertIn('#define VERSION "1.11.2-fusex-0.7"', (root / 'fusepb/config.h').read_text())
            self.assertIn('#define FUSE_RC_VERSION 1,11,2,7', (root / 'fusepb/config.h').read_text())
            project = (root / 'fusepb/FuseX.xcodeproj/project.pbxproj').read_text()
            self.assertEqual(project.count('CURRENT_PROJECT_VERSION = 42;'), 3)
            self.assertNotIn('1.10.0-fusex-0.2', project)

    def test_invalid_tags_and_colliding_counters_rejected(self):
        for tag in ('main', 'refs/heads/main', 'v1.10.0-fusex-0.2', '1.10.0-fusex-0.2\nBAD=1'):
            with self.assertRaises(ValueError):
                release_version(tag)
        for number in (0, 8, '9\nBAD=1', '9.0'):
            with self.assertRaises(ValueError):
                build_number(number)
        self.assertGreater(build_number(10), build_number(9))

    def test_older_resumed_build_cannot_replace_newer_release(self):
        candidate = dict(build_number=42, build_run_id='a', release_version='1.10.0-fusex-0.3')
        verify_counter(candidate, [('1.10.0-fusex-0.2', dict(build_number=41, build_run_id='b'))])
        verify_counter(candidate, [('1.10.0-fusex-0.3', candidate)])
        with self.assertRaises(ValueError):
            verify_counter(candidate, [('1.10.0-fusex-0.4', dict(build_number=43, build_run_id='c'))])

    def test_source_uses_tag_version_and_rejects_unmerged_code(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            def git(*args):
                return subprocess.check_output(['git', *args], cwd=root, stderr=subprocess.DEVNULL, text=True).strip()
            git('init', '-q')
            git('config', 'user.name', 'Release test')
            git('config', 'user.email', 'test@example.invalid')
            hook = root / 'fusepb/ci_scripts/release_version.py'
            hook.parent.mkdir(parents=True)
            hook.write_text('# release hook\n')
            git('add', '.')
            git('commit', '-qm', 'Reviewed source')
            reviewed = git('rev-parse', 'HEAD')
            git('update-ref', 'refs/remotes/origin/main', reviewed)
            git('tag', '1.10.0-fusex-0.3')
            import os
            previous = os.getcwd()
            try:
                os.chdir(root)
                self.assertEqual(source_info('refs/tags/1.10.0-fusex-0.3')['version'], '1.10.0-fusex-0.3')
                with self.assertRaises(ValueError):
                    source_info('refs/heads/main')
                hook.write_text('# unreviewed change\n')
                git('commit', '-qam', 'Unmerged source')
                git('tag', '1.10.0-fusex-0.4')
                with self.assertRaises(subprocess.CalledProcessError):
                    source_info('refs/tags/1.10.0-fusex-0.4')
            finally:
                os.chdir(previous)


if __name__ == '__main__':
    unittest.main()
