#include <iostream>
#include <string>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

// https://stackoverflow.com/questions/10998504/winsock2-how-to-use-iocp-on-client-side

typedef struct
{
    WSAOVERLAPPED Overlapped;
    SOCKET Socket;
    WSABUF wsaBuf;
    char Buffer[1024];
    DWORD Flags;
} PER_IO_DATA, * LPPER_IO_DATA;

static DWORD WINAPI ClientWorkerThread(LPVOID lpParameter)
{
    HANDLE hCompletionPort = (HANDLE)lpParameter;
    DWORD NumBytesRecv = 0;
    ULONG CompletionKey;
    LPPER_IO_DATA PerIoData;

    while (GetQueuedCompletionStatus(
        hCompletionPort,
        &NumBytesRecv,
        (PULONG_PTR)& CompletionKey,
        (LPOVERLAPPED*)&PerIoData,
        INFINITE))
    {
        if (!PerIoData) {
            std::cout << "No PerIoData ......\r\n\r\n";
            continue;
        }

        if (NumBytesRecv == 0)
        {
            std::cout << "Server disconnected!\r\n\r\n";
        }
        else
        {
            // use PerIoData->Buffer as needed...
            std::cout << std::string(PerIoData->Buffer, NumBytesRecv);

            PerIoData->wsaBuf.len = sizeof(PerIoData->Buffer);
            PerIoData->Flags = 0;

            if (WSARecv(PerIoData->Socket, &(PerIoData->wsaBuf), 1, &NumBytesRecv, &(PerIoData->Flags), &(PerIoData->Overlapped), NULL) == 0)
                continue;

            if (WSAGetLastError() == WSA_IO_PENDING)
                continue;
        }

        closesocket(PerIoData->Socket);
        delete PerIoData;
    }

    return 0;
}

int main(void)
{
    WSADATA WsaDat;
    if (WSAStartup(MAKEWORD(2, 2), &WsaDat) != 0)
        return 0;

    HANDLE hCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (!hCompletionPort)
        return 0;

    SYSTEM_INFO systemInfo;
    GetSystemInfo(&systemInfo);

    for (DWORD i = 0; i < systemInfo.dwNumberOfProcessors; ++i)
    {
        HANDLE hThread = CreateThread(NULL, 0, ClientWorkerThread, hCompletionPort, 0, NULL);
        CloseHandle(hThread);
    }

    SOCKET socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (socket == INVALID_SOCKET)
        return 0;

    SOCKADDR_IN SockAddr;
    SockAddr.sin_family = AF_INET;
    SockAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    SockAddr.sin_port = htons(8888);

    CreateIoCompletionPort((HANDLE)socket, hCompletionPort, 0, 0);

    if (WSAConnect(socket, (SOCKADDR*)(&SockAddr), sizeof(SockAddr), NULL, NULL, NULL, NULL) == SOCKET_ERROR)
        return 0;

    PER_IO_DATA* pPerIoData = new PER_IO_DATA;
    ZeroMemory(pPerIoData, sizeof(PER_IO_DATA));

    pPerIoData->Socket = socket;
    pPerIoData->Overlapped.hEvent = WSACreateEvent();
    pPerIoData->wsaBuf.buf = pPerIoData->Buffer;
    pPerIoData->wsaBuf.len = sizeof(pPerIoData->Buffer);

    DWORD dwNumRecv;
    if (WSARecv(socket, &(pPerIoData->wsaBuf), 1, &dwNumRecv, &(pPerIoData->Flags), &(pPerIoData->Overlapped), NULL) == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            delete pPerIoData;
            return 0;
        }
    }

    while (TRUE)
        Sleep(1000);

    shutdown(socket, SD_BOTH);
    closesocket(socket);

    WSACleanup();
    return 0;
}