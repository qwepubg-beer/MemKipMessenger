#include <windows.h>
#include <stdio.h>
#include <sddl.h>
#include <locale.h>

#define PIPE_NAME L"\\\\.\\pipe\\ChatPipe"
#define BUFFER_SIZE 1024

int main() {
    HANDLE hPipe;
    wchar_t buffer[BUFFER_SIZE];
    wchar_t clientName[64];
    DWORD bytesRead, bytesWritten;
    PSECURITY_DESCRIPTOR pSD = NULL;
    SECURITY_ATTRIBUTES sa;

    // Устанавливаем локаль и режим консоли для Unicode
    setlocale(LC_ALL, "");
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);

    const wchar_t* sddlString = L"D:(A;;GA;;;AU)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
        sddlString, SDDL_REVISION_1, &pSD, NULL)) {
        wprintf(L"Ошибка создания дескриптора безопасности: %d\n", GetLastError());
        return 1;
    }

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = pSD;
    sa.bInheritHandle = FALSE;

    wprintf(L"Сервер запущен с открытым доступом...\n");

    hPipe = CreateNamedPipeW(
        PIPE_NAME,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,
        BUFFER_SIZE * sizeof(wchar_t),
        BUFFER_SIZE * sizeof(wchar_t),
        0,
        &sa);

    LocalFree(pSD);

    if (hPipe == INVALID_HANDLE_VALUE) {
        wprintf(L"CreateNamedPipe не удался. Ошибка: %d\n", GetLastError());
        return 1;
    }

    while (1) {
        wprintf(L"\nОжидание подключения клиента...\n");

        if (!ConnectNamedPipe(hPipe, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) {
            wprintf(L"ConnectNamedPipe не удался. Ошибка: %d\n", GetLastError());
            break;
        }

        // Читаем имя клиента (в широких символах)
        if (ReadFile(hPipe, buffer, BUFFER_SIZE * sizeof(wchar_t), &bytesRead, NULL) && bytesRead > 0) {
            // bytesRead содержит количество прочитанных байт, делим на sizeof(wchar_t)
            DWORD wcharsRead = bytesRead / sizeof(wchar_t);
            if (wcharsRead > 0) {
                buffer[wcharsRead] = L'\0';
                wcscpy_s(clientName, 64, buffer);
            }
        }
        else {
            DisconnectNamedPipe(hPipe);
            continue;
        }

        wprintf(L"Клиент '%s' подключился!\n", clientName);
        wprintf(L"=============================================\n");

        while (1) {
            wprintf(L"Ожидание сообщения от %s...\n", clientName);
            if (!ReadFile(hPipe, buffer, BUFFER_SIZE * sizeof(wchar_t), &bytesRead, NULL) || bytesRead == 0) {
                wprintf(L"Клиент '%s' отключился.\n", clientName);
                break;
            }
            DWORD wcharsRead = bytesRead / sizeof(wchar_t);
            buffer[wcharsRead] = L'\0';
            wprintf(L"%s\n", buffer);

            wprintf(L"Борисс: ");
            // Читаем ввод с консоли в широких символах
            HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
            DWORD charsRead;
            ReadConsoleW(hStdIn, buffer, BUFFER_SIZE, &charsRead, NULL);
            // Удаляем символы \r\n в конце
            if (charsRead >= 2 && buffer[charsRead - 2] == L'\r') {
                buffer[charsRead - 2] = L'\0';
            }
            else {
                buffer[charsRead] = L'\0';
            }

            wchar_t formatted[BUFFER_SIZE + 64];
            swprintf(formatted, BUFFER_SIZE + 64, L"Борисс: %s", buffer);

            DWORD bytesToWrite = (DWORD)(wcslen(formatted) + 1) * sizeof(wchar_t);
            if (!WriteFile(hPipe, formatted, bytesToWrite, &bytesWritten, NULL)) {
                wprintf(L"Не удалось отправить сообщение. Ошибка: %d\n", GetLastError());
                break;
            }
        }

        DisconnectNamedPipe(hPipe);
    }

    CloseHandle(hPipe);
    return 0;
}