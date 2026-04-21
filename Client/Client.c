#include <windows.h>
#include <stdio.h>
#include <locale.h>

#define PIPE_NAME L"\\\\.\\pipe\\ChatPipe"
#define BUFFER_SIZE 1024

// Функция отправки файла через канал
void SendFile(HANDLE hPipe, const wchar_t* filePath) {
    HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        wprintf(L"Не удалось открыть файл: %s (ошибка %d)\n", filePath, GetLastError());
        return;
    }

    // Получаем размер файла
    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE) {
        wprintf(L"Ошибка получения размера файла\n");
        CloseHandle(hFile);
        return;
    }

    // Извлекаем имя файла из пути
    const wchar_t* fileName = wcsrchr(filePath, L'\\');
    if (fileName == NULL) fileName = wcsrchr(filePath, L'/');
    if (fileName != NULL) fileName++;
    else fileName = filePath;

    // Отправляем заголовок: "/file <имя> <размер>"
    wchar_t header[BUFFER_SIZE];
    swprintf(header, BUFFER_SIZE, L"/file %s %lu", fileName, fileSize);
    DWORD bytesWritten;
    DWORD headerBytes = (wcslen(header) + 1) * sizeof(wchar_t);
    if (!WriteFile(hPipe, header, headerBytes, &bytesWritten, NULL)) {
        wprintf(L"Ошибка отправки заголовка файла\n");
        CloseHandle(hFile);
        return;
    }

    // Отправляем содержимое файла
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
        wprintf(L"Файл '%s' (%lu байт) отправлен\n", fileName, fileSize);
    }
    free(fileBuffer);
}

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

        // Проверка на команду /send
        if (wcsncmp(buffer, L"/send ", 6) == 0) {
            wchar_t* filePath = buffer + 6;
            // Удаляем возможные кавычки
            if (filePath[0] == L'"') {
                filePath++;
                wchar_t* endQuote = wcschr(filePath, L'"');
                if (endQuote) *endQuote = L'\0';
            }
            SendFile(hPipe, filePath);
            continue;
        }

        // Обычное сообщение
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