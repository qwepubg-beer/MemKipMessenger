#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <stdbool.h>

#define PIPE_NAME "\\\\.\\pipe\\ChatPipe"
#define BUFFER_SIZE 1024

HANDLE hPipe;
CRITICAL_SECTION csConsole;
volatile BOOL connected = TRUE;
HANDLE hReadThread;

// Поток для приема сообщений
DWORD WINAPI ReceiveThread(LPVOID param) {
    char buffer[BUFFER_SIZE];
    DWORD bytesRead;

    while (connected) {
        memset(buffer, 0, BUFFER_SIZE);

        if (!ReadFile(hPipe, buffer, BUFFER_SIZE, &bytesRead, NULL)) {
            DWORD error = GetLastError();
            if (connected && error != ERROR_BROKEN_PIPE) {
                EnterCriticalSection(&csConsole);
                printf("\n[Система]: Ошибка чтения: %d\n", error);
                printf("[Система]: Соединение с сервером потеряно\n");
                LeaveCriticalSection(&csConsole);
            }
            connected = FALSE;
            break;
        }

        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';
            EnterCriticalSection(&csConsole);
            printf("\n%s\n", buffer);
            printf("Вы: ");
            fflush(stdout);
            LeaveCriticalSection(&csConsole);
        }
    }

    return 0;
}

int main() {
    char buffer[BUFFER_SIZE];
    char userName[50];

    setlocale(LC_ALL, "rus");
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);
    InitializeCriticalSection(&csConsole);

    printf("=====================================\n");
    printf("       Чат-клиент\n");
    printf("=====================================\n\n");

    // Ввод имени
    printf("Введите ваше имя: ");
    fgets(userName, sizeof(userName), stdin);
    userName[strcspn(userName, "\n")] = 0;
    userName[strcspn(userName, "\r")] = 0;

    if (strlen(userName) == 0) {
        strcpy(userName, "Аноним");
    }

    printf("\nПодключение к серверу...\n");

    // Ожидание сервера
    if (!WaitNamedPipe(TEXT(PIPE_NAME), 5000)) {
        printf("Ошибка: Сервер не запущен или не отвечает\n");
        printf("Убедитесь, что сервер запущен и повторите попытку\n");
        DeleteCriticalSection(&csConsole);
        return 1;
    }

    // Подключение к серверу
    hPipe = CreateFile(
        TEXT(PIPE_NAME),
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hPipe == INVALID_HANDLE_VALUE) {
        printf("Ошибка подключения к серверу. Код: %d\n", GetLastError());
        DeleteCriticalSection(&csConsole);
        return 1;
    }

    // Переводим канал в режим сообщений
    DWORD pipeMode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(hPipe, &pipeMode, NULL, NULL)) {
        printf("Ошибка установки режима канала\n");
        CloseHandle(hPipe);
        DeleteCriticalSection(&csConsole);
        return 1;
    }

    // Отправляем имя серверу
    DWORD bytesWritten;
    if (!WriteFile(hPipe, userName, strlen(userName) + 1, &bytesWritten, NULL)) {
        printf("Ошибка отправки имени\n");
        CloseHandle(hPipe);
        DeleteCriticalSection(&csConsole);
        return 1;
    }

    // Запускаем поток приема сообщений
    hReadThread = CreateThread(NULL, 0, ReceiveThread, NULL, 0, NULL);
    if (hReadThread == NULL) {
        printf("Ошибка создания потока приема\n");
        CloseHandle(hPipe);
        DeleteCriticalSection(&csConsole);
        return 1;
    }

    printf("\n=====================================\n");
    printf("Добро пожаловать в чат, %s!\n", userName);
    printf("=====================================\n");
    printf("Введите /quit для выхода\n");
    printf("=====================================\n\n");

    // Основной цикл отправки сообщений
    while (connected) {
        printf("Вы: ");
        fflush(stdout);

        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            break;
        }

        // Удаляем символ новой строки
        buffer[strcspn(buffer, "\n")] = 0;
        buffer[strcspn(buffer, "\r")] = 0;

        if (strlen(buffer) == 0) {
            continue;
        }

        // Проверка на выход
        if (strcmp(buffer, "/quit") == 0) {
            printf("Выход из чата...\n");
            // Отправляем команду выхода серверу
            WriteFile(hPipe, buffer, strlen(buffer) + 1, &bytesWritten, NULL);
            connected = FALSE;
            break;
        }

        // Отправляем сообщение серверу
        if (!WriteFile(hPipe, buffer, strlen(buffer) + 1, &bytesWritten, NULL)) {
            printf("\n[Ошибка]: Не удалось отправить сообщение\n");
            connected = FALSE;
            break;
        }
    }

    // Завершение работы
    connected = FALSE;

    if (hReadThread != NULL) {
        WaitForSingleObject(hReadThread, 2000);
        CloseHandle(hReadThread);
    }

    if (hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(hPipe);
    }

    DeleteCriticalSection(&csConsole);
    printf("Соединение закрыто.\n");

    return 0;
}