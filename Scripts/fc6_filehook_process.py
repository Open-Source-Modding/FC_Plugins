#!/usr/bin/env python3
"""Process a FC6FileHook.log captured by the FC_Plugins FileHook plugin.

For each logged path, compute raw CRC64 (Gibbed table), check membership in the
set of all FC6 fat hashes, and report paths that are in a fat but NOT already in
the recovered filelists -> new recoverable names.

Usage:
  python3 fc6_filehook_process.py <FC6FileHook.log> [--out recovered.txt]
"""
import sys, os, re, struct, glob

def load_crc64_table():
    src = open('/home/selene/Documents/Code/game-tools/Ubisoft/Dunia/Gibbed.Dunia/projects/Gibbed.Dunia.FileFormats/Hashing/CRC64.cs').read()
    vals = re.findall(r'0x([0-9A-Fa-f]{16})ul', src)
    return [int(v, 16) for v in vals]

T = load_crc64_table()
def crc64(s):
    h = 0
    for ch in s:
        h = T[(h & 0xFF) ^ ord(ch)] ^ (h >> 8)
    return h & 0xFFFFFFFFFFFFFFFF
def swapped(v):
    return ((v & 0xFFFFFFFF) << 32) | (v >> 32)

def load_fat_hashes():
    base = '/home/selene/Games/ubisoft-connect/drive_c/Program Files (x86)/Ubisoft/Ubisoft Game Launcher/games/Far Cry 6/data_final'
    allh = set()
    for f in glob.glob(base + '/**/*.fat', recursive=True):
        d = open(f, 'rb').read()
        if len(d) < 24: continue
        if struct.unpack_from('<I', d, 0)[0] != 0x46415432: continue
        tot = struct.unpack_from('<I', d, 20)[0]
        o = 24
        for _ in range(tot):
            if o + 20 > len(d): break
            a, b, _, _ = struct.unpack_from('<IIII', d, o); o += 20
            allh.add((a << 32) | b)
    return allh

def load_known():
    known = set()
    for fl in glob.glob('/home/selene/Documents/Code/game-tools/Ubisoft/Dunia/Gibbed.Dunia/configs/Far Cry 6/files/**/*.filelist', recursive=True):
        try:
            for line in open(fl, encoding='utf-8', errors='ignore'):
                line = line.strip()
                if not line or line.startswith(';'): continue
                known.add(line.lower())
        except: pass
    return known

def main():
    if len(sys.argv) < 2:
        print(__doc__); return
    logfile = sys.argv[1]
    out = sys.argv[2][len('--out='):] if len(sys.argv) > 2 and sys.argv[2].startswith('--out=') else 'fc6_recovered_from_hook.txt'

    print('loading fat hashes...')
    allh = load_fat_hashes()
    print('loading known filelists...')
    known = load_known()
    known_hash = set(crc64(n) for n in known)
    print('fat hashes:', len(allh), '| known names:', len(known_hash))

    # parse log: <ms>\t<path>\n   (also handle a leading access flag if present)
    new = {}   # hash -> shortest path
    lines = 0
    with open(logfile, encoding='utf-8', errors='replace') as fh:
        for line in fh:
            line = line.rstrip('\n')
            if not line or '\t' not in line: continue
            parts = line.split('\t')
            path = parts[-1].strip()
            if not path: continue
            lines += 1
            p = path.lower().replace('/', '\\')
            # skip obvious non-archive paths (win32, drives, etc.) to keep it clean
            if not p.startswith(('ui\\', 'graphics\\', 'worlds\\', 'scripts\\', 'sound\\',
                                'audio\\', 'actionmaps\\', 'dictionaries\\', 'mission\\',
                                'sectors\\', 'configs\\', 'engine\\', 'wwise\\', 'shaders\\',
                                'nomad\\', 'd3d12\\', 'databases\\', 'animations\\', 'move\\',
                                'domino\\', 'dialog\\', 'entityarchetypeslibrary\\', 'generated\\',
                                'newparticles\\', 'sequences\\', 'activities\\')):
                continue
            h = swapped(crc64(p))
            if h in allh and h not in known_hash:
                if h not in new or len(p) < len(new[h]):
                    new[h] = p

    print('log lines:', lines, '| NEW unresolved-in-fat paths:', len(new))
    # save
    with open(out, 'w') as fh:
        fh.write('; %d new paths from FileHook capture\n' % len(new))
        for h in sorted(new):
            fh.write(new[h] + '\n')
    print('wrote', out)
    # preview
    for h, p in sorted(new.items())[:15]:
        print('  0x%016X  %s' % (h, p))

if __name__ == '__main__':
    main()
