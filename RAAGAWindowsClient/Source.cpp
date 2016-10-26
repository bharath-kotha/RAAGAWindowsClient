#pragma comment(lib,"ws2_32")

#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <iostream>
#include <WinSock2.h>
#include <fstream>
#include "WaveFileRead.h"

using namespace std;

int main()
{
#define DEBUG
	char * filename = "sample1.WAV";
	char * buffer = NULL;

	WSAData wsaData;
	WORD DllVersion = MAKEWORD(2, 1);

	cout << "*******CLIENT********" << endl;

	if (WSAStartup(DllVersion, &wsaData) != 0)		// If WSAStartup returns anything other than 0, then that means an error has occured
	{
		printf("Error occured in initializing WSA");
		exit(1);
	}

	SOCKADDR_IN addr;				// Address that we will bind our listening socket to
	int addrlen = sizeof(addr);	// length of the address (required for accept call
	addr.sin_addr.s_addr = inet_addr("192.168.0.108");		// Address = localhost (this pc)
	addr.sin_port = htons(1111);		// Port
	addr.sin_family = AF_INET;		// IPv4 socket

	SOCKET conn = socket(AF_INET, SOCK_STREAM, NULL);
	if (connect(conn, (SOCKADDR *)&addr, addrlen) != 0)
	{
		cout << "Failed to connect to server" << endl;
		perror("Failed to connect to server: ");
		system("pause");
		exit(1);
	}


	ReadWaveFile file(filename);
	file.readFile();
	int frames = file.getData(&buffer);

	cout << "Connected" << endl;
	//recv(conn, MOTD, sizeof(MOTD), NULL);			// Receive message of the day buffer into MOTD array
	char sendBuffer[4800];
	cout << sizeof(buffer) << endl;
	cout << "total_data_size_frames" << frames << endl;

	for (int i = 0; i < (frames/600); i++)
	{
		memcpy(sendBuffer, buffer + (i * sizeof(sendBuffer)), sizeof(sendBuffer));
		send(conn, sendBuffer, sizeof(sendBuffer), NULL);
	}



	system("pause");
}