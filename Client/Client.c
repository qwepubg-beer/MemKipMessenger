#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <stdbool.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>

#define PIPE_NAME L"\\\\.\\pipe\\ChatPipe"
#define BUFFER_SIZE 1024  // количество широких символов

HANDLE hPipe;
CRITICAL_SECTION csConsole;
volatile BOOL connected = TRUE;

// Поток приёма сообщений
unsigned __stdcall ReceiveThread(void* param) {
    wchar_t buffer[BUFFER_SIZE];
    DWORD bytesRead;

    while (connected) {
        memset(buffer, 0, sizeof(buffer));

        if (!ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL)) {
            DWORD error = GetLastError();
            if (connected && error != ERROR_BROKEN_PIPE) {
                EnterCriticalSection(&csConsole);
                wprintf(L"\n[Система]: Ошибка чтения: %d\n", error);
                wprintf(L"[Система]: Соединение с сервером потеряно\n");
                LeaveCriticalSection(&csConsole);
            }
            connected = FALSE;
            break;
        }

        if (bytesRead > 0) {
            buffer[bytesRead / sizeof(wchar_t)] = L'\0'; // гарантируем завершение
            EnterCriticalSection(&csConsole);
            wprintf(L"\n%s\n", buffer);
            wprintf(L"Вы: ");
            fflush(stdout);
            LeaveCriticalSection(&csConsole);
        }
    }
    return 0;
}

int main() {
    // Настройка консоли на Unicode (UTF-16)
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stdin), _O_U16TEXT);
    setlocale(LC_ALL, ".UTF8");

    wchar_t buffer[BUFFER_SIZE];
    wchar_t userName[50];

    InitializeCriticalSection(&csConsole);

    wprintf(L"=====================================\n");
    wprintf(L"       Чат-клиент (Unicode)\n");
    wprintf(L"=====================================\n\n");

    wprintf(L"Введите ваше имя: ");
    fgetws(userName, 50, stdin);
    userName[wcscspn(userName, L"\r\n")] = L'\0';

    if (wcslen(userName) == 0) {
        wcscpy(userName, L"Аноним");
    }

    wprintf(L"\nПодключение к серверу...\n");

    if (!WaitNamedPipeW(PIPE_NAME, 5000)) {
        wprintf(L"Ошибка: Сервер не запущен или не отвечает\n");
        wprintf(L"Убедитесь, что сервер запущен и повторите попытку\n");
        DeleteCriticalSection(&csConsole);
        return 1;
    }

    hPipe = CreateFileW(
        PIPE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hPipe == INVALID_HANDLE_VALUE) {
        wprintf(L"Ошибка подключения к серверу. Код: %d\n", GetLastError());
        DeleteCriticalSection(&csConsole);
        return 1;
    }

    DWORD pipeMode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(hPipe, &pipeMode, NULL, NULL)) {
        wprintf(L"Ошибка установки режима канала\n");
        CloseHandle(hPipe);
        DeleteCriticalSection(&csConsole);
        return 1;
    }

    // Отправляем имя серверу
    DWORD bytesWritten;
    if (!WriteFile(hPipe, userName, (wcslen(userName) + 1) * sizeof(wchar_t), &bytesWritten, NULL)) {
        wprintf(L"Ошибка отправки имени\n");
        CloseHandle(hPipe);
        DeleteCriticalSection(&csConsole);
        return 1;
    }

    HANDLE hReadThread = (HANDLE)_beginthreadex(NULL, 0, ReceiveThread, NULL, 0, NULL);
    if (hReadThread == NULL) {
        wprintf(L"Ошибка создания потока приёма\n");
        CloseHandle(hPipe);
        DeleteCriticalSection(&csConsole);
        return 1;
    }
    CloseHandle(hReadThread); // поток работает сам

    wprintf(L"\n=====================================\n");
    wprintf(L"Добро пожаловать в чат, %s!\n", userName);
    wprintf(L"=====================================\n");
    wprintf(L"Введите /quit для выхода\n");
    wprintf(L"=====================================\n\n");

    while (connected) {
        wprintf(L"Вы: ");
        fflush(stdout);

        if (fgetws(buffer, BUFFER_SIZE, stdin) == NULL) {
            break;
        }

        buffer[wcscspn(buffer, L"\r\n")] = L'\0';

        if (wcslen(buffer) == 0) {
            continue;
        }

        if (wcscmp(buffer, L"/quit") == 0) {
            wprintf(L"Выход из чата...\n");
            WriteFile(hPipe, buffer, (wcslen(buffer) + 1) * sizeof(wchar_t), &bytesWritten, NULL);
            connected = FALSE;
            break;
        }

        if (!WriteFile(hPipe, buffer, (wcslen(buffer) + 1) * sizeof(wchar_t), &bytesWritten, NULL)) {
            wprintf(L"\n[Ошибка]: Не удалось отправить сообщение\n");
            connected = FALSE;
            break;
        }
    }

    connected = FALSE;
    CloseHandle(hPipe);
    DeleteCriticalSection(&csConsole);
    wprintf(L"Соединение закрыто.\n");

    return 0;
}