#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <sddl.h>
#include <locale.h>
#include <process.h>
#include <stdlib.h>

#define PIPE_NAME "\\\\.\\pipe\\ChatPipe"
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 5

typedef struct {
    HANDLE hPipe;
    DWORD clientId;
    char name[50];
    HANDLE hThread;
} CLIENT_INFO;

CLIENT_INFO* clients[MAX_CLIENTS];
int clientCount = 0;
CRITICAL_SECTION cs;
HANDLE hMutex;  // Мьютекс для синхронизации

// Функция рассылки сообщений всем клиентам
void BroadcastMessage(const char* message, HANDLE excludePipe) {
    EnterCriticalSection(&cs);

    for (int i = 0; i < clientCount; i++) {
        if (clients[i] != NULL && clients[i]->hPipe != excludePipe) {
            DWORD bytesWritten;
            WriteFile(clients[i]->hPipe, message, strlen(message) + 1, &bytesWritten, NULL);
        }
    }

    LeaveCriticalSection(&cs);
}

// Поток для обработки клиента
unsigned int __stdcall ClientThread(void* param) {
    CLIENT_INFO* client = (CLIENT_INFO*)param;
    HANDLE hPipe = client->hPipe;
    char buffer[BUFFER_SIZE];
    DWORD bytesRead;
    BOOL connected = TRUE;

    // Отправляем приветствие
    char welcome[BUFFER_SIZE];
    snprintf(welcome, BUFFER_SIZE, "Добро пожаловать в чат, %s!\n", client->name);
    WriteFile(hPipe, welcome, strlen(welcome) + 1, &bytesRead, NULL);

    // Оповещаем всех о входе нового пользователя
    char joinMsg[BUFFER_SIZE];
    snprintf(joinMsg, BUFFER_SIZE, "Система: %s подключился к чату", client->name);
    BroadcastMessage(joinMsg, hPipe);

    printf("[Система]: %s подключился. Всего клиентов: %d\n", client->name, clientCount);

    // Цикл приема сообщений
    while (connected) {
        memset(buffer, 0, BUFFER_SIZE);

        // Читаем сообщение от клиента
        if (!ReadFile(hPipe, buffer, BUFFER_SIZE, &bytesRead, NULL)) {
            DWORD error = GetLastError();
            if (error != ERROR_BROKEN_PIPE && error != ERROR_NO_DATA) {
                printf("[Ошибка]: ReadFile для %s, код: %d\n", client->name, error);
            }
            break;
        }

        if (bytesRead == 0) {
            break;
        }

        buffer[bytesRead] = '\0';

        // Проверка на выход
        if (strcmp(buffer, "/quit") == 0) {
            printf("[%s]: Пользователь вышел из чата\n", client->name);
            break;
        }

        // Отправляем сообщение всем (включая отправителя для подтверждения)
        if (strlen(buffer) > 0) {
            char formattedMsg[BUFFER_SIZE];
            snprintf(formattedMsg, BUFFER_SIZE, "%s: %s", client->name, buffer);
            printf("%s\n", formattedMsg);

            // Рассылаем всем клиентам
            EnterCriticalSection(&cs);
            for (int i = 0; i < clientCount; i++) {
                if (clients[i] != NULL) {
                    DWORD bytesWritten;
                    WriteFile(clients[i]->hPipe, formattedMsg, strlen(formattedMsg) + 1, &bytesWritten, NULL);
                }
            }
            LeaveCriticalSection(&cs);
        }
    }

    // Сохраняем имя перед удалением
    char leaveName[50];
    strcpy(leaveName, client->name);

    // Удаляем клиента из списка
    EnterCriticalSection(&cs);
    int index = -1;
    for (int i = 0; i < clientCount; i++) {
        if (clients[i] == client) {
            index = i;
            break;
        }
    }

    if (index != -1) {
        for (int j = index; j < clientCount - 1; j++) {
            clients[j] = clients[j + 1];
        }
        clientCount--;
    }
    LeaveCriticalSection(&cs);

    // Оповещаем всех о выходе
    char leaveMsg[BUFFER_SIZE];
    snprintf(leaveMsg, BUFFER_SIZE, "Система: %s покинул чат", leaveName);

    EnterCriticalSection(&cs);
    for (int i = 0; i < clientCount; i++) {
        if (clients[i] != NULL) {
            DWORD bytesWritten;
            WriteFile(clients[i]->hPipe, leaveMsg, strlen(leaveMsg) + 1, &bytesWritten, NULL);
        }
    }
    LeaveCriticalSection(&cs);

    // Закрываем соединение
    FlushFileBuffers(hPipe);
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);

    printf("[Система]: %s отключился. Всего клиентов: %d\n", leaveName, clientCount);

    free(client);
    return 0;
}

int main() {
    HANDLE hPipe;
    PSECURITY_DESCRIPTOR pSD = NULL;
    SECURITY_ATTRIBUTES sa;

    setlocale(LC_ALL, "rus");
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);

    InitializeCriticalSection(&cs);

    // Настройка безопасности для Windows
    const char* sddlString = "D:(A;;GA;;;AU)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(sddlString, SDDL_REVISION_1, &pSD, NULL)) {
        printf("Ошибка создания дескриптора безопасности: %d\n", GetLastError());
        DeleteCriticalSection(&cs);
        return 1;
    }

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = pSD;
    sa.bInheritHandle = FALSE;

    printf("=============================================\n");
    printf("Чат-сервер запущен (макс. %d клиентов)\n", MAX_CLIENTS);
    printf("=============================================\n\n");

    DWORD clientIdCounter = 0;

    // Основной цикл сервера
    while (1) {
        // Проверка лимита клиентов
        if (clientCount >= MAX_CLIENTS) {
            printf("[Система]: Достигнут лимит клиентов (%d). Ожидание...\n", MAX_CLIENTS);
            Sleep(1000);
            continue;
        }

        // Создание именованного канала
        hPipe = CreateNamedPipe(
            TEXT(PIPE_NAME),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            BUFFER_SIZE,
            BUFFER_SIZE,
            0,
            &sa
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            printf("Ошибка CreateNamedPipe: %d\n", GetLastError());
            Sleep(1000);
            continue;
        }

        printf("Ожидание подключения клиента...\n");

        // Ожидание подключения
        BOOL connected = ConnectNamedPipe(hPipe, NULL);
        if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
            printf("Ошибка ConnectNamedPipe: %d\n", GetLastError());
            CloseHandle(hPipe);
            continue;
        }

        printf("Клиент подключился! Получение имени...\n");

        // Чтение имени клиента
        char clientName[50];
        DWORD bytesRead;
        BOOL readResult = ReadFile(hPipe, clientName, sizeof(clientName), &bytesRead, NULL);

        if (!readResult || bytesRead == 0) {
            printf("Ошибка чтения имени клиента\n");
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            continue;
        }

        clientName[bytesRead] = '\0';
        // Удаляем символы новой строки
        clientName[strcspn(clientName, "\r\n")] = 0;

        if (strlen(clientName) == 0) {
            strcpy(clientName, "Аноним");
        }

        // Создание структуры клиента
        CLIENT_INFO* newClient = (CLIENT_INFO*)malloc(sizeof(CLIENT_INFO));
        newClient->hPipe = hPipe;
        newClient->clientId = ++clientIdCounter;
        strcpy(newClient->name, clientName);

        // Добавление в список клиентов
        EnterCriticalSection(&cs);
        clients[clientCount++] = newClient;
        LeaveCriticalSection(&cs);

        // Создание потока для клиента
        newClient->hThread = (HANDLE)_beginthreadex(NULL, 0, ClientThread, newClient, 0, NULL);

        if (newClient->hThread == NULL) {
            printf("Ошибка создания потока для клиента\n");
            EnterCriticalSection(&cs);
            clientCount--;
            LeaveCriticalSection(&cs);
            free(newClient);
        }
    }

    DeleteCriticalSection(&cs);
    LocalFree(pSD);
    return 0;
}