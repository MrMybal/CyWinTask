import contextlib
import importlib.util
import io
import pathlib
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('distribution_privacy', pathlib.Path(__file__).resolve().parents[1] / 'scripts' / 'check-distribution-privacy.py')
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)

class DistributionPrivacyTests(unittest.TestCase):
    def check_silently(self, path):
        with contextlib.redirect_stdout(io.StringIO()):
            return checker.check(path)

    def test_clean_synthetic_package(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = pathlib.Path(tmp)
            (target / 'README.txt').write_text('Fictional demonstration package.')
            self.assertTrue(self.check_silently(target))

    def test_private_path_in_binary(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = pathlib.Path(tmp)
            fictional = 'C:' + '/Users/' + 'ExampleUser' + '/Documents/example.txt'
            (target / 'sample.dll').write_bytes(b'MZ' + fictional.encode('utf-16le'))
            self.assertFalse(self.check_silently(target))

    def test_diagnostics_in_zip(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = pathlib.Path(tmp) / 'sample.zip'
            with zipfile.ZipFile(target, 'w') as archive:
                archive.writestr('runtime-diagnostics.json', '{"example": true}')
            self.assertFalse(self.check_silently(target))

    def test_zip_traversal(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = pathlib.Path(tmp) / 'sample.zip'
            with zipfile.ZipFile(target, 'w') as archive:
                archive.writestr('../outside.txt', 'fictional')
            self.assertFalse(self.check_silently(target))

if __name__ == '__main__':
    unittest.main()
