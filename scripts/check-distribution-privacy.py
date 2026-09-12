"""Review distribution files and ZIP contents without printing sensitive values."""
import argparse
import importlib.util
import pathlib
import re
import zipfile

spec = importlib.util.spec_from_file_location('repository_privacy', pathlib.Path(__file__).with_name('check-repository-privacy.py'))
privacy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(privacy)


def entries(target):
    if target.is_dir():
        for item in sorted(target.rglob('*')):
            if item.is_file():
                yield item.relative_to(target).as_posix(), item.read_bytes()
    else:
        with zipfile.ZipFile(target) as archive:
            for item in archive.infolist():
                if not item.is_dir():
                    yield item.filename, archive.read(item)


def check(target):
    issues = []
    count = 0
    for name, data in entries(target):
        count += 1
        for reason in privacy.inspect(name, data):
            issues.append(f'{name}: {reason}')
        path = pathlib.PurePosixPath(name)
        if (path.is_absolute() or '..' in path.parts or
                re.search(r'(?i)(diagnostics\.json$|(^|/)preview[^/]*\.png$|\.(pdb|log|binlog|dmp|etl)$)', name) or
                any(part in {'.git', '.cyrevision', '.codex', 'obj', 'Tests'} for part in path.parts)):
            issues.append(f'{name}: excluded distribution file')
    for issue in issues:
        print(issue)
    if not issues:
        print(f'Distribution privacy checks passed: {count} files.')
    return not issues


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target', type=pathlib.Path, help='Extracted distribution directory or ZIP')
    args = parser.parse_args()
    raise SystemExit(0 if check(args.target) else 1)
