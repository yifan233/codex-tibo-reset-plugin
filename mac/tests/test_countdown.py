import os
from pathlib import Path
import subprocess
import tempfile
import unittest

EXECUTABLE = str(Path(__file__).resolve().parents[1] / 'dist/Reset Countdown.app/Contents/MacOS/ResetCountdown')


class CountdownTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.env = {**os.environ, 'TIBO_RESET_STATE_DIR': self.temp.name}

    def command(self, *args, valid=True):
        result = subprocess.run([EXECUTABLE, *args], env=self.env, capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, valid, result.stderr)
        return result.stdout

    def test_empty_status(self):
        self.assertEqual(self.command('--status'), 'tibo=none\nsource=none\nlanguage=auto\n')
        self.assertEqual(list(Path(self.temp.name).iterdir()), [])

    def test_update_and_restart(self):
        result = self.command('--set', '--tibo', '2026-10-23T17:00:00Z', '--cards', '2026-11-06T00:00:00Z,2026-10-29T00:00:00Z', '--source', 'https://x.com/thsottiaux', '--language', 'en')
        self.assertEqual(self.command('--status'), result)
        changed = self.command('--set', '--language', 'zh')
        self.assertIn('tibo=2026-10-23T17:00:00Z', changed)
        self.assertIn('language=zh', changed)
        self.assertIn('card=2026-11-06T00:00:00Z', changed)
        self.assertIn('tibo=none', self.command('--set', '--tibo', 'none', '--cards', 'none'))
        self.assertEqual(self.command('--reset'), 'tibo=none\nsource=none\nlanguage=auto\n')

    def test_invalid_update_keeps_state(self):
        before = self.command('--set', '--language', 'en')
        for date in ['2026-02-29T00:00:00Z', '2026-04-31T00:00:00Z', '2026-01-01T24:00:00Z', '2026-01-01T00:00:00+14:01', '2026-01-01T00:00:00', '2026-01-01T00:00:00Z\n']:
            with self.subTest(date=date):
                self.command('--set', '--tibo', date, valid=False)
                self.assertEqual(self.command('--status'), before)
        for url in ['https://?bad', 'https://evil.test', 'https://x.com@evil.test', 'https://x.com:443/', 'https://x.com/\nbad']:
            self.command('--set', '--source', url, valid=False)
        self.assertEqual(self.command('--status'), before)

    def test_arguments_and_limits(self):
        for args in [('--set',), ('--set', '--tibo'), ('--set', '--unknown', 'x'), ('--set', '--language', 'fr'), ('--set', '--language', 'en', '--language', 'zh'), ('--set', '--cards', ''), ('--status', 'extra'), ('--configure', 'old.json')]:
            self.command(*args, valid=False)
        self.command('--set', '--cards', ','.join(['2026-10-23T17:00:00Z'] * 129), valid=False)

    def test_valid_dates(self):
        for date in ['2024-02-29T00:00:00Z', '2026-10-23T10:00:00-07:00', '2026-10-23T10:00:00.123Z']:
            self.assertIn('tibo=' + date, self.command('--set', '--tibo', date))

    def test_corrupt_state_recovery(self):
        path = Path(self.temp.name) / 'countdown.state'
        path.write_bytes(b'bad saved state')
        self.command('--status', valid=False)
        self.command('--help')
        self.command('--reset')
        self.assertIn('tibo=none', self.command('--status'))
        path.write_bytes(b'x' * 65537)
        self.command('--status', valid=False)
        self.command('--reset')


if __name__ == '__main__':
    unittest.main()
