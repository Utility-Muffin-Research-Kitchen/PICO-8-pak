#!/usr/bin/env python3
"""Offline import/update and crash-recovery checks with real PNG containers."""
import hashlib
import os
from pathlib import Path
import shutil
import sqlite3
import struct
import subprocess
import tempfile
import zlib

root = Path(__file__).resolve().parent.parent

def png(value):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    data = (b'\0' + bytes([value, 2, 3, 255]) * 160) * 205
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 160, 205, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(data)) + chunk(b'IEND', b'')

def favourite(mid, rev=0, title='Same / title: stars ★'):
    # Seven pipe-delimited columns observed on Raspberry Pi PICO-8 0.2.7.
    return f'|{mid}-{rev:<10}|{mid:<20}|1794   |author           |                     |{title}\n'

with tempfile.TemporaryDirectory(prefix='pico8-import-check-') as temporary:
    work = Path(temporary)
    flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'libpng', 'openssl', 'sqlite3'], text=True).split()
    binary = work / 'import'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(root/'src/import.c'), *flags, '-lz', '-o', str(binary)], check=True)
    # Fault injection is compiled only into this test executable. Production
    # has no environment variable that can interrupt or damage an import.
    fault = work / 'fault.c'
    fault.write_text('''#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#define _XOPEN_SOURCE 700
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static ssize_t faulty_write(int fd, const void *p, size_t n) {
    const char *mode=getenv("FAULT");
    if (mode && !strcmp(mode,"full")) { errno=ENOSPC; return -1; }
    if (mode && !strcmp(mode,"partial")) { write(fd,p,n>7?7:n); _exit(99); }
    return write(fd,p,n);
}
static int faulty_rename(const char *a,const char *b) {
    const char *mode=getenv("FAULT");
    if (mode && strstr(b,".p8.png")) {
        if (!strcmp(mode,"before")) _exit(99);
        int rc=rename(a,b);
        if (!strcmp(mode,"after")) _exit(99);
        return rc;
    }
    return rename(a,b);
}
#define write faulty_write
#define rename faulty_rename
#include "''' + str(root/'src/import.c') + '''"
''')
    broken = work / 'fault'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(fault), *flags, '-lz', '-o', str(broken)], check=True)
    home = work/'primary/.userdata/mlp1/pico8'
    roms = work/'primary/Roms/PICO8'
    cache = home/'bbs/carts'
    cache.mkdir(parents=True); roms.mkdir(parents=True)
    env = dict(os.environ, UMRK_PICO8_HOME_PATH=str(home), UMRK_PICO8_ROOT_PATH=str(roms))
    fav = home/'favourites.txt'
    dest = roms/'Splore/leaf_import_a.p8.png'
    journal = home/'splore-imports.sqlite3'
    report = home/'splore-library.tsv'
    def run(code=0, fault=None):
        result = subprocess.run([str(broken if fault else binary)], env=dict(env, **({'FAULT':fault} if fault else {})), capture_output=True)
        assert result.returncode == code, (result.returncode, result.stderr.decode())
        return result
    def state(mid='leaf_import_a'):
        with sqlite3.connect(journal) as db:
            return db.execute('SELECT revision,hash,pending_revision FROM imports WHERE id=?', (mid,)).fetchone()
    fav.write_text(favourite('leaf_import_a') + favourite('leaf_import_b'))
    (cache/'leaf_import_a-0.p8.png').write_bytes(png(1))
    (cache/'leaf_import_b-0.p8.png').write_bytes(png(2))
    (cache/'never_favourited-0.p8.png').write_bytes(png(3))
    run()
    assert dest.read_bytes() == png(1) and len(list((roms/'Splore').glob('*.png'))) == 2
    assert 'Same / title: stars ★' in report.read_text()
    old = dest.stat().st_mtime_ns
    run(); assert dest.stat().st_mtime_ns == old
    # Existing ID-only titles get readable labels without renaming carts.
    fav.write_text(favourite('leaf_import_a', title='leaf_import_a') +
                   favourite('leaf_import_b', title='Super_Title 2048!'))
    run()
    assert 'leaf_import_a\tLeaf Import A\n' in report.read_text()
    assert 'leaf_import_b\tSuper_Title 2048!\n' in report.read_text()
    assert dest.stat().st_mtime_ns == old
    # Numbers belong to the fallback, and this also works after unfavoriting.
    with sqlite3.connect(journal) as db:
        db.execute("INSERT INTO imports(id,title,favourite) VALUES('pilatro_35','pilatro_35',1)")
    (cache/'pilatro_35-0.p8.png').write_bytes(png(6))
    fav.write_text(favourite('pilatro_35', title='pilatro_35'))
    run()
    assert 'pilatro_35\tPilatro 35\n' in report.read_text()
    # Unfavouriting keeps the copy and its update eligibility.
    fav.unlink()
    (cache/'leaf_import_a-1.p8.png').write_bytes(png(4))
    run(); assert dest.read_bytes() == png(4) and state()[0] == 1
    assert 'pilatro_35\tPilatro 35\n' in report.read_text()
    # Highest numeric revision, not lexical order. Updates preserve the path.
    (cache/'leaf_import_a-10.p8.png').write_bytes(png(10))
    (cache/'leaf_import_a-9.p8.png').write_bytes(png(9))
    run(); assert dest.read_bytes() == png(10) and state()[0] == 10
    (cache/'leaf_import_a-10.p8.png').unlink()
    run(); assert dest.read_bytes() == png(10)
    # Manual edits, unrelated collisions, and invalid PNGs are never replaced.
    dest.write_bytes(b'user edit')
    (cache/'leaf_import_a-11.p8.png').write_bytes(png(11))
    run(1); assert dest.read_bytes() == b'user edit' and 'leaf_import_a' not in report.read_text()
    dest.write_bytes(png(10))
    (cache/'leaf_import_a-11.p8.png').write_bytes(png(11)[:-12])
    run(1); assert dest.read_bytes() == png(10) and state()[0] == 10
    (cache/'leaf_import_a-11.p8.png').write_bytes(png(11))
    for failure in ('full','partial','before','after'):
        run(1 if failure=='full' else 99, failure)
        assert state()[0] == 10 and state()[2] == 11
        assert dest.read_bytes() == (png(11) if failure=='after' else png(10))
    run(); assert state()[0] == 11 and state()[2] == -1
    assert state()[1] == hashlib.sha256(png(11)).hexdigest()
    # Cache absent for new favourite: no placeholder; later successful retry.
    fav.write_text(favourite('missing'))
    run(1); assert not (roms/'Splore/missing.p8.png').exists()
    (cache/'missing-0.p8.png').write_bytes(png(13))
    run(); assert (roms/'Splore/missing.p8.png').exists()
    (roms/'Splore/missing.p8.png').unlink()
    run(); assert (roms/'Splore/missing.p8.png').exists()
    # Deleted unfavourited copies stay deleted.
    fav.unlink(); dest.unlink(); run(); assert not dest.exists()
    # Names in untrusted metadata cannot escape controlled roots.
    fav.write_text(favourite('../escape') + favourite('collision'))
    (cache/'collision-0.p8.png').write_bytes(png(4))
    collision = roms/'Splore/collision.p8.png'; collision.write_bytes(png(5))
    run(1); assert collision.read_bytes() == png(5)
    fav.write_text(favourite('symlink'))
    (cache/'symlink-0.p8.png').symlink_to(cache/'missing-0.p8.png')
    run(1); assert not (roms/'Splore/symlink.p8.png').exists()
    # Simulate changed mount roots: journal/report contain no old absolute paths.
    fav.write_text(favourite('missing'))
    moved = work/'different-mount'
    shutil.move(work/'primary', moved)
    home=moved/'.userdata/mlp1/pico8'; roms=moved/'Roms/PICO8'
    env.update(UMRK_PICO8_HOME_PATH=str(home), UMRK_PICO8_ROOT_PATH=str(roms))
    # An unrelated collision stays reported but cannot stop other imports.
    run(1)
    assert (roms/'Splore/missing.p8.png').read_bytes() == png(13)
print('PASS: favourites, revisions, permanent copies, collisions, PNG validation, full card, interrupted writes, recovery, mount changes')
