#include <WinSock2.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "ws2_32.lib")

int main()
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	const char* IP = "127.0.0.1";
	int port = 6000;
	struct sockaddr_in server;
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	server.sin_addr.S_un.S_addr = inet_addr(IP);

	SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	int ret = connect(client, (struct sockaddr*)&server, sizeof(server));
	if (ret < 0)
	{
		printf("connect %s:%d failed\n", IP, port);
		return 1;
	}

	char buffer[1024];
	int idx = 0;
	while (1)
	{
		int n = sprintf(buffer, "No:%d", ++idx);
		send(client, buffer, n, 0);
		memset(buffer, 0, sizeof(buffer));
		int rev = recv(client, buffer, sizeof(buffer), 0);
		if (rev == 0)
		{
			printf("Recv Nothing\n");
		}
		else
		{
			printf("Recv: %s\n", buffer);
		}
		Sleep(5000);
	}
	closesocket(client);
	WSACleanup();
	return 0;
}