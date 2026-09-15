"""Local build metadata and publication. Standard library only; no source edits."""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def write_changed(path, data):
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def git(*args):
    try:
        result = subprocess.run(['git', *args], capture_output=True, text=True)
    except OSError:
        return 'unavailable'
    return result.stdout.strip() if result.returncode == 0 else 'unavailable'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['configure', 'publish', 'clean'])
    parser.add_argument('--directory', type=pathlib.Path, required=True)
    parser.add_argument('--target', type=pathlib.Path)
    args = parser.parse_args()
    directory = args.directory
    if args.action == 'clean':
        target = directory.resolve()
        allowed_parents = [pathlib.Path('build').resolve(), pathlib.Path('pgo-data').resolve()]
        if target.parent not in allowed_parents or directory.is_symlink():
            raise ValueError('Refusing cleanup outside one build/profile configuration')
        if target.exists():
            shutil.rmtree(target)
        return
    config_path = directory / 'configuration.json'
    if args.action == 'configure':
        request = (directory / 'request.txt').read_text().splitlines()
        compiler = subprocess.run([request[0], '--version'], capture_output=True, text=True, check=True).stdout
        files = sorted(p for p in pathlib.Path('.').iterdir()
                       if p.is_file() and (p.suffix in ('.cpp', '.h', '.py') or p.name == 'Makefile'))
        sources = {p.name: digest(p) for p in files}
        identity = hashlib.sha256(json.dumps(sources, sort_keys=True).encode()).hexdigest()
        config = dict(compiler=compiler.strip(), command=request[0], flags=request[1:],
                      commit=git('rev-parse', 'HEAD'), dirty=bool(git('status', '--porcelain', '--', '.')),
                      source_sha256=identity, sources=sources)
        # Profiles are inputs too: never reuse optimized objects from an older workload.
        profile_flags = ' '.join(request[1:])
        import re
        match = re.search(r'-fprofile-use=([^\s]+)', profile_flags)
        if match:
            profile = pathlib.Path(match.group(1))
            profiles = [profile] if profile.is_file() else sorted(profile.rglob('*.gcda'))
            if not profiles:
                raise RuntimeError('PGO use requested without profile data')
            config['profiles'] = {str(p): digest(p) for p in profiles}
        serialized = json.dumps(config, sort_keys=True, indent=2).encode()
        write_changed(config_path, serialized)
        build_id = hashlib.sha256(serialized).hexdigest()
        header = '#pragma once\n#define DEEPBECKY_BUILD_ID ' + json.dumps(build_id) + '\n'
        write_changed(directory / 'buildinfo.h', header.encode())
    else:
        binary = directory / args.target.name
        if not args.target.exists() or digest(binary) != digest(args.target):
            shutil.copy2(binary, args.target)
        config = json.loads(config_path.read_text())
        config['binary'] = dict(path=str(args.target), sha256=digest(args.target), bytes=args.target.stat().st_size)
        network = pathlib.Path('db-leela500m-v5.nnue')
        config['distributed_network'] = dict(path=str(network), sha256=digest(network)) if network.exists() else None
        manifest = pathlib.Path(str(args.target) + '.manifest.json')
        if manifest.exists():
            previous = json.loads(manifest.read_text())
            if previous.get('binary') == config['binary'] and 'benchmark' in previous:
                config['benchmark'] = previous['benchmark']
        write_changed(manifest,
                      json.dumps(config, sort_keys=True, indent=2).encode())


if __name__ == '__main__':
    main()
