#!/usr/bin/env python3
"""Exercise wrapper refusal and real downloader GET/POST/failure paths offline."""
import http.server
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading

root = Path(__file__).resolve().parent.parent
for file in ('launch.sh', 'launch-cart.sh', 'bin/pico8-launch'):
    subprocess.run(['sh', '-n', str(root / 'pak' / file)], check=True)
with tempfile.TemporaryDirectory(prefix='pico8-check-') as temporary:
    work = Path(temporary)
    pak = work / 'PICO8.pak'
    shutil.copytree(root / 'pak', pak)
    message = pak / 'bin/pico8-message'
    message.write_text('#!/bin/sh\nprintf "%s" "$1" > "$MESSAGE"\n')
    message.chmod(0o755)
    env = dict(os.environ, MESSAGE=str(work / 'message'))
    env.pop('UMRK_PICO8_SESSION', None)
    result = subprocess.run([str(pak / 'launch.sh')], env=env, capture_output=True)
    assert result.returncode == 1
    assert 'Update Leaf' in (work / 'message').read_text()
    native = work / 'purchased files'
    native.mkdir()
    env.update({name: str(work / name) for name in (
        'UMRK_PICO8_HOME_PATH', 'UMRK_PICO8_ROOT_PATH', 'UMRK_PICO8_DESKTOP_PATH')})
    env['UMRK_PICO8_RUNTIME_PATH'] = str(native)
    # --check never queries a waiting daemon, writes native state, or starts UI.
    (work / 'message').unlink()
    for content in (None, b'not ELF', b'\x7fELF\x01\x01' + b'\0' * 60):
        if content is not None:
            exe = native / 'pico8_64'
            exe.write_bytes(content)
            exe.chmod(0o755)
            (native / 'pico8.dat').write_bytes(b'CPODfixture')
        result = subprocess.run([str(pak / 'launch-cart.sh'), '--check'], env=env, capture_output=True)
        assert result.returncode == 1 and b'Raspberry Pi' in result.stderr
        assert not (work / 'message').exists()
        assert not Path(env['UMRK_PICO8_HOME_PATH']).exists()

    # The dialog must build warning-free wherever SDL2_ttf is available; its
    # visuals are checked on device.
    sdl = subprocess.run(['pkg-config', '--cflags', '--libs', 'sdl2', 'SDL2_ttf'], capture_output=True, text=True)
    if sdl.returncode == 0:
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(root / 'src/message.c'),
                        *sdl.stdout.split(), '-o', str(work / 'pico8-message')], check=True)
    else:
        print('SKIP: pico8-message host build (no SDL2_ttf pkg-config)')

    binary = work / 'wget'
    flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'libcurl'], text=True).split()
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(root / 'src/wget.c'), *flags, '-o', str(binary)], check=True)

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass
        def do_GET(self):
            self.send_response(404 if self.path == '/missing' else 200)
            self.end_headers()
            self.wfile.write(b'cart bytes')
        def do_POST(self):
            data = self.rfile.read(int(self.headers['Content-Length']))
            self.send_response(200)
            self.end_headers()
            self.wfile.write(data)
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        url = f'http://127.0.0.1:{server.server_port}'
        output = work / 'cart with spaces.png'
        subprocess.run([str(binary), url, '-q', '-O', str(output)], check=True)
        assert output.read_bytes() == b'cart bytes'
        post = work / 'post body'
        post.write_bytes(b'field=hello\x00world')
        subprocess.run([str(binary), url, '-q', '-O', str(output), '--post-file=' + str(post)], check=True)
        assert output.read_bytes() == post.read_bytes()
        result = subprocess.run([str(binary), url + '/missing', '-q', '-O', str(output)], capture_output=True)
        assert result.returncode != 0 and output.read_bytes() == post.read_bytes()
        for args in ([], ['-O'], ['--no-check-certificate'], ['file:///etc/passwd', '-O', str(output)],
                     [url, url, '-O', str(output)], [url, '-O', str(output), '--post-file=']):
            assert subprocess.run([str(binary), *args], capture_output=True).returncode != 0
        assert not list(work.glob('*.part-*'))
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
print('PASS: old-launcher refusal, missing/wrong runtime, read-only preflight, GET/POST, failure keeps cache')
