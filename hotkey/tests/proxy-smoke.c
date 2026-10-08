/* Proxy smoke test. Runs on a normal desktop, never starts the game.
 *
 * Proves three things about the built d3d11.dll:
 *   1. it loads as a DLL and its DllMain start-up path runs to completion;
 *   2. it still forwards D3D11 to the real System32 d3d11.dll (a NULL driver
 *      device is created through our proxy, exercising proxy_resolve);
 *   3. with no GameAssembly.dll next to it, it refuses to hook anything.
 *
 * Build + run:
 *   zig cc -target x86_64-windows-gnu tests/proxy-smoke.c -o build/proxy-smoke.exe
 *   build/proxy-smoke.exe <path-to-built-d3d11.dll> <scratch-dir>
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

typedef HRESULT(WINAPI *PFN_D3D11CreateDevice)(void *adapter, int driver_type, void *software,
                                               unsigned flags, const void *levels,
                                               unsigned level_count, unsigned sdk_version,
                                               void **device, unsigned *feature_level,
                                               void **context);

static int failures = 0;
static void check(bool cond, const char *what) {
    printf("  %s  %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) ++failures;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("%s\n", "usage: proxy-smoke.exe <d3d11.dll> <scratch-dir>");
        return 2;
    }
    WCHAR dll[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, dll, MAX_PATH);
    WCHAR scratch[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, scratch, MAX_PATH);

    /* Resolve both to absolute paths before changing the current directory:
     * a relative DLL path would otherwise be resolved against the scratch dir.
     * Also create the scratch dir so that "build/scratch" works from scratch. */
    WCHAR resolved[MAX_PATH];
    if (GetFullPathNameW(dll, MAX_PATH, resolved, NULL)) wcscpy(dll, resolved);
    if (GetFullPathNameW(scratch, MAX_PATH, resolved, NULL)) wcscpy(scratch, resolved);
    CreateDirectoryW(scratch, NULL);

    /* Copy the DLL into an isolated directory so the start-up path and the log
     * and config files it writes cannot touch the real game folder. */
    WCHAR cwd[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, cwd);
    SetCurrentDirectoryW(scratch);
    WCHAR base[64];
    wcscpy(base, L"\\probe.dll");
    WCHAR local[MAX_PATH];
    wcscpy(local, scratch);
    wcscat(local, base);
    DeleteFileW(local);
    check(CopyFileW(dll, local, FALSE) != 0, "proxy DLL copied to an isolated directory");

    SetDllDirectoryW(scratch);
    HMODULE h = LoadLibraryExW(local, NULL,
                              LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    check(h != NULL, "proxy DLL loads (DllMain returns TRUE)");
    if (!h) {
        printf("      GetLastError=%lu\n", (unsigned long)GetLastError());
        SetCurrentDirectoryW(cwd);
        return 1;
    }

    PFN_D3D11CreateDevice create = (PFN_D3D11CreateDevice)(void *)GetProcAddress(h, "D3D11CreateDevice");
    check(create != NULL, "D3D11CreateDevice resolves through the proxy");

    FARPROC swapchain = GetProcAddress(h, "D3D11CreateDeviceAndSwapChain");
    check(swapchain != NULL, "D3D11CreateDeviceAndSwapChain resolves through the proxy");

    if (create) {
        void *device = NULL, *context = NULL;
        unsigned level = 0;
        /* D3D_DRIVER_TYPE_NULL == 1, D3D11_SDK_VERSION == 7. Needs no GPU. */
        HRESULT hr = create(NULL, 1, NULL, 0, NULL, 0, 7, &device, &level, &context);
        check(SUCCEEDED(hr), "real System32 d3d11.dll created a NULL-driver device");
        printf("      hr=0x%08lX device=%p context=%p\n", (unsigned long)hr, device, context);
        (void)context; /* left for the process to release at exit */
    }

    /* The start-up thread needs a moment to write its log and refuse to hook. */
    Sleep(1500);
    WCHAR ini[MAX_PATH], log[MAX_PATH];
    wcscpy(ini, scratch);
    wcscat(ini, L"\\hslr-wait-hotkey.ini");
    wcscpy(log, scratch);
    wcscat(log, L"\\hslr-wait-hotkey.log");
    check(GetFileAttributesW(ini) != INVALID_FILE_ATTRIBUTES, "default configuration was created");
    check(GetFileAttributesW(log) != INVALID_FILE_ATTRIBUTES, "log was created");

    /* The Mod keeps the log open for writing, so the reader must also allow
     * concurrent writes (FILE_SHARE_WRITE) or Windows reports a sharing
     * violation rather than letting us read it. */
    HANDLE lf = CreateFileW(log, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_EXISTING, 0, NULL);
    if (lf != INVALID_HANDLE_VALUE) {
        char text[4096];
        DWORD got = 0;
        ReadFile(lf, text, sizeof(text) - 1, &got, NULL);
        text[got] = '\0';
        CloseHandle(lf);
        /* Check before printing: the print loop splits the buffer in place. */
        check(strstr(text, "Wait Hotkey") != NULL, "log records the Mod banner");
        check(strstr(text, "Unsupported GameAssembly") != NULL,
              "refuses to hook when no supported GameAssembly.dll is present");
        check(strstr(text, "nothing is hooked") != NULL, "states explicitly that nothing was hooked");
        check(strstr(text, "binding: space -> wait") != NULL, "log records the parsed binding");
        printf("      --- log ---\n");
        for (char *line = text; line && *line;) {
            char *nl = strpbrk(line, "\r\n");
            if (nl) { *nl = '\0'; printf("      %s\n", line); line = nl + 1; while (*line == '\n' || *line == '\r') ++line; }
            else { printf("      %s\n", line); break; }
        }
    } else {
        check(false, "log is readable");
    }

    DeleteFileW(local);
    DeleteFileW(ini);
    DeleteFileW(log);
    SetCurrentDirectoryW(cwd);
    printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
