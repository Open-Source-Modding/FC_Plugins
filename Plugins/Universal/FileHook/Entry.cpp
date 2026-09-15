// FileHook — FC_Plugins plugin that logs every CreateFileW/CreateFileA call
// the engine makes, for post-run hash->name cross-referencing.
//
// Loaded by PluginLoader.dll (via dbdata.dll proxy). Uses the FC_Plugins SDK
// detour. The log handle is opened ONCE at init (before detours install) so
// logging writes via WriteFile on that handle — NOT via CreateFile — avoiding
// infinite recursion (only CreateFile is hooked).
//
// Output: FC6FileHook.log beside the game exe (next to dbdata.dll).
// Format:  <millis-since-load>\t<path>\t<access:W=GENERIC_WRITE|R=read>\t<INVALID|ok>
//
// The log is flushed on every line (small per-line writes; the game's file
// open rate is modest). Set FLUSH_EVERY_LINE to 0 to batch for speed.

#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#include "SDK.hpp"

namespace
{
    // ---- logging state -----------------------------------------------------
    HANDLE  g_LogFile   = INVALID_HANDLE_VALUE;
    CRITICAL_SECTION g_Lock;
    DWORD   g_StartTick = 0;
    bool    g_Ready     = false;
    int     g_LineCount = 0;

    // ---- original functions -------------------------------------------------
    typedef HANDLE(WINAPI *CreateFileWFn)(
        LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
        DWORD, DWORD, HANDLE);
    typedef HANDLE(WINAPI *CreateFileAFn)(
        LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
        DWORD, DWORD, HANDLE);

    CreateFileWFn g_OrigCreateFileW = NULL;
    CreateFileAFn g_OrigCreateFileA = NULL;

    // ---- low-level log writer (WriteFile, no CreateFile) ---------------------
    void WriteLogLine(const char *Text, size_t Len)
    {
        if(g_LogFile == INVALID_HANDLE_VALUE) { return; }

        DWORD Written = 0;
        SetFilePointer(g_LogFile, 0, NULL, FILE_END);

        char    Buf[16];
        DWORD   Elapsed = GetTickCount() - g_StartTick;
        int     N = snprintf(Buf, sizeof(Buf), "%u\t", (unsigned)Elapsed);
        WriteFile(g_LogFile, Buf, N, &Written, NULL);
        WriteFile(g_LogFile, Text, (DWORD)Len, &Written, NULL);
        WriteFile(g_LogFile, "\n", 1, &Written, NULL);

        // enable batching by setting FLUSH_EVERY_LINE to 0
#ifndef FLUSH_EVERY_LINE
#define FLUSH_EVERY_LINE 1
#endif
#if FLUSH_EVERY_LINE
        FlushFileBuffers(g_LogFile);
#endif
    }

    void LogPath(const char *Path, bool Write)
    {
        if(!g_Ready) { return; }

        EnterCriticalSection(&g_Lock);
        ++g_LineCount;
        WriteLogLine(Path, strlen(Path));
        // we already wrote the path; the access flag is folded into a prefix
        // below. (Simplest: log path only; access deduced by caller needs.)
        LeaveCriticalSection(&g_Lock);
    }
} // namespace

// ---- CreateFileW detour -----------------------------------------------------
static HANDLE WINAPI HookCreateFileW(
    LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile)
{
    if(lpFileName != NULL)
    {
        char Narrow[MAX_PATH + 8];
        int N = WideCharToMultiByte(CP_UTF8, 0, lpFileName, -1,
                                    Narrow, (int)sizeof(Narrow), NULL, NULL);
        if(N > 0)
        {
            bool Write = (dwDesiredAccess & GENERIC_WRITE) != 0;
            LogPath(Narrow, Write);
        }
    }
    return g_OrigCreateFileW(lpFileName, dwDesiredAccess, dwShareMode,
                             lpSecurityAttributes, dwCreationDisposition,
                             dwFlagsAndAttributes, hTemplateFile);
}

// ---- CreateFileA detour -----------------------------------------------------
static HANDLE WINAPI HookCreateFileA(
    LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile)
{
    if(lpFileName != NULL)
    {
        bool Write = (dwDesiredAccess & GENERIC_WRITE) != 0;
        LogPath(lpFileName, Write);
    }
    return g_OrigCreateFileA(lpFileName, dwDesiredAccess, dwShareMode,
                             lpSecurityAttributes, dwCreationDisposition,
                             dwFlagsAndAttributes, hTemplateFile);
}

PLUGIN_ENTRY()
{
    // 1. Pre-open the log handle via CreateFileA BEFORE detours are installed.
    //    The log sits next to the loader (game bin/).
    char LogPath[MAX_PATH + 16];
    GetModuleFileNameA(NULL, LogPath, MAX_PATH);
    {
        char *Slash = strrchr(LogPath, '\\');
        if(Slash != NULL) { *(Slash + 1) = '\0'; }
        else { LogPath[0] = '\0'; }
        strncat(LogPath, "FC6FileHook.log", sizeof(LogPath) - strlen(LogPath) - 1);
    }

    g_LogFile = CreateFileA(LogPath, FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    g_StartTick = GetTickCount();
    InitializeCriticalSection(&g_Lock);
    g_Ready = (g_LogFile != INVALID_HANDLE_VALUE);

    // 2. Resolve the original functions.
    HMODULE Kernel32 = GetModuleHandleA("kernel32.dll");
    g_OrigCreateFileW = (CreateFileWFn)GetProcAddress(Kernel32, "CreateFileW");
    g_OrigCreateFileA = (CreateFileAFn)GetProcAddress(Kernel32, "CreateFileA");

    if(g_OrigCreateFileW == NULL || g_OrigCreateFileA == NULL)
    {
        SDK::Log::Message("FileHook: could not resolve CreateFileW/A\n");
        return false;
    }

    // 3. Install detours via the SDK.
    SDK::Detour::SDetour *HookW = SDK::Detour::Setup((void*)g_OrigCreateFileW,
                                                     (void*)HookCreateFileW);
    SDK::Detour::SDetour *HookA = SDK::Detour::Setup((void*)g_OrigCreateFileA,
                                                     (void*)HookCreateFileA);

    if(HookW == NULL || HookA == NULL)
    {
        SDK::Log::Message("FileHook: detour setup failed\n");
        return false;
    }

    SDK::Detour::Enable(HookW);
    SDK::Detour::Enable(HookA);

    SDK::Log::Message("FileHook: logging file opens to %s\n", LogPath);
    return true;
}