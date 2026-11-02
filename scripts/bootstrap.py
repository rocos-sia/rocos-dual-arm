#!/usr/bin/env python3
"""Build locked upstream dependencies locally. Requires JDK >=17, CMake, Ninja, GCC, git."""
import json
from pathlib import Path
import subprocess
import argparse

ROOT = Path(__file__).resolve().parents[1]

def run(*args, cwd=ROOT):
    print('+', ' '.join(str(a) for a in args), flush=True)
    subprocess.run([str(a) for a in args], cwd=cwd, check=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    lock = json.loads((ROOT / 'dependencies.lock.json').read_text())
    prefix = ROOT / 'work/deps-install'
    src = ROOT / 'work/deps-src'
    src.mkdir(parents=True, exist_ok=True)
    for name, spec in lock['repositories'].items():
        path = src / name
        if not path.exists():
            run('git', 'clone', '--depth', '1', '--branch', spec['tag'], spec['url'], path)
        actual = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=path, text=True).strip()
        if actual != spec['commit']:
            raise SystemExit(f'{name}: expected {spec["commit"]}, found {actual}; refusing to overwrite')
        run('git', 'diff', '--quiet', cwd=path)
        run('git', 'diff', '--cached', '--quiet', cwd=path)
        if name == 'fastdds':
            run('git', 'submodule', 'update', '--init', '--recursive', '--', 'thirdparty/asio', 'thirdparty/tinyxml2', cwd=path)
        elif name == 'fastddsgen':
            run('git', 'submodule', 'update', '--init', '--recursive', cwd=path)
        if name == 'fastddsgen':
            run('./gradlew', 'assemble', '--no-daemon', cwd=path)
            continue
        build = ROOT / 'work/deps-build' / ('memory-pinned' if name == 'memory' else name)
        options = ['-DBUILD_TESTING=OFF', '-DBUILD_SHARED_LIBS=ON', '-DCMAKE_INSTALL_RPATH=$ORIGIN']
        if name == 'memory':
            options += ['-DFOONATHAN_MEMORY_BUILD_TOOLS=OFF', '-DFOONATHAN_MEMORY_BUILD_TESTS=OFF', '-DFOONATHAN_MEMORY_BUILD_EXAMPLES=OFF']
        if name == 'fastdds':
            options += ['-DCOMPILE_EXAMPLES=OFF', '-DTHIRDPARTY=ON']
        run('cmake', '-S', path, '-B', build, '-G', 'Ninja',
            f'-DCMAKE_INSTALL_PREFIX={prefix}', f'-DCMAKE_PREFIX_PATH={prefix}', *options)
        run('cmake', '--build', build, '-j', args.jobs)
        run('cmake', '--install', build)
    print('Dependencies ready. Run: cmake --preset dev && cmake --build --preset dev')

if __name__ == '__main__':
    main()
