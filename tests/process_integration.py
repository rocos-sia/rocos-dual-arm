"""Exercise real UDP between processes; kill and restart the service process."""
import concurrent.futures
import selectors
import subprocess
import sys
import uuid

binary = sys.argv[1]
namespace = 'process_' + uuid.uuid4().hex
args = ['192', namespace]

def start():
    return subprocess.Popen([binary, 'server', *args], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)

def stop(process):
    if process.poll() is None:
        process.terminate()
    try:
        process.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.communicate(timeout=5)

def client():
    result = subprocess.run([binary, 'client', *args], capture_output=True, text=True, timeout=25)
    if result.returncode or 'CLIENT_OK' not in result.stdout:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout.strip()

server = start()
watcher = None
try:
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        results = list(pool.map(lambda _: client(), range(2)))
    old_instance = results[0].split('instance=')[1].split()[0]
    print('\n'.join(results))
    watcher = subprocess.Popen([binary, 'watch', *args], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)
    with selectors.DefaultSelector() as selector:
        selector.register(watcher.stdout, selectors.EVENT_READ)
        if not selector.select(timeout=12):
            raise RuntimeError('persistent client did not become ready')
        ready = watcher.stdout.readline().strip()
        if not ready.startswith('WATCH_READY '):
            raise RuntimeError('unexpected watcher output: ' + ready)
        print(ready)
    server.kill()
    server.communicate(timeout=5)
    server = start()
    restarted = client()
    new_instance = restarted.split('instance=')[1].split()[0]
    if old_instance == new_instance:
        raise RuntimeError('process restart reused identity')
    print(restarted)
    recovered, _ = watcher.communicate(timeout=40)
    if watcher.returncode or 'WATCH_RECOVERED' not in recovered:
        raise RuntimeError('persistent client recovery failed: ' + recovered)
    print(recovered.strip())
    print('PASS two clients, SIGKILL, restart, persistent-client recovery, pubsub and services')
finally:
    if watcher is not None:
        stop(watcher)
    stop(server)
