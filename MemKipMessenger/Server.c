#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <sddl.h>
#include <locale.h>
#include <process.h>
#include <stdlib.h>
#include <fcntl.h>
#include <io.h>

#define PIPE_NAME L"\\\\.\\pipe\\ChatPipe"
#define BUFFER_SIZE 1024  // широких символов
#define MAX_CLIENTS 5

typedef struct {
    HANDLE hPipe;
    DWORD clientId;
    wchar_t name[50];
} CLIENT_INFO;

CLIENT_INFO* clients[MAX_CLIENTS];
int clientCount = 0;
CRITICAL_SECTION cs;

void BroadcastMessage(const wchar_t* message, HANDLE excludePipe) {
    EnterCriticalSection(&cs);
    DWORD bytes = (wcslen(message) + 1) * sizeof(wchar_t);
    for (int i = 0; i < clientCount; i++) {
        if (clients[i] != NULL && clients[i]->hPipe != excludePipe) {
            DWORD bytesWritten;
            WriteFile(clients[i]->hPipe, message, bytes, &bytesWritten, NULL);
        }
    }
    LeaveCriticalSection(&cs);
}

unsigned int __stdcall ClientThread(void* param) {
    CLIENT_INFO* client = (CLIENT_INFO*)param;
    HANDLE hPipe = client->hPipe;
    wchar_t buffer[BUFFER_SIZE];
    DWORD bytesRead;
    BOOL connected = TRUE;

    wchar_t welcome[BUFFER_SIZE];
    swprintf(welcome, BUFFER_SIZE, L"Добро пожаловать в чат, %s!\n", client->name);
    WriteFile(hPipe, welcome, (wcslen(welcome) + 1) * sizeof(wchar_t), &bytesRead, NULL);

    wchar_t joinMsg[BUFFER_SIZE];
    swprintf(joinMsg, BUFFER_SIZE, L"Система: %s подключился к чату", client->name);
    BroadcastMessage(joinMsg, hPipe);
    wprintf(L"[Система]: %s подключился. Всего клиентов: %d\n", client->name, clientCount);

    while (connected) {
        memset(buffer, 0, sizeof(buffer));
        if (!ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL)) {
            DWORD error = GetLastError();
            if (error != ERROR_BROKEN_PIPE && error != ERROR_NO_DATA) {
                wprintf(L"[Ошибка]: ReadFile для %s, код: %d\n", client->name, error);
            }
            break;
        }
        if (bytesRead == 0) break;
        buffer[bytesRead / sizeof(wchar_t)] = L'\0';

        if (wcscmp(buffer, L"/quit") == 0) {
            wprintf(L"[%s]: Пользователь вышел из чата\n", client->name);
            break;
        }

        if (wcslen(buffer) > 0) {
            wchar_t formattedMsg[BUFFER_SIZE];
            swprintf(formattedMsg, BUFFER_SIZE, L"%s: %s", client->name, buffer);
            wprintf(L"%s\n", formattedMsg);

            EnterCriticalSection(&cs);
            DWORD bytes = (wcslen(formattedMsg) + 1) * sizeof(wchar_t);
            for (int i = 0; i < clientCount; i++) {
                if (clients[i] != NULL) {
                    DWORD bytesWritten;
                    WriteFile(clients[i]->hPipe, formattedMsg, bytes, &bytesWritten, NULL);
                }
            }
            LeaveCriticalSection(&cs);
        }
    }

    wchar_t leaveName[50];
    wcscpy(leaveName, client->name);

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

    wchar_t leaveMsg[BUFFER_SIZE];
    swprintf(leaveMsg, BUFFER_SIZE, L"Система: %s покинул чат", leaveName);
    EnterCriticalSection(&cs);
    DWORD bytes = (wcslen(leaveMsg) + 1) * sizeof(wchar_t);
    for (int i = 0; i < clientCount; i++) {
        if (clients[i] != NULL) {
            DWORD bytesWritten;
            WriteFile(clients[i]->hPipe, leaveMsg, bytes, &bytesWritten, NULL);
        }
    }
    LeaveCriticalSection(&cs);

    FlushFileBuffers(hPipe);
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);

    wprintf(L"[Система]: %s отключился. Всего клиентов: %d\n", leaveName, clientCount);

    free(client);
    return 0;
}

int main() {
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stdin), _O_U16TEXT);
    setlocale(LC_ALL, ".UTF8");

    HANDLE hPipe;
    PSECURITY_DESCRIPTOR pSD = NULL;
    SECURITY_ATTRIBUTES sa;

    InitializeCriticalSection(&cs);

    const char* sddlString = "D:(A;;GA;;;AU)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(sddlString, SDDL_REVISION_1, &pSD, NULL)) {
        wprintf(L"Ошибка создания дескриптора безопасности: %d\n", GetLastError());
        DeleteCriticalSection(&cs);
        return 1;
    }

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = pSD;
    sa.bInheritHandle = FALSE;

    wprintf(L"=============================================\n");
    wprintf(L"Чат-сервер (Unicode, макс. %d клиентов)\n", MAX_CLIENTS);
    wprintf(L"=============================================\n\n");

    DWORD clientIdCounter = 0;

    while (1) {
        if (clientCount >= MAX_CLIENTS) {
            wprintf(L"[Система]: Достигнут лимит клиентов (%d). Ожидание...\n", MAX_CLIENTS);
            Sleep(1000);
            continue;
        }

        hPipe = CreateNamedPipeW(
            PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            BUFFER_SIZE * sizeof(wchar_t),
            BUFFER_SIZE * sizeof(wchar_t),
            0,
            &sa
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            wprintf(L"Ошибка CreateNamedPipe: %d\n", GetLastError());
            Sleep(1000);
            continue;
        }

        wprintf(L"Ожидание подключения клиента...\n");

        BOOL connected = ConnectNamedPipe(hPipe, NULL);
        if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
            wprintf(L"Ошибка ConnectNamedPipe: %d\n", GetLastError());
            CloseHandle(hPipe);
            continue;
        }

        wprintf(L"Клиент подключился! Получение имени...\n");

        wchar_t clientName[50];
        DWORD bytesRead;
        BOOL readResult = ReadFile(hPipe, clientName, sizeof(clientName), &bytesRead, NULL);

        if (!readResult || bytesRead == 0) {
            wprintf(L"Ошибка чтения имени клиента\n");
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            continue;
        }

        clientName[bytesRead / sizeof(wchar_t)] = L'\0';
        clientName[wcscspn(clientName, L"\r\n")] = L'\0';

        if (wcslen(clientName) == 0) {
            wcscpy(clientName, L"Аноним");
        }

        CLIENT_INFO* newClient = (CLIENT_INFO*)malloc(sizeof(CLIENT_INFO));
        newClient->hPipe = hPipe;
        newClient->clientId = ++clientIdCounter;
        wcscpy(newClient->name, clientName);

        EnterCriticalSection(&cs);
        clients[clientCount++] = newClient;
        LeaveCriticalSection(&cs);

        HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, ClientThread, newClient, 0, NULL);
        if (hThread == NULL) {
            wprintf(L"Ошибка создания потока для клиента\n");
            EnterCriticalSection(&cs);
            clientCount--;
            LeaveCriticalSection(&cs);
            free(newClient);
        }
        else {
            CloseHandle(hThread);
        }
    }

    DeleteCriticalSection(&cs);
    LocalFree(pSD);
    return 0;
}