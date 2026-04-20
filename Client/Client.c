#include <windows.h>
#include <stdio.h>
#include <locale.h>

#define PIPE_NAME L"\\\\.\\pipe\\ChatPipe"
#define BUFFER_SIZE 1024

int main() {
    setlocale(LC_ALL, "");
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);

    HANDLE hPipe;
    wchar_t buffer[BUFFER_SIZE];
    wchar_t clientName[64];
    DWORD bytesRead, bytesWritten;

    wprintf(L"Клиент запущен. Подключение к серверу...\n");

    wprintf(L"Введите ваше имя: ");
    HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD charsRead;
    ReadConsoleW(hStdIn, clientName, 64, &charsRead, NULL);
    if (charsRead >= 2 && clientName[charsRead - 2] == L'\r')
        clientName[charsRead - 2] = L'\0';
    else
        clientName[charsRead] = L'\0';

    if (!WaitNamedPipeW(PIPE_NAME, NMPWAIT_WAIT_FOREVER)) {
        wprintf(L"WaitNamedPipe не удался. Ошибка: %d\n", GetLastError());
        return 1;
    }

    hPipe = CreateFileW(
        PIPE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    if (hPipe == INVALID_HANDLE_VALUE) {
        wprintf(L"CreateFile не удался. Ошибка: %d\n", GetLastError());
        return 1;
    }

    wprintf(L"Подключено к серверу!\n");
    wprintf(L"=============================================\n");

    // Отправляем имя серверу
    DWORD bytesToWrite = (DWORD)(wcslen(clientName) + 1) * sizeof(wchar_t);
    if (!WriteFile(hPipe, clientName, bytesToWrite, &bytesWritten, NULL)) {
        wprintf(L"Не удалось отправить имя серверу. Ошибка: %d\n", GetLastError());
        CloseHandle(hPipe);
        return 1;
    }

    while (1) {
        wprintf(L"Вы (%s): ", clientName);
        ReadConsoleW(hStdIn, buffer, BUFFER_SIZE, &charsRead, NULL);
        if (charsRead >= 2 && buffer[charsRead - 2] == L'\r')
            buffer[charsRead - 2] = L'\0';
        else
            buffer[charsRead] = L'\0';

        wchar_t formatted[BUFFER_SIZE + 64];
        swprintf(formatted, BUFFER_SIZE + 64, L"%s: %s", clientName, buffer);

        bytesToWrite = (DWORD)(wcslen(formatted) + 1) * sizeof(wchar_t);
        if (!WriteFile(hPipe, formatted, bytesToWrite, &bytesWritten, NULL)) {
            wprintf(L"Не удалось отправить сообщение. Ошибка: %d\n", GetLastError());
            break;
        }

        wprintf(L"Ожидание ответа от сервера...\n");
        if (!ReadFile(hPipe, buffer, BUFFER_SIZE * sizeof(wchar_t), &bytesRead, NULL) || bytesRead == 0) {
            wprintf(L"Сервер отключился.\n");
            break;
        }
        DWORD wcharsRead = bytesRead / sizeof(wchar_t);
        buffer[wcharsRead] = L'\0';
        wprintf(L"%s\n", buffer);
    }

    CloseHandle(hPipe);
    return 0;
}