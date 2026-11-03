#!/usr/bin/env python3
"""Localhost-only checks of the actual interface-bound probe."""
from pathlib import Path
import os, socket, subprocess, threading, time
here = Path(__file__).resolve().parent
out = here / 'tests/out'
out.mkdir(exist_ok=True)
exe = out / 'sweeten2-host'
env = dict(os.environ, TMPDIR=str(out))
subprocess.run(['cc', '-save-temps=obj', '-DSWEETEN2_HOST_TEST', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', str(here/'sweeten2.c'), '-o', str(exe)], check=True, env=env)
def run(reply, want):
    with socket.socket() as server:
        server.bind(('127.0.0.1', 0)); server.listen(1); server.settimeout(15)
        def answer():
            with server.accept()[0] as conn:
                conn.settimeout(15)
                req = conn.recv(1024)
                assert req.startswith(b'HEAD / HTTP/1.1\r\n')
                if reply is None:
                    time.sleep(11)
                else:
                    for part in reply:
                        conn.sendall(part); time.sleep(.01)
        worker = threading.Thread(target=answer)
        worker.start()
        start = time.monotonic()
        result = subprocess.run([str(exe), 'lo0', '127.0.0.1', str(server.getsockname()[1])], capture_output=True, text=True, timeout=13)
        elapsed = time.monotonic()-start
        worker.join(15)
        assert not worker.is_alive()
        assert result.returncode == want, result.stdout+result.stderr
        if reply is None:
            assert 9 <= elapsed < 12 and 'errno=60' in result.stderr, result.stderr
        if not want:
            assert 'PASS interface=lo0' in result.stdout and 'HTTP=204' in result.stdout
run([b'HTTP/1.', b'1 204 No Content\r\n\r\n'], 0)
run([b'garbage\r\n'], 1)
run([b'HTTP/1.1 200'+b'x'*68], 1)
run(None, 1)
r = subprocess.run([str(exe), 'no-such-interface', '127.0.0.1', '80'], capture_output=True, timeout=2)
assert r.returncode == 1 and b'PASS' not in r.stdout
print('PASS: actual bound socket, fragmented status, malformed/truncated response, missing interface and monotonic total deadline')
