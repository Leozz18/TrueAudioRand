#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include "trueaudiorand.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define IDC_COMBO_MIC    101
#define IDC_EDIT_COUNT   102
#define IDC_BTN_REFRESH  103
#define IDC_BTN_DEMO     104
#define IDC_BTN_GENERATE 105
#define IDC_BTN_ANALYZE  106
#define IDC_PROGRESS     107
#define IDC_LOG          108
#define IDC_BTN_HIST     109
#define IDC_BTN_SCATTER  110

#define WM_APP_LOG       (WM_APP + 1)
#define WM_APP_PROGRESS  (WM_APP + 2)
#define WM_APP_DONE      (WM_APP + 3)
#define WM_APP_ERROR     (WM_APP + 4)

typedef enum {
    TASK_NONE = 0,
    TASK_DEMO,
    TASK_GENERATE,
    TASK_ANALYZE
} TaskKind;

typedef struct {
    TaskKind kind;
    int      count;
    int      pa_index;
    HWND     hwnd;
} WorkerParams;

static HWND g_hwnd_main;
static HWND g_combo_mic;
static HWND g_edit_count;
static HWND g_progress;
static HWND g_log;
static HANDLE g_worker_thread = NULL;
static volatile LONG g_busy = 0;
static wchar_t g_workdir[MAX_PATH];

static BOOL CALLBACK gui_enum_font_cb(HWND child, LPARAM lparam);

static void utf8_to_wide(const char *src, wchar_t *dst, size_t dst_chars)
{
    if (!src || !dst || dst_chars == 0) {
        return;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, (int)dst_chars) > 0) {
        return;
    }

    MultiByteToWideChar(CP_ACP, 0, src, -1, dst, (int)dst_chars);
}

static void gui_log_line(const wchar_t *text)
{
    if (!g_log || !text) {
        return;
    }

    int idx = (int)SendMessageW(g_log, LB_ADDSTRING, 0, (LPARAM)text);
    if (idx != LB_ERR) {
        SendMessageW(g_log, LB_SETCURSEL, (WPARAM)idx, 0);
    }
}

static void gui_log_path_line(const wchar_t *prefix, const wchar_t *path)
{
    wchar_t line[1024];
    size_t pos = 0;
    size_t i;

    if (!path) {
        gui_log_line(prefix);
        return;
    }

    if (prefix) {
        while (prefix[pos] && pos < 900) {
            line[pos] = prefix[pos];
            pos++;
        }
    }

    for (i = 0; path[i] && pos + 1 < 1023; ++i) {
        line[pos++] = (path[i] == L'\\') ? L'/' : path[i];
    }
    line[pos] = L'\0';
    gui_log_line(line);
}

static void gui_log_utf8(const char *text)
{
    wchar_t buf[512];
    utf8_to_wide(text, buf, 512);
    gui_log_line(buf);
}

static void gui_set_busy(int busy)
{
    EnableWindow(g_combo_mic, !busy);
    EnableWindow(GetDlgItem(g_hwnd_main, IDC_BTN_REFRESH), !busy);
    EnableWindow(GetDlgItem(g_hwnd_main, IDC_BTN_DEMO), !busy);
    EnableWindow(GetDlgItem(g_hwnd_main, IDC_BTN_GENERATE), !busy);
    EnableWindow(GetDlgItem(g_hwnd_main, IDC_BTN_ANALYZE), !busy);
    EnableWindow(g_edit_count, !busy);
}

static void gui_set_progress(int current, int total)
{
    SendMessageW(g_progress, PBM_SETRANGE32, 0, total > 0 ? total : 1);
    SendMessageW(g_progress, PBM_SETPOS, current, 0);
}

static int gui_get_selected_pa_index(void)
{
    int sel = (int)SendMessageW(g_combo_mic, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) {
        return -1;
    }
    return (int)SendMessageW(g_combo_mic, CB_GETITEMDATA, (WPARAM)sel, 0);
}

static int gui_get_sample_count(void)
{
    wchar_t buf[32];
    GetWindowTextW(g_edit_count, buf, 32);
    int count = _wtoi(buf);
    return count > 0 ? count : 1000;
}

static void gui_populate_devices(void)
{
    SendMessageW(g_combo_mic, CB_RESETCONTENT, 0, 0);

    int count = tar_input_device_count();
    if (count < 0) {
        gui_log_utf8(tar_strerror());
        return;
    }

    int default_sel = 0;
    for (int i = 0; i < count; ++i) {
        wchar_t label[512];
        int pa_index = -1;

        if (tar_get_input_device_label_w(i, label, 512, &pa_index) != 0) {
            continue;
        }

        int idx = (int)SendMessageW(g_combo_mic, CB_ADDSTRING, 0, (LPARAM)label);
        SendMessageW(g_combo_mic, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)pa_index);

        if (wcsstr(label, L"*predefinito*")) {
            default_sel = idx;
        }
    }

    if (count > 0) {
        SendMessageW(g_combo_mic, CB_SETCURSEL, (WPARAM)default_sel, 0);
        SendMessageW(g_combo_mic, CB_SETDROPPEDWIDTH, 700, 0);
    }

    wchar_t msg[128];
    swprintf(msg, 128, L"Trovati %d dispositivi di input.", count);
    gui_log_line(msg);
}

static int gui_init_selected_device(int pa_index)
{
    tar_close();
    int rc = tar_init_device(pa_index);
    if (rc != 0) {
        gui_log_utf8(tar_strerror());
    }
    return rc;
}

static void gui_log_hex(const wchar_t *prefix, const uint8_t *data, size_t len)
{
    wchar_t line[512];
    size_t pos = 0;

    if (prefix) {
        wcscpy(line, prefix);
        pos = wcslen(line);
    }

    for (size_t i = 0; i < len && pos + 4 < 510; ++i) {
        if (i > 0) {
            line[pos++] = L' ';
        }
        swprintf(line + pos, 512 - pos, L"%02x", data[i]);
        pos += 2;
    }
    line[pos] = L'\0';
    gui_log_line(line);
}

static void gui_init_workdir(void)
{
    wchar_t marker[MAX_PATH];

    /* run_gui.bat imposta gia la cartella del progetto con /D */
    if (GetCurrentDirectoryW(MAX_PATH, g_workdir) == 0) {
        g_workdir[0] = L'\0';
    }

    swprintf(marker, MAX_PATH, L"%s\\verify_rand.py", g_workdir);
    if (GetFileAttributesW(marker) != INVALID_FILE_ATTRIBUTES) {
        return;
    }

    {
        wchar_t exe_path[MAX_PATH];
        DWORD n = GetModuleFileNameW(NULL, exe_path, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            for (DWORD i = n; i > 0; --i) {
                if (exe_path[i - 1] == L'\\' || exe_path[i - 1] == L'/') {
                    exe_path[i - 1] = L'\0';
                    break;
                }
            }
            wcscpy(g_workdir, exe_path);
            SetCurrentDirectoryW(g_workdir);
        }
    }

    GetCurrentDirectoryW(MAX_PATH, g_workdir);
}

static void gui_show_pipeline_report(const TarPipelineReport *r)
{
    wchar_t line[512];

    gui_log_line(L"");
    gui_log_line(L"--- PIPELINE TrueAudioRand ---");

    utf8_to_wide(r->device_name, line, 512);
    gui_log_line(L"[1] Microfono");
    gui_log_line(line);

    swprintf(line, 512, L"    Formato: %d Hz, %d canale/i, PCM 16-bit",
             r->sample_rate, r->channels);
    gui_log_line(line);

    swprintf(line, 512, L"[2] Cattura audio (%zu campioni PCM)", r->frames_captured);
    gui_log_line(line);

    swprintf(line, 512,
             L"    Anteprima: %d, %d, %d, %d, %d, %d, %d, %d",
             (int)r->raw_samples[0], (int)r->raw_samples[1],
             (int)r->raw_samples[2], (int)r->raw_samples[3],
             (int)r->raw_samples[4], (int)r->raw_samples[5],
             (int)r->raw_samples[6], (int)r->raw_samples[7]);
    gui_log_line(line);

    swprintf(line, 512, L"[3] Bit estratti: %zu byte", r->raw_bit_bytes);
    gui_log_line(line);
    gui_log_hex(L"    Anteprima:", r->raw_bits_preview, TAR_DEMO_PREVIEW_BYTES);

    swprintf(line, 512, L"[4] Von Neumann: %zu byte sbiancati", r->debiased_bytes);
    gui_log_line(line);
    gui_log_hex(L"    Anteprima:", r->debiased_preview, TAR_DEMO_PREVIEW_BYTES);

    gui_log_line(L"[5] SHA-256");
    gui_log_hex(L"    Hash:", r->sha256_hash, 32);

    swprintf(line, 512, L"[6] Risultato: %u  (hex %08x)",
             (unsigned)r->rand32, (unsigned)r->rand32);
    gui_log_line(line);
    gui_log_line(L"------------------------------");
}

static FILE *gui_open_output_file(const wchar_t *filename)
{
    (void)filename;
    return fopen("random_samples.txt", "w");
}

static int gui_file_exists(const wchar_t *filename)
{
    return GetFileAttributesW(filename) != INVALID_FILE_ATTRIBUTES;
}

static int gui_run_python_analyze(void)
{
    wchar_t cmd[MAX_PATH * 3];

    if (!gui_file_exists(L"random_samples.txt")) {
        gui_log_line(L"random_samples.txt non trovato nella cartella di lavoro.");
        gui_log_path_line(L"Cartella attuale: ", g_workdir);
        return 1;
    }

    swprintf(cmd, MAX_PATH * 3, L"python.exe verify_rand.py random_samples.txt");

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, g_workdir, &si, &pi)) {
        wcscpy(cmd, L"py -3 verify_rand.py random_samples.txt");
        if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, g_workdir, &si, &pi)) {
            gui_log_line(L"Errore avvio Python. Installa Python e aggiungilo al PATH.");
            return 1;
        }
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exit_code != 0) {
        wchar_t msg[128];
        swprintf(msg, 128, L"Python terminato con codice %lu", exit_code);
        gui_log_line(msg);
    }

    return (int)exit_code;
}

static DWORD WINAPI gui_worker_thread(LPVOID param)
{
    WorkerParams *wp = (WorkerParams *)param;
    HWND hwnd = wp->hwnd;

    if (wp->kind == TASK_DEMO || wp->kind == TASK_GENERATE) {
        if (gui_init_selected_device(wp->pa_index) != 0) {
            gui_log_utf8(tar_strerror());
            PostMessageW(hwnd, WM_APP_ERROR, 0, 0);
            free(wp);
            return 1;
        }
    }

    if (wp->kind == TASK_DEMO) {
        TarPipelineReport report;
        if (tar_demo_once(&report) != 0) {
            gui_log_utf8(tar_strerror());
            PostMessageW(hwnd, WM_APP_ERROR, 0, 0);
        } else {
            TarPipelineReport *heap_report = (TarPipelineReport *)malloc(sizeof(TarPipelineReport));
            if (heap_report) {
                *heap_report = report;
                PostMessageW(hwnd, WM_APP_DONE, TASK_DEMO, (LPARAM)heap_report);
            }
        }
        tar_close();
    } else if (wp->kind == TASK_GENERATE) {
        GetCurrentDirectoryW(MAX_PATH, g_workdir);
        FILE *out = gui_open_output_file(L"random_samples.txt");
        if (!out) {
            gui_log_line(L"Impossibile creare random_samples.txt");
            PostMessageW(hwnd, WM_APP_ERROR, 0, 0);
            tar_close();
            free(wp);
            return 1;
        }

        for (int i = 0; i < wp->count; ++i) {
            uint32_t value = tar_get_rand32();
            fprintf(out, "%" PRIu32 "\n", value);
            if ((i + 1) % 100 == 0 || i + 1 == wp->count) {
                PostMessageW(hwnd, WM_APP_PROGRESS, (WPARAM)(i + 1), (LPARAM)wp->count);
            }
        }

        fclose(out);
        tar_close();
        PostMessageW(hwnd, WM_APP_DONE, TASK_GENERATE, 0);
    } else if (wp->kind == TASK_ANALYZE) {
        PostMessageW(hwnd, WM_APP_PROGRESS, 0, 100);
        int rc = gui_run_python_analyze();
        PostMessageW(hwnd, WM_APP_PROGRESS, 100, 100);
        PostMessageW(hwnd, WM_APP_DONE, TASK_ANALYZE, (LPARAM)(INT_PTR)rc);
    }

    free(wp);
    return 0;
}

static int gui_start_task(TaskKind kind)
{
    if (InterlockedCompareExchange(&g_busy, 1, 0) != 0) {
        return 0;
    }

    WorkerParams *wp = (WorkerParams *)calloc(1, sizeof(WorkerParams));
    if (!wp) {
        InterlockedExchange(&g_busy, 0);
        return 0;
    }

    wp->kind = kind;
    wp->hwnd = g_hwnd_main;
    wp->pa_index = gui_get_selected_pa_index();
    wp->count = gui_get_sample_count();

    if (kind == TASK_DEMO || kind == TASK_GENERATE) {
        if (wp->pa_index < 0) {
            gui_log_line(L"Seleziona un microfono.");
            free(wp);
            InterlockedExchange(&g_busy, 0);
            return 0;
        }
    }

    gui_set_busy(1);
    gui_set_progress(0, kind == TASK_GENERATE ? wp->count : 100);

    if (kind == TASK_DEMO) {
        gui_log_line(L"Avvio demo pipeline...");
    } else if (kind == TASK_GENERATE) {
        wchar_t msg[128];
        swprintf(msg, 128, L"Generazione di %d campioni...", wp->count);
        gui_log_line(msg);
    } else {
        gui_log_line(L"Analisi statistica con Python...");
    }

    g_worker_thread = CreateThread(NULL, 0, gui_worker_thread, wp, 0, NULL);
    if (!g_worker_thread) {
        gui_log_line(L"Impossibile avviare il thread di lavoro.");
        free(wp);
        gui_set_busy(0);
        InterlockedExchange(&g_busy, 0);
    }

    return 1;
}

static void gui_open_file(const wchar_t *filename)
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%s\\%s", g_workdir, filename);
    ShellExecuteW(NULL, L"open", path, NULL, g_workdir, SW_SHOWNORMAL);
}

static void gui_create_controls(HWND hwnd)
{
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    CreateWindowW(L"STATIC", L"Microfono:",
                  WS_CHILD | WS_VISIBLE, 12, 12, 80, 20, hwnd, NULL, NULL, NULL);

    g_combo_mic = CreateWindowW(
        L"COMBOBOX", NULL,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        96, 10, 530, 320, hwnd, (HMENU)IDC_COMBO_MIC, NULL, NULL);

    CreateWindowW(L"BUTTON", L"Aggiorna",
                  WS_CHILD | WS_VISIBLE, 536, 9, 90, 26,
                  hwnd, (HMENU)IDC_BTN_REFRESH, NULL, NULL);

    CreateWindowW(L"STATIC", L"Campioni:",
                  WS_CHILD | WS_VISIBLE, 12, 48, 80, 20, hwnd, NULL, NULL, NULL);

    g_edit_count = CreateWindowW(
        L"EDIT", L"10000",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
        96, 46, 120, 24, hwnd, (HMENU)IDC_EDIT_COUNT, NULL, NULL);

    CreateWindowW(L"BUTTON", L"Demo pipeline",
                  WS_CHILD | WS_VISIBLE, 12, 84, 120, 30,
                  hwnd, (HMENU)IDC_BTN_DEMO, NULL, NULL);

    CreateWindowW(L"BUTTON", L"Genera campioni",
                  WS_CHILD | WS_VISIBLE, 140, 84, 130, 30,
                  hwnd, (HMENU)IDC_BTN_GENERATE, NULL, NULL);

    CreateWindowW(L"BUTTON", L"Analizza grafici",
                  WS_CHILD | WS_VISIBLE, 278, 84, 130, 30,
                  hwnd, (HMENU)IDC_BTN_ANALYZE, NULL, NULL);

    CreateWindowW(L"BUTTON", L"Istogramma",
                  WS_CHILD | WS_VISIBLE, 416, 84, 100, 30,
                  hwnd, (HMENU)IDC_BTN_HIST, NULL, NULL);

    CreateWindowW(L"BUTTON", L"Scatter",
                  WS_CHILD | WS_VISIBLE, 524, 84, 100, 30,
                  hwnd, (HMENU)IDC_BTN_SCATTER, NULL, NULL);

    g_progress = CreateWindowW(
        PROGRESS_CLASSW, NULL,
        WS_CHILD | WS_VISIBLE,
        12, 126, 714, 22, hwnd, (HMENU)IDC_PROGRESS, NULL, NULL);

    CreateWindowW(L"STATIC", L"Log:",
                  WS_CHILD | WS_VISIBLE, 12, 158, 60, 20, hwnd, NULL, NULL, NULL);

    g_log = CreateWindowW(
        L"LISTBOX", NULL,
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        12, 178, 714, 300, hwnd, (HMENU)IDC_LOG, NULL, NULL);

    EnumChildWindows(hwnd, gui_enum_font_cb, (LPARAM)font);
}

static BOOL CALLBACK gui_enum_font_cb(HWND child, LPARAM lparam)
{
    SendMessageW(child, WM_SETFONT, (WPARAM)lparam, TRUE);
    return TRUE;
}

static LRESULT CALLBACK gui_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        g_hwnd_main = hwnd;
        gui_create_controls(hwnd);
        gui_log_line(L"TrueAudioRand GUI avviata.");
        gui_log_path_line(L"Cartella di lavoro: ", g_workdir);
        gui_log_line(L"Seleziona il microfono e usa Demo, Genera o Analizza.");
        gui_populate_devices();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BTN_REFRESH:
            gui_populate_devices();
            break;
        case IDC_BTN_DEMO:
            gui_start_task(TASK_DEMO);
            break;
        case IDC_BTN_GENERATE:
            gui_start_task(TASK_GENERATE);
            break;
        case IDC_BTN_ANALYZE:
            gui_start_task(TASK_ANALYZE);
            break;
        case IDC_BTN_HIST:
            gui_open_file(L"histogram.png");
            break;
        case IDC_BTN_SCATTER:
            gui_open_file(L"scatter.png");
            break;
        }
        return 0;

    case WM_APP_PROGRESS:
        gui_set_progress((int)wParam, (int)lParam);
        return 0;

    case WM_APP_DONE:
        if (wParam == TASK_DEMO && lParam) {
            TarPipelineReport *report = (TarPipelineReport *)lParam;
            gui_show_pipeline_report(report);
            free(report);
            gui_log_line(L"Demo completata.");
        } else if (wParam == TASK_GENERATE) {
            gui_log_line(L"Generazione completata: random_samples.txt");
        } else if (wParam == TASK_ANALYZE) {
            if ((int)(INT_PTR)lParam == 0) {
                gui_log_line(L"Analisi completata: histogram.png, scatter.png");
            } else {
                gui_log_line(L"Analisi Python fallita.");
            }
        }
        gui_set_busy(0);
        InterlockedExchange(&g_busy, 0);
        if (g_worker_thread) {
            CloseHandle(g_worker_thread);
            g_worker_thread = NULL;
        }
        return 0;

    case WM_APP_ERROR:
        gui_log_line(L"Operazione fallita.");
        gui_set_busy(0);
        InterlockedExchange(&g_busy, 0);
        if (g_worker_thread) {
            CloseHandle(g_worker_thread);
            g_worker_thread = NULL;
        }
        return 0;

    case WM_CLOSE:
        if (InterlockedCompareExchange(&g_busy, 0, 0) != 0) {
            MessageBoxW(hwnd, L"Attendi il completamento dell'operazione in corso.",
                        L"TrueAudioRand", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        tar_close();
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)pCmdLine;

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);

    gui_init_workdir();

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = gui_wnd_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"TrueAudioRandGui";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

    if (!RegisterClassW(&wc)) {
        MessageBoxW(NULL, L"Impossibile registrare la finestra.", L"TrueAudioRand", MB_OK | MB_ICONERROR);
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0, L"TrueAudioRandGui", L"TrueAudioRand - TRNG da microfono",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 760, 540,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) {
        MessageBoxW(NULL, L"Impossibile creare la finestra.", L"TrueAudioRand", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
