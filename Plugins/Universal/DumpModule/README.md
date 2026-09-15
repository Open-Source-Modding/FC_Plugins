# DumpModule

Captures the LIVE (post-unpack) image of a target module and writes it to disk,
for runtime disassembly.

## Why
FC6's main DLL (`FC_m64d3d12.dll`) is Denuvo-packed + VMProtect (`.vmp0`/`.vmp1`
sections). The on-disk `.code` section is encrypted stubs; real instructions
only exist in memory AFTER the runtime unpacker runs. Static `objdump` on the
on-disk file yields nothing. DumpModule reads the module from its own address
space once unpacking completes, so the resulting file disassembles cleanly
(VAs preserved — image base `0x180000000`).

## Output
`<ModuleName>.dump.dll` beside the game exe (e.g. `FC_m64d3d12.dll.dump.dll`).
Disassemble with:
    x86_64-w64-mingw32-objdump -d --start-address=0x180575D000 \
        --stop-address=0x180575D05A <dump>.dump.dll
(mapping function addresses from `FC6 Dunia2 - Functions Dump.dart`.)

## Configure (preprocessor defines)
    TARGET_MODULE   - module filename (default FC_m64d3d12.dll)
    DUMP_DELAY_MS   - initial wait after init (default 15000)
    DUMP_TIMEOUT_MS - max total wait (default 120000)
    DUMP_WHOLE      - 1 = whole image, 0 = .code only (default 1)

## Install
`make install` copies DumpModule.dll into the game `plugins/` folder
(PluginLoader auto-loads `.dll` from `.\plugins\`).
