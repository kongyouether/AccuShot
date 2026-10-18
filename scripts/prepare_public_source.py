"""Export reviewed source paths without repository history or local evidence."""
import argparse
import json
from datetime import datetime
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import zipfile

ROOT_FILES = {'.gitignore', 'README.md', 'LICENSE', 'CMakeLists.txt',
              'CONTRIBUTING.md', 'generate_icon.py'}
SOURCE_DIRS = {'src', 'resources', 'scripts', 'tests', 'plugins', 'tools', 'openspec'}
# These legacy helpers upload to the upstream maintainer's hard-coded repository.
LOCAL_HELPERS = {'scripts/upload_release.py', 'tools/upload_release_asset.py'}
PUBLIC_IMAGES = {'workbench-light.png', 'workbench-dark.png', 'toolbar-light.png',
                 'plugins-light.png', 'plugins-dark.png'}


def category_allowed(name):
    path = PurePosixPath(name)
    if name in LOCAL_HELPERS:
        return False
    if name in ROOT_FILES:
        return True
    if path.parent == PurePosixPath('docs/images/readme'):
        return path.name in PUBLIC_IMAGES
    return len(path.parts) > 1 and path.parts[0] in SOURCE_DIRS


def export(root, output):
    def git(*args, data=None):
        return subprocess.run(['git', *args], cwd=root, input=data,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              check=True).stdout
    tracked = git('ls-files', '-z')
    new = git('ls-files', '--others', '--exclude-standard', '-z')
    names = sorted({p.decode('utf-8') for p in (tracked + new).split(b'\0') if p})
    # --no-index is essential: tracked private files can also match ignore rules.
    check = subprocess.run(['git', 'check-ignore', '--no-index', '-z', '--stdin'],
                           cwd=root, input=b'\0'.join(n.encode('utf-8') for n in names)+b'\0',
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if check.returncode not in (0, 1):
        raise RuntimeError('git check-ignore failed')
    ignored = {p.decode('utf-8') for p in check.stdout.split(b'\0') if p}
    selected = [n for n in names if n not in ignored and category_allowed(n)]
    output.mkdir(parents=True, exist_ok=False)
    destination = output / 'AccuShot-source'
    destination.mkdir()
    manifest = []
    for name in selected:
        source = root / name
        resolved = source.resolve()
        if source.is_symlink() or not resolved.is_relative_to(root):
            raise ValueError(f'External path or symlink is not allowed: {name}')
        if not source.is_file():
            continue  # A tracked file may have been deleted in the working tree.
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        manifest.append({'path': name, 'bytes': source.stat().st_size})
    archive = output / 'AccuShot-source.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
        for item in manifest:
            bundle.write(destination / item['path'], 'AccuShot-source/' + item['path'])
    (output / 'manifest.json').write_text(json.dumps({
        'included': manifest,
        'excluded': [n for n in names if n not in selected],
        'history_included': False,
    }, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'files': len(manifest), 'source_bytes': sum(x['bytes'] for x in manifest),
                      'zip_bytes': archive.stat().st_size, 'archive': str(archive)}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, help='New output directory; must not exist')
    args = parser.parse_args()
    repository = Path(__file__).resolve().parents[1]
    target = args.output or repository / 'build-evidence' / ('public-source-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
    export(repository, target.resolve())
