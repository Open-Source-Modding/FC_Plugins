# FileHook

Logs every CreateFileW/CreateFileA the game makes to `FC6FileHook.log`
(next to the game exe), for post-run hash->name recovery.

## Install (FC6 example)
Option A — `make install` (uses your per-machine paths):
    make            # build dbdata.dll + PluginLoader.dll + FileHook.dll
    make install    # backs up dbdata.dll -> dbdata.old.dll (first time),
                    # copies dbdata.dll + PluginLoader.dll -> bin/,
                    # copies FileHook.dll -> plugins/

Create `Makefile.local` (gitignored) with your game paths first:
    GAME_DIR     = /path/to/Far Cry 6
    GAME_BIN     = $(GAME_DIR)/bin
    GAME_PLUGINS = $(GAME_DIR)/plugins

Option B — manual: rename bin/dbdata.dll -> bin/dbdata.old.dll, copy
build/dbdata.dll + build/PluginLoader.dll into bin/, copy build/FileHook.dll
into plugins/ (PluginLoader auto-loads .\plugins\*.dll).

Then launch the game, play/load to capture file opens, exit.

## Process the log
    python3 Scripts/fc6_filehook_process.py <path-to-FC6FileHook.log>
Outputs `fc6_recovered_from_hook.txt`: paths that are in a .fat archive but
not yet in the recovered filelists (raw CRC64 vs all FC6 fat hashes).
Append to `binary_recovered.filelist` and re-run RebuildFileLists.
