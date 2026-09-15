# FileHook

Logs every CreateFileW/CreateFileA the game makes to `FC6FileHook.log`
(next to the game exe), for post-run hash->name recovery.

## Install (FC6 example)
1. In the game `bin/` folder: rename `dbdata.dll` -> `dbdata.old.dll`,
   copy `build/dbdata.dll` + `build/PluginLoader.dll` there.
2. Copy `build/FileHook.dll` into the game `plugins/` folder
   (PluginLoader auto-loads `.dll` from `.\plugins\`).
3. Launch the game, play/load to capture file opens, exit.

## Process the log
    python3 Scripts/fc6_filehook_process.py <path-to-FC6FileHook.log>
Outputs `fc6_recovered_from_hook.txt`: paths that are in a .fat archive but
not yet in the recovered filelists (raw CRC64 vs all FC6 fat hashes).
Append to `binary_recovered.filelist` and re-run RebuildFileLists.
