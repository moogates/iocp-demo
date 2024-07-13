#include <iostream>
#include <winsock2.h>

#include <algorithm>

#pragma comment(lib,"ws2_32.lib")

// https://stackoverflow.com/questions/10998504/winsock2-how-to-use-iocp-on-client-side

enum ConnectionStatus {
    S_ACCEPTED = 0,
    S_RECEIVING,
    S_SENDING
};
typedef struct
{
    WSAOVERLAPPED Overlapped;
    SOCKET Socket;
    WSABUF wsaBuf;
    char Buffer[1024];
    // DWORD bytesReceived;  // always ZERO
    DWORD BytesSent;
    DWORD BytesToSend;
    ConnectionStatus status;

} PER_IO_DATA, * LPPER_IO_DATA;

std::string str_toupper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        // static_cast<int(*)(int)>(std::toupper)         // wrong
        // [](int c){ return std::toupper(c); }           // wrong
        // [](char c){ return std::toupper(c); }          // wrong
        [](unsigned char c) { return std::toupper(c); } // correct
    );
    return s;
}

void SetNonblocking(int fd)
{
    unsigned long ul = 1;
    int ret = ioctlsocket(fd, FIONBIO, &ul);
    if (ret == SOCKET_ERROR)
    {
        printf("set socket:%d non blocking failed:%s\n", WSAGetLastError());
    }
}



void IssueReceive(PER_IO_DATA* io_data) {
    io_data->wsaBuf.buf = io_data->Buffer;
    io_data->wsaBuf.len = sizeof(io_data->Buffer);
    io_data->status = S_RECEIVING;

    DWORD flags = 0;
    if (::WSARecv(io_data->Socket,  // TODO: 验证有无可能直接立即读取到了数据，未触发 completion port 事件？
        &(io_data->wsaBuf),
        1,
        NULL,   // 该参数应该是NULL，具体请参考文档:
                //   https://learn.microsoft.com/zh-cn/windows/win32/api/winsock2/nf-winsock2-wsarecv#------i-o 
        &flags, // TODO : could be NULL?
        &(io_data->Overlapped), NULL) == SOCKET_ERROR)
    {
        if (::WSAGetLastError() != WSA_IO_PENDING)
        {
            std::cerr << "HandleBytesSent WSARecv error" << std::endl;
            // TODO : error handling
        }
    }
}

static void HandleBytesReveived(PER_IO_DATA* io_data) {
    DWORD bytesReceived = 0;
    DWORD flags = 0;
    ::WSAGetOverlappedResult(
        io_data->Socket,
        &io_data->Overlapped,
        &bytesReceived,
        FALSE,
        &flags
    );

    if (bytesReceived == 0) {
        // TODO : conn closed?
        return;
    }

    std::string dataReceived(std::string(io_data->Buffer, bytesReceived));
    std::cout << "Reveived: [" << dataReceived << "]" << std::endl;
    std::string byteToSend(str_toupper(dataReceived));
    strcpy_s(io_data->Buffer, byteToSend.c_str());

    io_data->wsaBuf.buf = io_data->Buffer;
    io_data->wsaBuf.len = byteToSend.size(); // not sizeof(io_data->Buffer)
    io_data->BytesToSend = byteToSend.size();

    io_data->status = S_SENDING;

    if (::WSASend(io_data->Socket,
            &(io_data->wsaBuf),
            1,
            &io_data->BytesSent,
            0,
            &(io_data->Overlapped), NULL) == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            std::cerr << "WSASend error" << std::endl;
            // TODO : error handling
        }
    }
    else {
        std::cout << "WSASend DONE" << std::endl;
        // TODO : handle partly done

        IssueReceive(io_data);
    }
}

static void HandleBytesSent(PER_IO_DATA* io_data) {

    if (io_data->BytesSent == 0) {
        // TODO : conn closed?
        return;
    }

    if (io_data->BytesSent < io_data->BytesToSend) {
        // TODO: test by intentional small wsaBuf.len
        std::cout << "To send more ..." << std::endl;
        io_data->wsaBuf.buf += io_data->BytesSent;

        io_data->wsaBuf.len -= io_data->BytesSent;
        io_data->BytesToSend -= io_data->BytesSent;

        if (::WSASend(io_data->Socket,
            &(io_data->wsaBuf), 1,
            &io_data->BytesSent,
            0,
            &(io_data->Overlapped), NULL) == SOCKET_ERROR)
        {
            if (WSAGetLastError() != WSA_IO_PENDING)
            {
                std::cerr << "HandleBytesSent WSASend error" << std::endl;
                // TODO : error handling
            }
        }
        return;
    }

    io_data->wsaBuf.buf = io_data->Buffer;
    io_data->wsaBuf.len = sizeof(io_data->Buffer);
    io_data->status = S_RECEIVING;

    DWORD flags = 0;
    if (::WSARecv(io_data->Socket,  // TODO: 验证有无可能直接立即读取到了数据，未触发 completion port 事件？
        &(io_data->wsaBuf),
        1,
        NULL,   // 该参数应该是NULL，具体请参考文档:
                // https://learn.microsoft.com/zh-cn/windows/win32/api/winsock2/nf-winsock2-wsarecv#------i-o 
        &flags, // TODO : could be NULL?
        &(io_data->Overlapped), NULL) == SOCKET_ERROR)
    {
        if (::WSAGetLastError() != WSA_IO_PENDING)
        {
            std::cerr << "HandleBytesSent WSARecv error" << std::endl;
            // TODO : error handling
        }
    }

}

static DWORD WINAPI ServerWorkerThread(LPVOID lpParameter)
{
    HANDLE hCompletionPort = (HANDLE)lpParameter;
    DWORD NumBytesSent = 0;
    ULONG CompletionKey;
    LPPER_IO_DATA PerIoData;

    while (GetQueuedCompletionStatus(hCompletionPort, &NumBytesSent, (ULONG*)&CompletionKey, (LPOVERLAPPED*)&PerIoData, INFINITE))
    {
        if (!PerIoData) {
            std::cout << "ERROR! NULL PerIoData! ------------------\r\n\r\n";
            continue;
        }
        if (PerIoData->status == S_RECEIVING) {
            HandleBytesReveived(PerIoData);
        } else if (PerIoData->status == S_RECEIVING) {
            HandleBytesSent(PerIoData);
        }
        continue;

        if (NumBytesSent == 0)
        {
            std::cout << "Client disconnected!\r\n\r\n";
            break;
        }
        else
        {
            PerIoData->BytesSent += NumBytesSent;
            if (PerIoData->BytesSent < PerIoData->BytesToSend)
            {
                PerIoData->wsaBuf.buf = &(PerIoData->Buffer[PerIoData->BytesSent]);
                PerIoData->wsaBuf.len = (PerIoData->BytesToSend - PerIoData->BytesSent);
            }
            else
            {
                PerIoData->wsaBuf.buf = PerIoData->Buffer;
                PerIoData->wsaBuf.len = strlen(PerIoData->Buffer);
                PerIoData->BytesSent = 0;
                PerIoData->BytesToSend = PerIoData->wsaBuf.len;
            }

            if (::WSASend(PerIoData->Socket, &(PerIoData->wsaBuf), 1,
                    &NumBytesSent, 0, &(PerIoData->Overlapped), NULL) != 0)
                break;

            if (::WSAGetLastError() != WSA_IO_PENDING)
                break;
        }
    }

    closesocket(PerIoData->Socket);
    delete PerIoData;

    return 0;
}

int main()
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
        HANDLE hThread = CreateThread(NULL, 0, ServerWorkerThread, hCompletionPort, 0, NULL);
        CloseHandle(hThread);
    }

    SOCKET listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (listenSocket == INVALID_SOCKET)
        return 0;

    SOCKADDR_IN server;
    ZeroMemory(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(8888);

    if (bind(listenSocket, (SOCKADDR*)(&server), sizeof(server)) != 0)
        return 0;

    if (listen(listenSocket, 1) != 0)
        return 0;

    std::cout << "Waiting for incoming connection...\r\n";

    while (true) {
        SOCKET acceptSocket;
        do
        {
            sockaddr_in saClient;
            int nClientSize = sizeof(saClient);
            acceptSocket = WSAAccept(listenSocket, (SOCKADDR*)&saClient, &nClientSize, NULL, NULL);
        } while (acceptSocket == INVALID_SOCKET);

        SetNonblocking(acceptSocket);

        // binding socket to Completion Port
        if (NULL == ::CreateIoCompletionPort((HANDLE)acceptSocket, hCompletionPort, 0, 0)) {
            // TODO: error handling
            break;
        }

        LPPER_IO_DATA pPerIoData = new PER_IO_DATA;
        ZeroMemory(pPerIoData, sizeof(PER_IO_DATA));

        // pPerIoData->Overlapped.hEvent = WSACreateEvent();
        pPerIoData->Socket = acceptSocket;
        pPerIoData->wsaBuf.buf = pPerIoData->Buffer;
        pPerIoData->wsaBuf.len = sizeof(pPerIoData->Buffer);

        pPerIoData->status = S_RECEIVING;

        DWORD flags = 0;
        if (::WSARecv(acceptSocket,  // TODO: 验证有无可能直接立即读取到了数据，未触发 completion port 事件？
                &(pPerIoData->wsaBuf),
                1,
                NULL, // bytesReceived, 如果接收操作立即完成，该字段返回字节数； 如果 lpOverlapped 参数
                      //    不是 NULL，请对此参数使用 NULL
                &flags, // TODO : could be NULL?
                &(pPerIoData->Overlapped), NULL) == SOCKET_ERROR)
        {
            if (::WSAGetLastError() != WSA_IO_PENDING)
            {
                std::cout << "Client error.\r\n\r\n";
                delete pPerIoData;
                ::closesocket(acceptSocket);
                continue;
            }
        }
        else {
            // TODO: IOCP模式下，能直接成功走到这里吗？如果能走到这里，如何得到 bytesReceived？
            std::cout << "Read ok , bytes=??.\r\n\r\n";

        }

        std::cout << "Client connected. Try to receive ...\r\n\r\n";

        //DWORD dwNumSent;
        //if (WSASend(acceptSocket, &(pPerIoData->wsaBuf), 1, &dwNumSent, 0, &(pPerIoData->Overlapped), NULL) == SOCKET_ERROR)
        //{
        //    if (WSAGetLastError() != WSA_IO_PENDING)
        //    {
        //        delete pPerIoData;
        //        return 0;
        //    }
        //}
    }

    while (TRUE)
        Sleep(1000);

    // FIXME : close the acceptSocket
    // shutdown(acceptSocket, SD_BOTH);
    // closesocket(acceptSocket);

    WSACleanup();
    return 0;
}

