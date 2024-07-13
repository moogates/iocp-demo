#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>

// Windows下的高效网络模型IOCP完整示例
// https://wittonbell.github.io/posts/2023/2023-03-23-Windows%E4%B8%8B%E7%9A%84%E9%AB%98%E6%95%88%E7%BD%91%E7%BB%9C%E6%A8%A1%E5%9E%8BIOCP%E5%AE%8C%E6%95%B4%E7%A4%BA%E4%BE%8B/

#pragma comment(lib, "ws2_32.lib")

typedef enum IoKind
{
	IoREAD,
	IoWRITE
} IoKind;

typedef struct IoData
{
	OVERLAPPED Overlapped;
	WSABUF wsabuf;
	DWORD nBytes;
	IoKind opCode;
	SOCKET cliSock;
} IoData;

BOOL PostRead(IoData* data)
{
	memset(&data->Overlapped, 0, sizeof(data->Overlapped));
	data->nBytes = data->wsabuf.len;
	data->opCode = IoREAD;

	DWORD dwFlags = 0;
	int nRet = WSARecv(data->cliSock, &data->wsabuf, 1, &data->nBytes, &dwFlags, &data->Overlapped, NULL);
	if (nRet == SOCKET_ERROR && (ERROR_IO_PENDING != WSAGetLastError()))
	{
		printf("WASRecv Failed:%s\n", WSAGetLastError());
		closesocket(data->cliSock);
		free(data->wsabuf.buf);
		free(data);
		return FALSE;
	}
	return TRUE;
}

BOOL PostWrite(IoData* data, DWORD nSendLen)
{
	memset(&data->Overlapped, 0, sizeof(data->Overlapped));
	data->nBytes = nSendLen;
	data->opCode = IoWRITE;
	int nRet = WSASend(data->cliSock, &data->wsabuf, 1, &data->nBytes, 0, &(data->Overlapped), NULL);
	if (nRet == SOCKET_ERROR && (ERROR_IO_PENDING != WSAGetLastError()))
	{
		printf("WASSend Failed:%s", WSAGetLastError());
		closesocket(data->cliSock);
		free(data->wsabuf.buf);
		free(data);
		return FALSE;
	}
	return TRUE;
}

DWORD WINAPI WorkerThread(HANDLE hIOCP)
{
	IoData* ctx = NULL;
	DWORD dwIoSize = 0;
	void* lpCompletionKey = NULL;
	LPOVERLAPPED lpOverlapped = NULL;

	while (1)
	{
		GetQueuedCompletionStatus(hIOCP, &dwIoSize, (PULONG_PTR)&lpCompletionKey, (LPOVERLAPPED*)&lpOverlapped, INFINITE);
		ctx = (IoData*)lpOverlapped;
		if (dwIoSize == 0)
		{
			if (ctx == NULL)
			{
				printf("WorkerThread Exit...\n");
				break;
			}
			printf("Client:%d disconnect\n", ctx->cliSock);
			closesocket(ctx->cliSock);
			free(ctx->wsabuf.buf);
			free(ctx);
			continue;
		}
		if (ctx->opCode == IoREAD)
		{
			ctx->wsabuf.buf[dwIoSize] = 0;
			printf("%s\n", ctx->wsabuf.buf);
			PostWrite(ctx, dwIoSize);
		}
		else if (ctx->opCode == IoWRITE)
		{
			PostRead(ctx);
		}
	}
	return 0;
}

static BOOL IsExit = FALSE;

void OnSignal(int sig)
{
	IsExit = TRUE;
	printf("Recv exit signal...\n");
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

void NetWork(int port)
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	SOCKET m_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

	struct sockaddr_in server;
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	server.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

	bind(m_socket, (struct sockaddr*)&server, sizeof(server));
	listen(m_socket, 0);

	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	int threadCount = sysInfo.dwNumberOfProcessors;

	HANDLE hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, threadCount);
	for (int i = 0; i < threadCount; ++i)
	{
		HANDLE hThread;
		DWORD dwThreadId;
		hThread = CreateThread(NULL, 0, WorkerThread, hIOCP, 0, &dwThreadId);
		CloseHandle(hThread);
	}
	SetNonblocking(m_socket);
	while (!IsExit)
	{
		SOCKET cliSock = accept(m_socket, NULL, NULL);
		if (cliSock == SOCKET_ERROR)
		{
			Sleep(10);
			continue;
		}
		printf("Client:%d connected.\n", cliSock);
		if (CreateIoCompletionPort((HANDLE)cliSock, hIOCP, 0, 0) == NULL)
		{
			printf("Binding Client Socket to IO Completion Port Failed:%s\n", GetLastError());
			closesocket(cliSock);
		}
		else
		{
			IoData* data = (IoData*)malloc(sizeof(IoData));
			memset(data, 0, sizeof(IoData));
			data->wsabuf.buf = (char*)malloc(1024);
			data->wsabuf.len = 1024;
			data->cliSock = cliSock;
			PostRead(data);
		}
	}
	PostQueuedCompletionStatus(hIOCP, 0, 0, 0);
	closesocket(m_socket);
	WSACleanup();
}

int main()
{
	SetConsoleOutputCP(65001);
	signal(SIGINT, OnSignal);
	NetWork(6000);
	printf("exit\n");
	return 0;
}