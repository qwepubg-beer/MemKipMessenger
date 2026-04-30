#include <windows.h>
#include <stdio.h>
#include <sddl.h>
#include <locale.h>

#define PIPE_NAME L"\\\\.\\pipe\\ChatPipe"
#define BUFFER_SIZE 1024
#define RECEIVED_FOLDER L".\\received_files\\"

// Приём файла от клиента
void ReceiveFile(HANDLE hPipe, const wchar_t* header) {
    wchar_t fileName[MAX_PATH];
    DWORD fileSize;

    if (swscanf(header, L"/file %s %lu", fileName, &fileSize) != 2) {
        wprintf(L"Ошибка разбора заголовка файла\n");
        return;
    }

    CreateDirectoryW(RECEIVED_FOLDER, NULL);
    wchar_t fullPath[MAX_PATH];
    swprintf(fullPath, MAX_PATH, L"%s%s", RECEIVED_FOLDER, fileName);

    HANDLE hFile = CreateFileW(fullPath, GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        wprintf(L"Не удалось создать файл: %s (ошибка %d)\n", fullPath, GetLastError());
        byte* dummy = (byte*)malloc(fileSize);
        DWORD bytesRead;
        ReadFile(hPipe, dummy, fileSize, &bytesRead, NULL);
        free(dummy);
        return;
    }

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

// Отправка файла клиенту
void SendFileToClient(HANDLE hPipe, const wchar_t* filePath) {
    HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        wprintf(L"Не удалось открыть файл: %s (ошибка %d)\n", filePath, GetLastError());
        wchar_t errMsg[] = L"/file_error\0";
        DWORD bytesWritten;
        WriteFile(hPipe, errMsg, sizeof(errMsg), &bytesWritten, NULL);
        return;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE) {
        wprintf(L"Ошибка получения размера файла\n");
        CloseHandle(hFile);
        return;
    }

    const wchar_t* fileName = wcsrchr(filePath, L'\\');
    if (fileName == NULL) fileName = wcsrchr(filePath, L'/');
    if (fileName != NULL) fileName++;
    else fileName = filePath;

    wchar_t header[BUFFER_SIZE];
    swprintf(header, BUFFER_SIZE, L"/file %s %lu", fileName, fileSize);
    DWORD bytesWritten;
    DWORD headerBytes = (wcslen(header) + 1) * sizeof(wchar_t);
    if (!WriteFile(hPipe, header, headerBytes, &bytesWritten, NULL)) {
        wprintf(L"Ошибка отправки заголовка файла\n");
        CloseHandle(hFile);
        return;
    }

    byte* fileBuffer = (byte*)malloc(fileSize);
    if (fileBuffer == NULL) {
        wprintf(L"Ошибка выделения памяти\n");
        CloseHandle(hFile);
        return;
    }

    DWORD bytesRead;
    if (!ReadFile(hFile, fileBuffer, fileSize, &bytesRead, NULL) || bytesRead != fileSize) {
        wprintf(L"Ошибка чтения файла\n");
        free(fileBuffer);
        CloseHandle(hFile);
        return;
    }
    CloseHandle(hFile);

    if (!WriteFile(hPipe, fileBuffer, fileSize, &bytesWritten, NULL) || bytesWritten != fileSize) {
        wprintf(L"Ошибка отправки данных файла\n");
    }
    else {
        wprintf(L"Файл '%s' (%lu байт) отправлен клиенту\n", fileName, fileSize);
    }
    free(fileBuffer);
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
    wprintf(L"команда /sendfile путь в ковычках\n");
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
            // Проверка на команду /take от клиента
            else if (wcsncmp(buffer, L"/take ", 6) == 0) {
                wchar_t* filePath = buffer + 6;
                // Убираем возможные пробелы в начале и кавычки
                while (*filePath == L' ') filePath++;
                if (filePath[0] == L'"') {
                    filePath++;
                    wchar_t* endQuote = wcschr(filePath, L'"');
                    if (endQuote) *endQuote = L'\0';
                }
                wprintf(L"Клиент запросил файл: %s\n", filePath);
                SendFileToClient(hPipe, filePath);
                continue;
            }
            // Обычное сообщение
            wprintf(L"%s\n", buffer);

            // Ввод ответа сервера
            wprintf(L"Сервер: ");
            HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
            DWORD charsRead;
            wchar_t input[BUFFER_SIZE];
            ReadConsoleW(hStdIn, input, BUFFER_SIZE, &charsRead, NULL);
            if (charsRead >= 2 && input[charsRead - 2] == L'\r')
                input[charsRead - 2] = L'\0';
            else
                input[charsRead] = L'\0';

            // Проверка на команду отправки файла
            if (wcsncmp(input, L"/sendfile ", 10) == 0) {
                wchar_t* filePath = input + 10;
                if (filePath[0] == L'"') {
                    filePath++;
                    wchar_t* endQuote = wcschr(filePath, L'"');
                    if (endQuote) *endQuote = L'\0';
                }
                SendFileToClient(hPipe, filePath);
                continue;
            }

            // Обычное сообщение
            wchar_t formatted[BUFFER_SIZE + 64];
            swprintf(formatted, BUFFER_SIZE + 64, L"Сервер: %s", input);

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