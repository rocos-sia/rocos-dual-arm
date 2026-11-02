"""Install the package, copy consumer sources, build independently, run two processes."""
import argparse
from pathlib import Path
import shutil
import subprocess
import uuid

parser = argparse.ArgumentParser()
parser.add_argument('--build', required=True)
parser.add_argument('--dependencies', required=True)
parser.add_argument('--generator', required=True)
args = parser.parse_args()
build = Path(args.build).resolve()
workspace = build / 'external-acceptance'
source = workspace / 'source'
prefix = workspace / 'prefix'
shutil.copytree(Path(__file__).parent / 'external_app', source, dirs_exist_ok=True)

def run(*command):
    subprocess.run([str(arg) for arg in command], check=True)

run('cmake', '--install', build, '--prefix', prefix)
run('cmake', '-S', source, '-B', workspace / 'build', '-G', 'Ninja',
    f'-DCMAKE_PREFIX_PATH={prefix};{Path(args.dependencies).resolve()}',
    f'-DFASTDDSGEN_EXECUTABLE={Path(args.generator).resolve()}', '-DCMAKE_BUILD_TYPE=Release')
run('cmake', '--build', workspace / 'build', '-j', '2')
binary = workspace / 'build/independent_app'
namespace = 'external_' + uuid.uuid4().hex
server = subprocess.Popen([str(binary), 'server', namespace])
try:
    subprocess.run([str(binary), 'client', namespace], check=True, timeout=15)
finally:
    server.terminate()
    try:
        server.wait(timeout=5)
    except subprocess.TimeoutExpired:
        server.kill()
        server.wait(timeout=5)
