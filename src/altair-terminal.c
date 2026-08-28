
#ifndef UNICODE
#  define UNICODE
#endif
#ifndef _UNICODE
#  define _UNICODE
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <shlobj.h>

#define ALTAIR_VERSION_STR   "1.8.5vB"

#define COL_RESET   "\033[0m"
#define COL_CYAN    "\033[96m"
#define COL_BLUE    "\033[94m"
#define COL_GOLD    "\033[93m"
#define COL_WHITE   "\033[97m"
#define COL_GREEN   "\033[92m"
#define COL_RED     "\033[91m"
#define COL_DIM     "\033[2m"

static const char *BANNER =
COL_CYAN
" \u2588\u2588\u2588\u2588\u2588\u2557 \u2588\u2588\u2557  \u2588\u2588\u2588\u2588\u2588\u2588\u2588\u2557 \u2588\u2588\u2588\u2588\u2588\u2557 \u2588\u2588\u2557\u2588\u2588\u2588\u2588\u2588\u2588\u2557 \n"
" \u2588\u2588\u2554\u2550\u2550\u2588\u2588\u2557\u2588\u2588\u2551  \u2554\u2550\u2550\u2588\u2588\u2554\u2550\u2550\u255d\u2588\u2588\u2554\u2550\u2550\u2588\u2588\u2557\u2588\u2588\u2551\u2588\u2588\u2554\u2550\u2550\u2588\u2588\u2557\n"
" \u2588\u2588\u2588\u2588\u2588\u2588\u2588\u2551\u2588\u2588\u2551     \u2588\u2588\u2551   \u2588\u2588\u2588\u2588\u2588\u2588\u2588\u2551\u2588\u2588\u2551\u2588\u2588\u2588\u2588\u2588\u2588\u2554\u255d\n"
" \u2588\u2588\u2554\u2550\u2550\u2588\u2588\u2551\u2588\u2588\u2551     \u2588\u2588\u2551   \u2588\u2588\u2554\u2550\u2550\u2588\u2588\u2551\u2588\u2588\u2551\u2588\u2588\u2554\u2550\u2550\u2588\u2588\u2557\n"
" \u2588\u2588\u2551  \u2588\u2588\u2551\u2588\u2588\u2588\u2588\u2588\u2588\u2557\u2588\u2588\u2551   \u2588\u2588\u2551  \u2588\u2588\u2551\u2588\u2588\u2551\u2588\u2588\u2551  \u2588\u2588\u2551\n"
" \u255a\u2550\u255d  \u255a\u2550\u255d\u255a\u2550\u2550\u2550\u2550\u2550\u2550\u255d\u255a\u2550\u255d   \u255a\u2550\u255d  \u255a\u2550\u255d\u255a\u2550\u255d\u255a\u2550\u255d  \u255a\u2550\u255d\n"
COL_RESET
"\n"
COL_DIM "\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500" COL_RESET "\n"
"\n"
COL_WHITE "              Altair Programming Language\n"
COL_DIM  "                    Version " ALTAIR_VERSION_STR "\n" COL_RESET
"\n"
COL_DIM "\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500" COL_RESET "\n"
"\n";

static void get_exe_dir(wchar_t *buf, int size) {
    wchar_t tmp[MAX_PATH];
    GetModuleFileNameW(NULL, tmp, MAX_PATH);
    wchar_t *slash = wcsrchr(tmp, L'\\');
    if (slash) *slash = L'\0';
    wcsncpy(buf, tmp, size);
    buf[size-1] = L'\0';
}

static void enable_ansi(void) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleOutputCP(CP_UTF8);
}

static int file_exists_w(const wchar_t *path) {
    DWORD attr = GetFileAttributesW(path);
    return (attr != INVALID_FILE_ATTRIBUTES);
}

static void print_check(const char *label, int ok, const char *detail) {
    if (ok)
        printf(COL_GREEN " \u2713 " COL_WHITE "%-16s" COL_DIM "%s" COL_RESET "\n", label, detail);
    else
        printf(COL_RED " \u2717 " COL_WHITE "%-16s" COL_DIM "%s" COL_RESET "\n", label, detail);
}

static void print_version(void) {
    printf(COL_WHITE "Altair Language\n"
           COL_DIM  "Compiler   " ALTAIR_VERSION_STR "\n"
                     "Runtime    " ALTAIR_VERSION_STR "\n"
                     "Terminal   " ALTAIR_VERSION_STR "\n" COL_RESET);
}

static void print_help(void) {
    printf(COL_GOLD "\nComandos disponibles:\n" COL_RESET);
    printf(COL_WHITE "  help" COL_DIM "                       Muestra esta ayuda\n" COL_RESET);
    printf(COL_WHITE "  version" COL_DIM "                    Muestra la version del compilador\n" COL_RESET);
    printf(COL_WHITE "  doc" COL_DIM "                        Descarga ALTAIR_GUIDE.md en esta carpeta\n" COL_RESET);
    printf(COL_WHITE "  build <nombre.at>" COL_DIM "          Compila nombre.at -> nombre.exe\n" COL_RESET);
    printf(COL_WHITE "  build <nombre.at> -o <salida>" COL_DIM "  Compila con nombre de salida propio\n" COL_RESET);
    printf(COL_WHITE "  build <nombre.at> as <salida.exe> with icon <icono.ico>" COL_DIM "\n"
           "                              Compila con icono personalizado\n" COL_RESET);
    printf(COL_WHITE "  run <nombre.at>" COL_DIM "            Compila y ejecuta en un paso\n" COL_RESET);
    printf(COL_WHITE "  new <proyecto>" COL_DIM "             Crea un proyecto nuevo en Documents\\Altair\n" COL_RESET);
    printf(COL_WHITE "  doctor" COL_DIM "                     Comprueba la instalacion\n" COL_RESET);
    printf(COL_WHITE "  ruta" COL_DIM "                       Muestra tus rutas guardadas y te deja saltar a una\n" COL_RESET);
    printf(COL_WHITE "  ruta add <carpeta>" COL_DIM "         Guarda una carpeta nueva en la tabla de rutas\n" COL_RESET);
    printf(COL_WHITE "  cls" COL_DIM "                        Limpia la pantalla\n" COL_RESET);
    printf(COL_WHITE "  cd / dir / cualquier comando" COL_DIM "  Se reenvia al interprete de Windows\n" COL_RESET);
    printf(COL_WHITE "  exit" COL_DIM "                       Cierra la terminal\n" COL_RESET);
    printf("\n");
}

static void run_doctor(wchar_t *exe_dir) {
    wchar_t path[MAX_PATH];
    char detail[MAX_PATH];

    printf(COL_GOLD "\n Altair Doctor " ALTAIR_VERSION_STR "\n" COL_RESET);
    printf(COL_DIM " ----------------------------------------\n" COL_RESET);

    swprintf(path, MAX_PATH, L"%ls\\runtime\\altair_rt.h", exe_dir);
    WideCharToMultiByte(CP_UTF8,0,path,-1,detail,MAX_PATH,NULL,NULL);
    print_check("Runtime", file_exists_w(path), detail);

    swprintf(path, MAX_PATH, L"%ls\\altairc.exe", exe_dir);
    WideCharToMultiByte(CP_UTF8,0,path,-1,detail,MAX_PATH,NULL,NULL);
    print_check("Compiler", file_exists_w(path), detail);

    swprintf(path, MAX_PATH, L"%ls\\mingw64\\bin\\gcc.exe", exe_dir);
    WideCharToMultiByte(CP_UTF8,0,path,-1,detail,MAX_PATH,NULL,NULL);
    print_check("GCC", file_exists_w(path), detail);

    print_check("Version", 1, ALTAIR_VERSION_STR);
    printf("\n");
}

static void setup_env(wchar_t *exe_dir) {
    wchar_t val[MAX_PATH*2];

    SetEnvironmentVariableW(L"ALTAIR_HOME", exe_dir);

    swprintf(val, MAX_PATH*2, L"%ls\\runtime", exe_dir);
    SetEnvironmentVariableW(L"ALTAIR_RUNTIME", val);

    SetEnvironmentVariableW(L"ALTAIR_VERSION", L"" ALTAIR_VERSION_STR);

    wchar_t cur_path[32768];
    GetEnvironmentVariableW(L"PATH", cur_path, 32768);
    swprintf(val, MAX_PATH*2, L"%ls\\mingw64\\bin", exe_dir);
    wchar_t new_path[32768];
    swprintf(new_path, 32768, L"%ls;%ls;%ls", exe_dir, val, cur_path);
    SetEnvironmentVariableW(L"PATH", new_path);
}

static void ensure_workspace(wchar_t *workspace, int size) {

    wchar_t desktop[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop) != S_OK) {

        SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, desktop);
    }
    swprintf(workspace, size, L"%ls\\Altair", desktop);
    CreateDirectoryW(workspace, NULL);
}

static void get_paths_file(wchar_t *out, int size) {
    wchar_t appdata[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata);
    wchar_t dir[MAX_PATH];
    swprintf(dir, MAX_PATH, L"%ls\\Altair", appdata);
    CreateDirectoryW(dir, NULL);
    swprintf(out, size, L"%ls\\rutas.txt", dir);
}

static void ensure_paths_file_default(wchar_t *workspace) {
    wchar_t pf[MAX_PATH];
    get_paths_file(pf, MAX_PATH);
    if (file_exists_w(pf)) return;

    wchar_t desktop[MAX_PATH], docs[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop);
    SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docs);

    FILE *f = _wfopen(pf, L"w, ccs=UTF-8");
    if (f) {
        fwprintf(f, L"%ls\n", workspace);
        fwprintf(f, L"%ls\\Altair\n", docs);
        fwprintf(f, L"%ls\n", desktop);
        fclose(f);
    }
}

static int load_paths(wchar_t list[][MAX_PATH], int max) {
    wchar_t pf[MAX_PATH];
    get_paths_file(pf, MAX_PATH);
    FILE *f = _wfopen(pf, L"r, ccs=UTF-8");
    if (!f) return 0;
    int n = 0;
    wchar_t line[MAX_PATH];
    while (n < max && fgetws(line, MAX_PATH, f)) {
        size_t len = wcslen(line);
        while (len > 0 && (line[len-1] == L'\n' || line[len-1] == L'\r')) line[--len] = L'\0';
        if (line[0] == L'\0') continue;
        wcsncpy(list[n], line, MAX_PATH - 1);
        list[n][MAX_PATH-1] = L'\0';
        n++;
    }
    fclose(f);
    return n;
}

static void add_path(const wchar_t *path) {
    wchar_t list[32][MAX_PATH];
    int n = load_paths(list, 32);
    for (int i = 0; i < n; i++)
        if (_wcsicmp(list[i], path) == 0) return;

    wchar_t pf[MAX_PATH];
    get_paths_file(pf, MAX_PATH);
    FILE *f = _wfopen(pf, L"a, ccs=UTF-8");
    if (f) { fwprintf(f, L"%ls\n", path); fclose(f); }
}

static void cmd_ruta(wchar_t *workspace, const wchar_t *args) {
    wchar_t trimmed[MAX_PATH*2];
    wcsncpy(trimmed, args, MAX_PATH*2 - 1);
    trimmed[MAX_PATH*2 - 1] = L'\0';
    while (*trimmed == L' ') memmove(trimmed, trimmed+1, wcslen(trimmed)*sizeof(wchar_t));

    if (_wcsnicmp(trimmed, L"add", 3) == 0 && (trimmed[3] == L' ' || trimmed[3] == L'\0')) {
        wchar_t *p = trimmed + 3;
        while (*p == L' ') p++;
        if (*p == L'\0') {
            printf(COL_RED " Uso: ruta add <carpeta>\n" COL_RESET);
            return;
        }
        if (!file_exists_w(p)) {
            printf(COL_RED " Esa carpeta no existe.\n" COL_RESET);
            return;
        }
        add_path(p);
        char p_a[MAX_PATH];
        WideCharToMultiByte(CP_UTF8,0,p,-1,p_a,MAX_PATH,NULL,NULL);
        printf(COL_GREEN " Ruta anadida: " COL_WHITE "%s\n" COL_RESET, p_a);
        return;
    }

    wchar_t list[32][MAX_PATH];
    int n = load_paths(list, 32);

    if (n == 0) {
        printf(COL_RED " No hay rutas guardadas todavia. Usa: ruta add <carpeta>\n" COL_RESET);
        return;
    }

    printf(COL_GOLD "\n Rutas guardadas:\n" COL_RESET);
    printf(COL_DIM " ----------------------------------------\n" COL_RESET);
    for (int i = 0; i < n; i++) {
        char p_a[MAX_PATH];
        WideCharToMultiByte(CP_UTF8,0,list[i],-1,p_a,MAX_PATH,NULL,NULL);
        int is_current = (_wcsicmp(list[i], workspace) == 0);
        printf(COL_WHITE " [%d] " COL_RESET "%s%s\n", i+1, p_a, is_current ? COL_GOLD "  (actual)" COL_RESET : "");
    }
    printf(COL_DIM " ----------------------------------------\n" COL_RESET);
    printf(COL_CYAN " Numero de ruta (ENTER para cancelar): " COL_RESET);
    fflush(stdout);

    wchar_t choice[32];
    if (!fgetws(choice, 32, stdin)) return;
    size_t clen = wcslen(choice);
    while (clen > 0 && (choice[clen-1] == L'\n' || choice[clen-1] == L'\r')) choice[--clen] = L'\0';
    if (choice[0] == L'\0') return;

    int idx = _wtoi(choice);
    if (idx < 1 || idx > n) {
        printf(COL_RED " Numero invalido.\n" COL_RESET);
        return;
    }

    if (!file_exists_w(list[idx-1])) {
        printf(COL_RED " Esa ruta ya no existe en el disco.\n" COL_RESET);
        return;
    }

    wcsncpy(workspace, list[idx-1], MAX_PATH - 1);
    workspace[MAX_PATH-1] = L'\0';
    SetCurrentDirectoryW(workspace);
    char w_a[MAX_PATH];
    WideCharToMultiByte(CP_UTF8,0,workspace,-1,w_a,MAX_PATH,NULL,NULL);
    printf(COL_GREEN " Movido a: " COL_WHITE "%s\n" COL_RESET, w_a);
}

static DWORD run_and_wait(const wchar_t *cmdline, const wchar_t *cwd) {
    wchar_t buf[MAX_PATH*8];
    wcsncpy(buf, cmdline, MAX_PATH*8 - 1);
    buf[MAX_PATH*8 - 1] = L'\0';

    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};

    if (!CreateProcessW(NULL, buf, NULL, NULL, TRUE, 0, NULL, cwd, &si, &pi)) {
        printf(COL_RED " No se pudo ejecutar el comando.\n" COL_RESET);
        return 1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code;
}

static void cmd_build(wchar_t *exe_dir, wchar_t *workspace, const wchar_t *args) {
    wchar_t source[MAX_PATH] = L"";
    wchar_t output[MAX_PATH] = L"";
    wchar_t icon[MAX_PATH]   = L"";
    wchar_t tmp[MAX_PATH*2];
    wcsncpy(tmp, args, MAX_PATH*2 - 1);
    tmp[MAX_PATH*2 - 1] = L'\0';

    wchar_t *saveptr = NULL;
    wchar_t *tok = wcstok(tmp, L" \t", &saveptr);
    while (tok) {
        if (wcscmp(tok, L"-o") == 0) {
            tok = wcstok(NULL, L" \t", &saveptr);
            if (tok) wcsncpy(output, tok, MAX_PATH - 1);
        } else if (wcscmp(tok, L"-icon") == 0) {
            tok = wcstok(NULL, L" \t", &saveptr);
            if (tok) wcsncpy(icon, tok, MAX_PATH - 1);
        } else if (_wcsicmp(tok, L"as") == 0) {
            tok = wcstok(NULL, L" \t", &saveptr);
            if (tok) wcsncpy(output, tok, MAX_PATH - 1);
        } else if (_wcsicmp(tok, L"with") == 0) {

            tok = wcstok(NULL, L" \t", &saveptr);
            if (tok && _wcsicmp(tok, L"icon") == 0) {
                tok = wcstok(NULL, L" \t", &saveptr);
                if (tok) wcsncpy(icon, tok, MAX_PATH - 1);
            }
        } else if (_wcsicmp(tok, L"icon") == 0) {

            tok = wcstok(NULL, L" \t", &saveptr);
            if (tok) wcsncpy(icon, tok, MAX_PATH - 1);
        } else if (source[0] == L'\0') {
            wcsncpy(source, tok, MAX_PATH - 1);
        }
        tok = wcstok(NULL, L" \t", &saveptr);
    }

    if (source[0] == L'\0') {
        printf(COL_RED " Uso:\n" COL_RESET);
        printf(COL_DIM "   build <nombre.at>\n"
                        "   build <nombre.at> -o <salida.exe>\n"
                        "   build <nombre.at> as <salida.exe> with icon <icono.ico>\n" COL_RESET);
        return;
    }

    if (output[0] == L'\0') {

        wcsncpy(output, source, MAX_PATH - 1);
        wchar_t *dot = wcsrchr(output, L'.');
        if (dot && _wcsicmp(dot, L".at") == 0) *dot = L'\0';
        wcsncat(output, L".exe", MAX_PATH - wcslen(output) - 1);
    } else {

        wchar_t *dot = wcsrchr(output, L'.');
        if (!dot || _wcsicmp(dot, L".exe") != 0)
            wcsncat(output, L".exe", MAX_PATH - wcslen(output) - 1);
    }

    if (icon[0] != L'\0') {
        wchar_t icon_path[MAX_PATH];
        swprintf(icon_path, MAX_PATH, L"%ls\\%ls", workspace, icon);
        if (GetFileAttributesW(icon_path) == INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(icon) == INVALID_FILE_ATTRIBUTES) {
            char icon_a[MAX_PATH];
            WideCharToMultiByte(CP_UTF8,0,icon,-1,icon_a,MAX_PATH,NULL,NULL);
            printf(COL_RED " Aviso: no se encuentra el icono \"%s\" en el proyecto. Se compilara sin icono.\n" COL_RESET, icon_a);
            icon[0] = L'\0';
        }
    }

    char src_a[MAX_PATH], out_a[MAX_PATH], icon_a[MAX_PATH]="";
    WideCharToMultiByte(CP_UTF8,0,source,-1,src_a,MAX_PATH,NULL,NULL);
    WideCharToMultiByte(CP_UTF8,0,output,-1,out_a,MAX_PATH,NULL,NULL);
    if (icon[0]) WideCharToMultiByte(CP_UTF8,0,icon,-1,icon_a,MAX_PATH,NULL,NULL);

    printf(COL_DIM " Compilando " COL_WHITE "%s" COL_DIM " -> " COL_WHITE "%s" COL_RESET, src_a, out_a);
    if (icon_a[0]) printf(COL_DIM "  (icono: " COL_WHITE "%s" COL_DIM ")" COL_RESET, icon_a);
    printf("\n");

    wchar_t cmd[MAX_PATH*4];
    if (icon[0] != L'\0')
        swprintf(cmd, MAX_PATH*4, L"\"%ls\\altairc.exe\" \"%ls\" -o \"%ls\" -icon \"%ls\"",
                 exe_dir, source, output, icon);
    else
        swprintf(cmd, MAX_PATH*4, L"\"%ls\\altairc.exe\" \"%ls\" -o \"%ls\"",
                 exe_dir, source, output);

    DWORD ret = run_and_wait(cmd, workspace);
    if (ret == 0) {
        printf(COL_GREEN " Listo: " COL_WHITE "%s" COL_DIM "  (doble clic o \"run\" para ejecutarlo)\n" COL_RESET, out_a);
    } else {
        printf(COL_RED " La compilacion fallo (codigo %lu).\n" COL_RESET, (unsigned long)ret);
    }
}

static void cmd_doc(wchar_t *exe_dir, wchar_t *workspace) {
    wchar_t cmd[MAX_PATH*2];
    swprintf(cmd, MAX_PATH*2, L"\"%ls\\altairc.exe\" guide", exe_dir);
    DWORD ret = run_and_wait(cmd, workspace);
    if (ret == 0) {
        char ws_a[MAX_PATH];
        WideCharToMultiByte(CP_UTF8,0,workspace,-1,ws_a,MAX_PATH,NULL,NULL);
        printf(COL_GREEN " Guia descargada: " COL_WHITE "%s\\ALTAIR_GUIDE.md\n" COL_RESET, ws_a);
    } else {
        printf(COL_RED " No se pudo generar la guia.\n" COL_RESET);
    }
}

static void cmd_run(wchar_t *exe_dir, wchar_t *workspace, const wchar_t *args) {
    wchar_t source[MAX_PATH];
    wcsncpy(source, args, MAX_PATH - 1);

    while (*source == L' ') memmove(source, source+1, (wcslen(source))*sizeof(wchar_t));

    if (source[0] == L'\0') {
        printf(COL_RED " Uso: run <nombre.at>\n" COL_RESET);
        return;
    }

    wchar_t output[MAX_PATH];
    wcsncpy(output, source, MAX_PATH - 1);
    wchar_t *dot = wcsrchr(output, L'.');
    if (dot && _wcsicmp(dot, L".at") == 0) *dot = L'\0';
    wcsncat(output, L".exe", MAX_PATH - wcslen(output) - 1);

    wchar_t buildcmd[MAX_PATH*4];
    swprintf(buildcmd, MAX_PATH*4, L"\"%ls\\altairc.exe\" \"%ls\" -o \"%ls\"",
             exe_dir, source, output);
    if (run_and_wait(buildcmd, workspace) != 0) return;

    wchar_t runcmd[MAX_PATH*2];
    swprintf(runcmd, MAX_PATH*2, L"\"%ls\\%ls\"", workspace, output);
    printf(COL_DIM "\n \u2500\u2500\u2500 ejecutando %ls \u2500\u2500\u2500\n" COL_RESET, output);
    run_and_wait(runcmd, workspace);
}

static void cmd_new(const wchar_t *args) {
    wchar_t name[MAX_PATH];
    wcsncpy(name, args, MAX_PATH - 1);
    while (*name == L' ') memmove(name, name+1, wcslen(name)*sizeof(wchar_t));
    if (name[0] == L'\0') {
        printf(COL_RED " Uso: new <nombre_proyecto>\n" COL_RESET);
        return;
    }

    wchar_t docs[MAX_PATH], proj[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docs);
    swprintf(proj, MAX_PATH, L"%ls\\Altair\\%ls", docs, name);
    CreateDirectoryW(proj, NULL);
    wchar_t main_at[MAX_PATH];
    swprintf(main_at, MAX_PATH, L"%ls\\main.at", proj);
    FILE *f = _wfopen(main_at, L"w");
    if (f) {
        fprintf(f,
            "altair.doc;\n"
            "    name = \"%ls\"\n"
            "    version = \"1.0\"\n"
            "    author = \"Tu Nombre\"\n"
            "create altair.doc\n\n"
            "log \"Hola desde %ls!\"\n", name, name);
        fclose(f);
    }
    char proj_a[MAX_PATH];
    WideCharToMultiByte(CP_UTF8,0,proj,-1,proj_a,MAX_PATH,NULL,NULL);
    printf(COL_GREEN " Proyecto creado: " COL_WHITE "%s\n" COL_RESET, proj_a);
}

int wmain(int argc, wchar_t **argv) {

    enable_ansi();
    SetConsoleTitleW(L"Altair Terminal " L"" ALTAIR_VERSION_STR);

    wchar_t exe_dir[MAX_PATH];
    get_exe_dir(exe_dir, MAX_PATH);
    setup_env(exe_dir);

    if (argc >= 2) {
        if (wcscmp(argv[1], L"--version") == 0 || wcscmp(argv[1], L"version") == 0) {
            print_version();
            return 0;
        }
        if (wcscmp(argv[1], L"--help") == 0 || wcscmp(argv[1], L"help") == 0) {
            print_help();
            return 0;
        }
        if (wcscmp(argv[1], L"--doctor") == 0 || wcscmp(argv[1], L"doctor") == 0) {
            run_doctor(exe_dir);
            return 0;
        }
        if (wcscmp(argv[1], L"doc") == 0) {
            wchar_t workspace[MAX_PATH];
            ensure_workspace(workspace, MAX_PATH);
            cmd_doc(exe_dir, workspace);
            return 0;
        }
        if (wcscmp(argv[1], L"ruta") == 0 || wcscmp(argv[1], L"rute") == 0) {
            wchar_t workspace[MAX_PATH];
            ensure_workspace(workspace, MAX_PATH);
            ensure_paths_file_default(workspace);
            wchar_t rest[MAX_PATH*4] = L"";
            for (int i = 2; i < argc; i++) {
                wcsncat(rest, argv[i], MAX_PATH*4 - wcslen(rest) - 2);
                wcsncat(rest, L" ", MAX_PATH*4 - wcslen(rest) - 1);
            }
            cmd_ruta(workspace, rest);
            return 0;
        }
        if (wcscmp(argv[1], L"--repair") == 0) {
            printf(COL_GOLD " Reparando PATH...\n" COL_RESET);
            setup_env(exe_dir);
            printf(COL_GREEN " OK\n" COL_RESET);
            return 0;
        }
        if (wcscmp(argv[1], L"build") == 0 && argc >= 3) {
            wchar_t workspace[MAX_PATH];
            ensure_workspace(workspace, MAX_PATH);
            wchar_t rest[MAX_PATH*4] = L"";
            for (int i = 2; i < argc; i++) {
                wcsncat(rest, argv[i], MAX_PATH*4 - wcslen(rest) - 2);
                wcsncat(rest, L" ", MAX_PATH*4 - wcslen(rest) - 1);
            }
            cmd_build(exe_dir, workspace, rest);
            return 0;
        }

        if (wcscmp(argv[1], L"compile") == 0 && argc >= 3) {
            wchar_t cc[MAX_PATH*2];
            swprintf(cc, MAX_PATH*2, L"\"%ls\\altairc.exe\" \"%ls\"", exe_dir, argv[2]);
            _wsystem(cc);
            return 0;
        }
        if (wcscmp(argv[1], L"run") == 0 && argc >= 3) {
            wchar_t workspace[MAX_PATH];
            ensure_workspace(workspace, MAX_PATH);
            cmd_run(exe_dir, workspace, argv[2]);
            return 0;
        }
        if (wcscmp(argv[1], L"new") == 0 && argc >= 3) {
            cmd_new(argv[2]);
            return 0;
        }
    }

    printf("%s", BANNER);

    wchar_t path[MAX_PATH];
    int rt_ok, cc_ok, gc_ok;

    swprintf(path, MAX_PATH, L"%ls\\runtime\\altair_rt.h", exe_dir);
    rt_ok = file_exists_w(path);
    swprintf(path, MAX_PATH, L"%ls\\altairc.exe", exe_dir);
    cc_ok = file_exists_w(path);
    swprintf(path, MAX_PATH, L"%ls\\mingw64\\bin\\gcc.exe", exe_dir);
    gc_ok = file_exists_w(path);

    printf(COL_DIM " Core : AltairC  Runtime : Loaded  Backend : C17\n" COL_RESET);
    print_check("Runtime",   rt_ok, rt_ok ? "OK" : "NO ENCONTRADO - reinstala Altair");
    print_check("Compiler",  cc_ok, cc_ok ? "OK" : "NO ENCONTRADO");
    print_check("GCC",       gc_ok, gc_ok ? "OK" : "NO ENCONTRADO");
    printf(COL_DIM "\nType \"help\" to list available commands. Type \"version\" to show compiler\ninformation. Type \"exit\" to close the terminal.\n" COL_RESET);
    printf(COL_DIM " \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\n" COL_RESET);

    if (!rt_ok || !cc_ok || !gc_ok) {
        printf(COL_RED "\n ERROR: Instalacion incompleta. Por favor reinstala Altair.\n" COL_RESET);
        printf("\nPresiona ENTER para salir...");
        getchar();
        return 1;
    }

    wchar_t workspace[MAX_PATH];
    ensure_workspace(workspace, MAX_PATH);
    ensure_paths_file_default(workspace);
    char workspace_a[MAX_PATH];
    WideCharToMultiByte(CP_UTF8,0,workspace,-1,workspace_a,MAX_PATH,NULL,NULL);
    printf(COL_DIM " Directorio: " COL_WHITE "%s\n" COL_RESET, workspace_a);
    printf(COL_DIM " Escribe \"ruta\" para ver y cambiar de carpeta rapidamente.\n\n" COL_RESET);

    SetCurrentDirectoryW(workspace);

    wchar_t line[4096];
    for (;;) {
        printf(COL_CYAN "\nAltair > " COL_RESET);
        fflush(stdout);
        if (!fgetws(line, 4096, stdin)) break;

        size_t len = wcslen(line);
        while (len > 0 && (line[len-1] == L'\n' || line[len-1] == L'\r')) line[--len] = L'\0';

        wchar_t *cmd = line;
        while (*cmd == L' ' || *cmd == L'\t') cmd++;
        if (cmd[0] == L'\0') continue;

        wchar_t first[64] = L"";
        wchar_t *sp = wcschr(cmd, L' ');
        wchar_t *rest = L"";
        if (sp) {
            size_t flen = (size_t)(sp - cmd);
            if (flen >= 64) flen = 63;
            wcsncpy(first, cmd, flen);
            first[flen] = L'\0';
            rest = sp + 1;
            while (*rest == L' ') rest++;
        } else {
            wcsncpy(first, cmd, 63);
        }

        if (wcscmp(first, L"exit") == 0 || wcscmp(first, L"quit") == 0) {
            printf(COL_DIM "Adios.\n" COL_RESET);
            break;
        }
        if (wcscmp(first, L"help") == 0) { print_help(); continue; }
        if (wcscmp(first, L"version") == 0) { print_version(); continue; }
        if (wcscmp(first, L"doctor") == 0) { run_doctor(exe_dir); continue; }
        if (wcscmp(first, L"doc") == 0) { cmd_doc(exe_dir, workspace); continue; }
        if (wcscmp(first, L"build") == 0) { cmd_build(exe_dir, workspace, rest); continue; }
        if (wcscmp(first, L"compile") == 0) { cmd_build(exe_dir, workspace, rest); continue; }
        if (wcscmp(first, L"run") == 0) { cmd_run(exe_dir, workspace, rest); continue; }
        if (wcscmp(first, L"new") == 0) { cmd_new(rest); continue; }
        if (wcscmp(first, L"cls") == 0) { system("cls"); continue; }
        if (wcscmp(first, L"ruta") == 0 || wcscmp(first, L"rute") == 0) { cmd_ruta(workspace, rest); continue; }

        wchar_t sys_cmd[MAX_PATH*8];
        swprintf(sys_cmd, MAX_PATH*8, L"cmd.exe /c %ls", cmd);
        run_and_wait(sys_cmd, workspace);
    }

    return 0;
}
