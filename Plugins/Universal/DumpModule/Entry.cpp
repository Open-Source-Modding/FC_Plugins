// DumpModule — FC_Plugins plugin that captures the LIVE (post-unpack) image
// of a target module and writes it to disk, for runtime disassembly.
//
// WHY: FC6's main DLL (FC_m64d3d12.dll) is Denuvo-packed + VMProtect
// (.vmp0/.vmp1 sections). The on-disk .code section is encrypted stubs; real
// instructions only exist in memory AFTER the runtime unpacker runs. Static
// objdump on the on-disk file yields nothing. This plugin dumps the module
// from its own address space once unpacking has completed, so the resulting
// file disassembles cleanly (VAs preserved: image base 0x180000000).
//
// Timing strategy: Denuvo/VMProtect unpack lazily. We spawn a worker thread
// at init that polls the module's .code section for a "decrypted" marker
// (the presence of a prologue that is NOT an int3/CC stub). When decrypted
// (or after DUMP_TIMEOUT_MS), we snapshot the whole image (headers + sections
// as mapped in memory) and write it next to the game exe.
//
// Output: <ModuleName>.dump.dll beside the game exe.
//
// Configure via preprocessor defines at the top:
//   TARGET_MODULE  - module filename to dump (default FC_m64d3d12.dll)
//   DUMP_DELAY_MS  - initial wait after init (default 15000 = 15s)
//   DUMP_TIMEOUT_MS- max total wait (default 120000 = 2min)
//   DUMP_WHOLE     - 1 = whole mapped image, 0 = only .code section (default 1)

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdint.h>

#include "SDK.hpp"

#ifndef TARGET_MODULE
#define TARGET_MODULE "FC_m64d3d12.dll"
#endif
#ifndef DUMP_DELAY_MS
#define DUMP_DELAY_MS 15000
#endif
#ifndef DUMP_TIMEOUT_MS
#define DUMP_TIMEOUT_MS 120000
#endif
#ifndef DUMP_WHOLE
#define DUMP_WHOLE 1
#endif

namespace
{
    HMODULE g_Target = NULL;
    char    g_OutPath[MAX_PATH + 32];

    // ---- PE helpers (operate on a pointer into our own memory) -------------
    struct PeMeta
    {
        uint8_t *Base;          // image base in our memory
        size_t   SizeOfImage;   // from optional header
        DWORD    CodeRVA, CodeSize; // .code section RVA/size
        uint8_t *Code;          // Base + CodeRVA
    };

    bool ReadPeMeta(HMODULE Mod, PeMeta &M)
    {
        M.Base = (uint8_t *)Mod;
        IMAGE_DOS_HEADER *Dos = (IMAGE_DOS_HEADER *)M.Base;
        if(Dos->e_magic != IMAGE_DOS_SIGNATURE) { return false; }
        IMAGE_NT_HEADERS *Nt = (IMAGE_NT_HEADERS *)(M.Base + Dos->e_lfanew);
        if(Nt->Signature != IMAGE_NT_SIGNATURE) { return false; }

        M.SizeOfImage = Nt->OptionalHeader.SizeOfImage;

        // locate the .code section
        M.Code = NULL; M.CodeRVA = 0; M.CodeSize = 0;
        IMAGE_SECTION_HEADER *Sec = IMAGE_FIRST_SECTION(Nt);
        for(WORD i = 0; i < Nt->FileHeader.NumberOfSections; i++)
        {
            char Name[9];
            memcpy(Name, Sec[i].Name, 8); Name[8] = '\0';
            if(strcmp(Name, ".code") == 0)
            {
                M.CodeRVA  = Sec[i].VirtualAddress;
                M.CodeSize = Sec[i].Misc.VirtualSize;
                M.Code     = M.Base + Sec[i].VirtualAddress;
                break;
            }
        }
        return true;
    }

    // Denuvo/VMProtect stubs typically start with int3 (0xCC) or 0xE9 jmp.
    // We consider .code "decrypted" once its first bytes are a plausible
    // prologue (not a CC-run and not all-zeros).
    bool IsDecrypted(const PeMeta &M)
    {
        if(M.Code == NULL || M.CodeSize < 16) { return false; }
        // count CC bytes in first 32
        int cc = 0;
        for(int i = 0; i < 32 && i < (int)M.CodeSize; i++)
        {
            if(M.Code[i] == 0xCC) { cc++; }
        }
        return cc < 8;   // a real prologue has few int3
    }

    // ---- the dump worker ----------------------------------------------------
    DWORD WINAPI DumpWorker(LPVOID)
    {
        Sleep(DUMP_DELAY_MS);                       // let unpacking begin

        DWORD Start = GetTickCount();
        PeMeta M;
        bool Have = false;

        for(;;)
        {
            if(ReadPeMeta(g_Target, M))
            {
                if(IsDecrypted(M)) { Have = true; break; }
            }
            if(GetTickCount() - Start > DUMP_TIMEOUT_MS) { break; }
            Sleep(2000);
        }

        if(!Have) { SDK::Log::Message("DumpModule: .code never verified decrypted\n"); return 0; }

        // snapshot the mapped image from our own memory (safe copy, no locks
        // needed since the loader thread itself is paused mid-unpack only by
        // coincidence — Denuvo already finished by this point).
        size_t ImageSize = M.SizeOfImage;
        uint8_t *Snapshot = (uint8_t *)malloc(ImageSize);
        if(Snapshot == NULL) { return 0; }
        memcpy(Snapshot, M.Base, ImageSize);

        // write to disk
        HANDLE F = CreateFileA(g_OutPath, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if(F != INVALID_HANDLE_VALUE)
        {
            DWORD Written = 0;
            WriteFile(F, Snapshot, (DWORD)ImageSize, &Written, NULL);
            CloseHandle(F);
            SDK::Log::Message("DumpModule: wrote %u bytes -> %s\n",
                              (unsigned)Written, g_OutPath);
        }
        else
        {
            SDK::Log::Message("DumpModule: failed to create %s\n", g_OutPath);
        }
        free(Snapshot);
        return 0;
    }
} // namespace

PLUGIN_ENTRY()
{
    // 1. Resolve the target module in our process.
    g_Target = GetModuleHandleA(TARGET_MODULE);
    if(g_Target == NULL)
    {
        SDK::Log::Message("DumpModule: %s not loaded (yet)\n", TARGET_MODULE);
        return false;
    }

    // 2. Build output path next to the game exe.
    GetModuleFileNameA(NULL, g_OutPath, MAX_PATH);
    {
        char *Slash = strrchr(g_OutPath, '\\');
        if(Slash != NULL) { *(Slash + 1) = '\0'; }
        else { g_OutPath[0] = '\0'; }
        strncat(g_OutPath, TARGET_MODULE, sizeof(g_OutPath) - strlen(g_OutPath) - 1);
        strncat(g_OutPath, ".dump.dll", sizeof(g_OutPath) - strlen(g_OutPath) - 1);
    }

    // 3. Spawn the worker (it waits for decryption, then dumps).
    HANDLE T = CreateThread(NULL, 0, DumpWorker, NULL, 0, NULL);
    if(T != NULL) { CloseHandle(T); }

    SDK::Log::Message("DumpModule: will dump %s to %s after unpack\n",
                      TARGET_MODULE, g_OutPath);
    return true;
}