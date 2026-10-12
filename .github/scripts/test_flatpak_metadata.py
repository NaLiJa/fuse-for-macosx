"""Check displayed Flatpak release versions using the real AppStream document."""

from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

from flatpak_metadata import stamp_metadata


class MetadataTests(unittest.TestCase):
    def test_tag_becomes_current_release_without_losing_history(self):
        original = (Path(__file__).resolve().parents[2] /
                    'data/org.speccytools.FuseX.metainfo.xml').read_text()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'metadata.xml'
            path.write_text(original)
            stamp_metadata(path, '1.10.0-fusex-0.3', '2026-10-12')
            stamped = path.read_text()
            releases = ET.fromstring(stamped).find('releases')
            self.assertEqual(releases[0].get('version'), '1.10.0-fusex-0.3')
            self.assertEqual(releases[0].get('date'), '2026-10-12')
            self.assertEqual(ET.tostring(releases[1]),
                             ET.tostring(ET.fromstring(original).find('releases')[0]))
            self.assertIn('<!-- Copyright', stamped)
            stamp_metadata(path, '1.10.0-fusex-0.3', '2026-10-13')
            self.assertEqual(path.read_text(), stamped)
            for invalid in ('main', 'release"bad', '1.10.0-fusex-0.3\n'):
                with self.assertRaises(ValueError):
                    stamp_metadata(path, invalid, '2026-10-12')
                self.assertEqual(path.read_text(), stamped)


if __name__ == '__main__':
    unittest.main()
