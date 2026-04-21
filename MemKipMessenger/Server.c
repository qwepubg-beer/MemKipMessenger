#include <windows.h>
#include <stdio.h>
#include <sddl.h>
#include <locale.h>
#include <shlobj.h>   // для SHCreateDirectoryExW (опционально)

#define PIPE_NAME L"\\\\.\\pipe\\ChatPipe"
#define BUFFER_SIZE 1024
#define RECEIVED_FOLDER L".\\received_files\\"

// Функция приёма файла от клиента
void ReceiveFile(HANDLE hPipe, const wchar_t* header) {
    wchar_t fileName[MAX_PATH];
    DWORD fileSize;

    // Парсим заголовок: "/file <имя> <размер>"
    if (swscanf(header, L"/file %s %lu", fileName, &fileSize) != 2) {
        wprintf(L"Ошибка разбора заголовка файла\n");
        return;
    }

    // Создаём папку для сохранения, если её нет
    CreateDirectoryW(RECEIVED_FOLDER, NULL);

    wchar_t fullPath[MAX_PATH];
    swprintf(fullPath, MAX_PATH, L"%s%s", RECEIVED_FOLDER, fileName);

    // Создаём файл для записи
    HANDLE hFile = CreateFileW(fullPath, GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        wprintf(L"Не удалось создать файл: %s (ошибка %d)\n", fullPath, GetLastError());
        // Пропускаем данные файла, чтобы не сбить протокол
        byte* dummy = (byte*)malloc(fileSize);
        DWORD bytesRead;
        ReadFile(hPipe, dummy, fileSize, &bytesRead, NULL);
        free(dummy);
        return;
    }

    // Принимаем содержимое файла
    byte* fileBuffer = (byte*)malloc(fileSize);
    if (fileBuffer == NULL) {
        wprintf(L"Ошибка выделения памяти\n");
        CloseHandle(hFile);
        return;
    }

    DWORD totalRead = 0;
    while (totalRead < fileSize) {
        DWORD bytesRead;
        DWORD toRead = min(BUFFER_SIZE * sizeof(wchar_t), fileSize - totalRead);
        if (!ReadFile(hPipe, fileBuffer + totalRead, toRead, &bytesRead, NULL) || bytesRead == 0) {
            wprintf(L"Ошибка чтения данных файла\n");
            free(fileBuffer);
            CloseHandle(hFile);
            return;
        }
        totalRead += bytesRead;
    }

    DWORD bytesWritten;
    if (!WriteFile(hFile, fileBuffer, fileSize, &bytesWritten, NULL) || bytesWritten != fileSize) {
        wprintf(L"Ошибка записи файла на диск\n");
    }
    else {
        wprintf(L"Файл '%s' (%lu байт) сохранён в %s\n", fileName, fileSize, fullPath);

        // Отправляем подтверждение клиенту
        wchar_t confirm[BUFFER_SIZE];
        swprintf(confirm, BUFFER_SIZE, L"Сервер: файл '%s' получен и сохранён", fileName);
        DWORD msgBytes = (wcslen(confirm) + 1) * sizeof(wchar_t);
        WriteFile(hPipe, confirm, msgBytes, &bytesWritten, NULL);
    }

    free(fileBuffer);
    CloseHandle(hFile);
}

int main() {
    HANDLE hPipe;
    wchar_t buffer[BUFFER_SIZE];
    wchar_t clientName[64] = L"Гость";
    DWORD bytesRead, bytesWritten;
    PSECURITY_DESCRIPTOR pSD = NULL;
    SECURITY_ATTRIBUTES sa;

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

        // Получаем имя клиента
        if (ReadFile(hPipe, buffer, BUFFER_SIZE * sizeof(wchar_t), &bytesRead, NULL) && bytesRead > 0) {
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

            // Проверка: это файл?
            if (wcsncmp(buffer, L"/file ", 6) == 0) {
                ReceiveFile(hPipe, buffer);
                continue;
            }

            // Обычное сообщение
            wprintf(L"%s\n", buffer);

            wprintf(L"Борисс: ");
            HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
            DWORD charsRead;
            ReadConsoleW(hStdIn, buffer, BUFFER_SIZE, &charsRead, NULL);
            if (charsRead >= 2 && buffer[charsRead - 2] == L'\r')
                buffer[charsRead - 2] = L'\0';
            else
                buffer[charsRead] = L'\0';

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