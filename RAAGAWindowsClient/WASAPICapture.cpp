// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved
//

//
// WASAPICaptureSharedTimerDriven.cpp : Scaffolding associated with the WASAPI Capture Shared Timer Driven sample application.
//
//  This application captures data from the specified input device and writes it to a uniquely named .WAV file in the current directory.
//

#pragma comment(lib,"ws2_32")

#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <WinSock2.h>
#include "WaveFileRead.h"

#include "stdafx.h"

#include <functiondiscoverykeys.h>
#include "WASAPI.h"
#include <chrono>
#include <thread>
#include <iostream>

#include "CmdLine.h"


int TargetLatency = 20;
int TargetDurationInSec = 1;
bool ShowHelp;
bool UseConsoleDevice;
bool UseCommunicationsDevice;
bool UseMultimediaDevice;
bool DisableMMCSS;

wchar_t *OutputEndpoint;

CWASAPICapture *capturer;
BYTE *captureBuffer;
BYTE *captureBuffer1;

CommandLineSwitch CmdLineArgs[] =
{
	{ L"?", L"Print this help", CommandLineSwitch::SwitchTypeNone, reinterpret_cast<void **>(&ShowHelp) },
	{ L"h", L"Print this help", CommandLineSwitch::SwitchTypeNone, reinterpret_cast<void **>(&ShowHelp) },
	{ L"l", L"Audio Capture Latency (ms)", CommandLineSwitch::SwitchTypeInteger, reinterpret_cast<void **>(&TargetLatency), false },
	{ L"d", L"Audio Capture Duration (s)", CommandLineSwitch::SwitchTypeInteger, reinterpret_cast<void **>(&TargetDurationInSec), false },
	{ L"m", L"Disable the use of MMCSS", CommandLineSwitch::SwitchTypeNone, reinterpret_cast<void **>(&DisableMMCSS) },
	{ L"console", L"Use the default console device", CommandLineSwitch::SwitchTypeNone, reinterpret_cast<void **>(&UseConsoleDevice) },
	{ L"communications", L"Use the default communications device", CommandLineSwitch::SwitchTypeNone, reinterpret_cast<void **>(&UseCommunicationsDevice) },
	{ L"multimedia", L"Use the default multimedia device", CommandLineSwitch::SwitchTypeNone, reinterpret_cast<void **>(&UseMultimediaDevice) },
	{ L"endpoint", L"Use the specified endpoint ID", CommandLineSwitch::SwitchTypeString, reinterpret_cast<void **>(&OutputEndpoint), true },
};

size_t CmdLineArgLength = ARRAYSIZE(CmdLineArgs);

//
//  Print help for the sample
//
void Help(LPCWSTR ProgramName)
{
	printf("Usage: %S [-/][Switch][:][Value]\n\n", ProgramName);
	printf("Where Switch is one of the following: \n");
	for (size_t i = 0; i < CmdLineArgLength; i += 1)
	{
		printf("    -%S: %S\n", CmdLineArgs[i].SwitchName, CmdLineArgs[i].SwitchHelp);
	}
}

//
//  Retrieves the device friendly name for a particular device in a device collection.  
//
//  The returned string was allocated using malloc() so it should be freed using free();
//
LPWSTR GetDeviceName(IMMDeviceCollection *DeviceCollection, UINT DeviceIndex)
{
	IMMDevice *device;
	LPWSTR deviceId;
	HRESULT hr;

	hr = DeviceCollection->Item(DeviceIndex, &device);
	if (FAILED(hr))
	{
		printf("Unable to get device %d: %x\n", DeviceIndex, hr);
		return NULL;
	}
	hr = device->GetId(&deviceId);
	if (FAILED(hr))
	{
		printf("Unable to get device %d id: %x\n", DeviceIndex, hr);
		return NULL;
	}

	IPropertyStore *propertyStore;
	hr = device->OpenPropertyStore(STGM_READ, &propertyStore);
	SafeRelease(&device);
	if (FAILED(hr))
	{
		printf("Unable to open device %d property store: %x\n", DeviceIndex, hr);
		return NULL;
	}

	PROPVARIANT friendlyName;
	PropVariantInit(&friendlyName);
	hr = propertyStore->GetValue(PKEY_Device_FriendlyName, &friendlyName);
	SafeRelease(&propertyStore);

	if (FAILED(hr))
	{
		printf("Unable to retrieve friendly name for device %d : %x\n", DeviceIndex, hr);
		return NULL;
	}

	wchar_t deviceName[128];
	hr = StringCbPrintf(deviceName, sizeof(deviceName), L"%s (%s)", friendlyName.vt != VT_LPWSTR ? L"Unknown" : friendlyName.pwszVal, deviceId);
	if (FAILED(hr))
	{
		printf("Unable to format friendly name for device %d : %x\n", DeviceIndex, hr);
		return NULL;
	}

	PropVariantClear(&friendlyName);
	CoTaskMemFree(deviceId);

	wchar_t *returnValue = _wcsdup(deviceName);
	if (returnValue == NULL)
	{
		printf("Unable to allocate buffer for return\n");
		return NULL;
	}
	return returnValue;
}
//
//  Based on the input switches, pick the specified device to use.
//
bool PickDevice(IMMDevice **DeviceToUse, bool *IsDefaultDevice, ERole *DefaultDeviceRole)
{
	HRESULT hr;
	bool retValue = true;
	IMMDeviceEnumerator *deviceEnumerator = NULL;
	IMMDeviceCollection *deviceCollection = NULL;

	*IsDefaultDevice = false;   // Assume we're not using the default device.

	hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&deviceEnumerator));
	if (FAILED(hr))
	{
		printf("Unable to instantiate device enumerator: %x\n", hr);
		retValue = false;
		goto Exit;
	}

	IMMDevice *device = NULL;

	//
	//  First off, if none of the console switches was specified, use the console device.
	//
	if (!UseConsoleDevice && !UseCommunicationsDevice && !UseMultimediaDevice && OutputEndpoint == NULL)
	{
		//
		//  The user didn't specify an output device, prompt the user for a device and use that.
		//
		hr = deviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &deviceCollection);
		if (FAILED(hr))
		{
			printf("Unable to retrieve device collection: %x\n", hr);
			retValue = false;
			goto Exit;
		}

		printf("Select an output device:\n");
		printf("    0:  Default Console Device\n");
		printf("    1:  Default Communications Device\n");
		printf("    2:  Default Multimedia Device\n");
		UINT deviceCount;
		hr = deviceCollection->GetCount(&deviceCount);
		if (FAILED(hr))
		{
			printf("Unable to get device collection length: %x\n", hr);
			retValue = false;
			goto Exit;
		}
		for (UINT i = 0; i < deviceCount; i += 1)
		{
			LPWSTR deviceName;

			deviceName = GetDeviceName(deviceCollection, i);
			if (deviceName == NULL)
			{
				retValue = false;
				goto Exit;
			}
			printf("    %d:  %S\n", i + 3, deviceName);
			free(deviceName);
		}
		wchar_t choice[10];
		_getws_s(choice);   // Note: Using the safe CRT version of _getws.

		long deviceIndex;
		wchar_t *endPointer;

		deviceIndex = wcstoul(choice, &endPointer, 0);
		if (deviceIndex == 0 && endPointer == choice)
		{
			printf("unrecognized device index: %S\n", choice);
			retValue = false;
			goto Exit;
		}
		switch (deviceIndex)
		{
		case 0:
			UseConsoleDevice = 1;
			break;
		case 1:
			UseCommunicationsDevice = 1;
			break;
		case 2:
			UseMultimediaDevice = 1;
			break;
		default:
			hr = deviceCollection->Item(deviceIndex - 3, &device);
			if (FAILED(hr))
			{
				printf("Unable to retrieve device %d: %x\n", deviceIndex - 3, hr);
				retValue = false;
				goto Exit;
			}
			break;
		}
	}
	else if (OutputEndpoint != NULL)
	{
		hr = deviceEnumerator->GetDevice(OutputEndpoint, &device);
		if (FAILED(hr))
		{
			printf("Unable to get endpoint for endpoint %S: %x\n", OutputEndpoint, hr);
			retValue = false;
			goto Exit;
		}
	}

	if (device == NULL)
	{
		ERole deviceRole = eConsole;    // Assume we're using the console role.
		if (UseConsoleDevice)
		{
			deviceRole = eConsole;
		}
		else if (UseCommunicationsDevice)
		{
			deviceRole = eCommunications;
		}
		else if (UseMultimediaDevice)
		{
			deviceRole = eMultimedia;
		}
		hr = deviceEnumerator->GetDefaultAudioEndpoint(eCapture, deviceRole, &device);
		if (FAILED(hr))
		{
			printf("Unable to get default device for role %d: %x\n", deviceRole, hr);
			retValue = false;
			goto Exit;
		}
		*IsDefaultDevice = true;
		*DefaultDeviceRole = deviceRole;
	}

	*DeviceToUse = device;
	retValue = true;
Exit:
	SafeRelease(&deviceCollection);
	SafeRelease(&deviceEnumerator);

	return retValue;
}

//
//  WAV file writer.
//
//  This is a VERY simple .WAV file writer.
//

//
//  A wave file consists of:
//
//  RIFF header:    8 bytes consisting of the signature "RIFF" followed by a 4 byte file length.
//  WAVE header:    4 bytes consisting of the signature "WAVE".
//  fmt header:     4 bytes consisting of the signature "fmt " followed by a WAVEFORMATEX 
//  WAVEFORMAT:     <n> bytes containing a waveformat structure.
//  DATA header:    8 bytes consisting of the signature "data" followed by a 4 byte file length.
//  wave data:      <m> bytes containing wave data.
//
//
//  Header for a WAV file - we define a structure describing the first few fields in the header for convenience.
//
struct WAVEHEADER
{
	DWORD   dwRiff;                     // "RIFF"
	DWORD   dwSize;                     // Size
	DWORD   dwWave;                     // "WAVE"
	DWORD   dwFmt;                      // "fmt "
	DWORD   dwFmtSize;                  // Wave Format Size
};

//  Static RIFF header, we'll append the format to it.
const BYTE WaveHeader[] =
{
	'R',   'I',   'F',   'F',  0x00,  0x00,  0x00,  0x00, 'W',   'A',   'V',   'E',   'f',   'm',   't',   ' ', 0x00, 0x00, 0x00, 0x00
};

//  Static wave DATA tag.
const BYTE WaveData[] = { 'd', 'a', 't', 'a' };

//
//  Write the contents of a WAV file.  We take as input the data to write and the format of that data.
//
bool WriteWaveFile(HANDLE FileHandle, const BYTE *Buffer, const size_t BufferSize, const WAVEFORMATEX *WaveFormat)
{
	DWORD waveFileSize = sizeof(WAVEHEADER) + sizeof(WAVEFORMATEX) + WaveFormat->cbSize + sizeof(WaveData) + sizeof(DWORD) + static_cast<DWORD>(BufferSize);
	BYTE *waveFileData = new (std::nothrow) BYTE[waveFileSize];
	BYTE *waveFilePointer = waveFileData;
	WAVEHEADER *waveHeader = reinterpret_cast<WAVEHEADER *>(waveFileData);

	if (waveFileData == NULL)
	{
		printf("Unable to allocate %d bytes to hold output wave data\n", waveFileSize);
		return false;
	}

	//
	//  Copy in the wave header - we'll fix up the lengths later.
	//
	CopyMemory(waveFilePointer, WaveHeader, sizeof(WaveHeader));
	waveFilePointer += sizeof(WaveHeader);

	//
	//  Update the sizes in the header.
	//
	waveHeader->dwSize = waveFileSize - (2 * sizeof(DWORD));
	waveHeader->dwFmtSize = sizeof(WAVEFORMATEX) + WaveFormat->cbSize;

	//
	//  Next copy in the WaveFormatex structure.
	//
	CopyMemory(waveFilePointer, WaveFormat, sizeof(WAVEFORMATEX) + WaveFormat->cbSize);
	waveFilePointer += sizeof(WAVEFORMATEX) + WaveFormat->cbSize;


	//
	//  Then the data header.
	//
	CopyMemory(waveFilePointer, WaveData, sizeof(WaveData));
	waveFilePointer += sizeof(WaveData);
	*(reinterpret_cast<DWORD *>(waveFilePointer)) = static_cast<DWORD>(BufferSize);
	waveFilePointer += sizeof(DWORD);

	//
	//  And finally copy in the audio data.
	//
	CopyMemory(waveFilePointer, Buffer, BufferSize);

	//
	//  Last but not least, write the data to the file.
	//
	DWORD bytesWritten;
	if (!WriteFile(FileHandle, waveFileData, waveFileSize, &bytesWritten, NULL))
	{
		printf("Unable to write wave file: %d\n", GetLastError());
		delete[]waveFileData;
		return false;
	}

	if (bytesWritten != waveFileSize)
	{
		printf("Failed to write entire wave file\n");
		delete[]waveFileData;
		return false;
	}
	delete[]waveFileData;
	return true;
}

//
//  Write the captured wave data to an output file so that it can be examined later.
//
void SaveWaveData(BYTE *CaptureBuffer, size_t BufferSize, const WAVEFORMATEX *WaveFormat)
{
	wchar_t waveFileName[MAX_PATH];
	HRESULT hr = StringCbCopy(waveFileName, sizeof(waveFileName), L"WASAPICaptureTimerDriven-");
	if (SUCCEEDED(hr))
	{
		GUID testGuid;
		if (SUCCEEDED(CoCreateGuid(&testGuid)))
		{
			wchar_t *guidString;
			if (SUCCEEDED(StringFromCLSID(testGuid, &guidString)))
			{
				hr = StringCbCat(waveFileName, sizeof(waveFileName), guidString);
				if (SUCCEEDED(hr))
				{
					hr = StringCbCat(waveFileName, sizeof(waveFileName), L".WAV");
					if (SUCCEEDED(hr))
					{
						HANDLE waveHandle = CreateFile(waveFileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
							FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
							NULL);
						if (waveHandle != INVALID_HANDLE_VALUE)
						{
							if (WriteWaveFile(waveHandle, CaptureBuffer, BufferSize, WaveFormat))
							{
								printf("Successfully wrote WAVE data to %S\n", waveFileName);
							}
							else
							{
								printf("Unable to write wave file\n");
							}
							CloseHandle(waveHandle);
						}
						else
						{
							printf("Unable to open output WAV file %S: %d\n", waveFileName, GetLastError());
						}
					}
				}
				CoTaskMemFree(guidString);
			}
		}
	}
}

// Not used in the present code
void writeFileThread(BYTE ** cbuffer, size_t bytesCaptured, WAVEFORMATEX * mixFormat)
{
	SaveWaveData(*cbuffer, bytesCaptured, mixFormat);
}


char sendBuffer[4800];		// Buffer to send data to server
SOCKET conn;				// Socket holding the server address

/*
Function Name: sendDataToServer
Input: 
		connection is the SOCKET address where data to be sent, sBuffer is the pointer to buffer holding data
		and len the size of the data to be written
Output: 
		Writes data to server
Logic:
		Uses send command of WINSOCK API
Example Call:
		sendDATATOServer
*/
void sendDataToServer(SOCKET * connection, char ** sBuffer, int len)
{
	send(*connection, *sBuffer, len, NULL);
}

/*
Function Name: wmain
Input:
		argc is the command line arguments length, argv is pointer to command line argument strings
Output:
		Parses the command line data, intialized sockets, connects to server, gets audio buffer data, sends it to server repeatedly
Logic:
		Uses various WINDOWS and WINSOCK APIs and other helper functions
Example Call:
		Not callable
*/
int wmain(int argc, wchar_t* argv[])
{
	int result = 0;
	IMMDevice *device = NULL;
	bool isDefaultDevice;
	ERole role;

	char * filename = "sample1.WAV";
	char * buffer = NULL;

	WSAData wsaData;
	WORD DllVersion = MAKEWORD(2, 1);

	if (WSAStartup(DllVersion, &wsaData) != 0)		// If WSAStartup returns anything other than 0, then that means an error has occured
	{
		printf("Error occured in initializing WSA");
		exit(1);
	}

	SOCKADDR_IN addr;				// Address that we will bind our listening socket to
	int addrlen = sizeof(addr);	// length of the address (required for accept call
	addr.sin_addr.s_addr = inet_addr("192.168.0.103");		// Address = localhost (this pc)
	addr.sin_port = htons(1111);		// Port
	addr.sin_family = AF_INET;		// IPv4 socket

	conn = socket(AF_INET, SOCK_STREAM, NULL);
	if (connect(conn, (SOCKADDR *)&addr, addrlen) != 0)
	{
		cout << "Failed to connect to server" << endl;
		perror("Failed to connect to server: ");
		system("pause");
		exit(1);
	}
	

	//ReadWaveFile file(filename);
	//file.readFile();
	//int frames = file.getData(&buffer);

	cout << "Connected" << endl;
	//recv(conn, MOTD, sizeof(MOTD), NULL);			// Receive message of the day buffer into MOTD array
	//cout << sizeof(buffer) << endl;
	//cout << "total_data_size_frames" << frames << endl;

	//for (int i = 0; i < (frames / 600); i++)
	{
		//memcpy(sendBuffer, buffer + (i * sizeof(sendBuffer)), sizeof(sendBuffer));
		//send(conn, sendBuffer, sizeof(sendBuffer), NULL);
	}
	printf("WASAPI Capture Shared Timer Driven Sample\n");
	printf("Copyright (c) Microsoft.  All Rights Reserved\n");
	printf("\n");


	//
	//  A GUI application should use COINIT_APARTMENTTHREADED instead of COINIT_MULTITHREADED.
	//
	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr))
	{
		printf("Unable to initialize COM: %x\n", hr);
		result = hr;
		goto Exit;
	}

	//
	//  Now that we've parsed our command line, pick the device to capture.
	//
	if (!PickDevice(&device, &isDefaultDevice, &role))
	{
		result = -1;
		goto Exit;
	}

	printf("Capture audio data for %d seconds\n", TargetDurationInSec);

	//
	//  Instantiate a capturer and capture sounds for TargetDuration seconds
	//
	//  Configure the capturer to enable stream switching on the specified role if the user specified one of the default devices.
	//
	{
		capturer = new (std::nothrow) CWASAPICapture(device, false, role);
		if (capturer == NULL)
		{
			printf("Unable to allocate capturer\n");
			return -1;
		}

		if (capturer->Initialize(TargetLatency))
		{
			//
			//  We've initialized the capturer.  Once we've done that, we know some information about the
			//  mix format and we can allocate the buffer that we're going to capture.
			//
			//
			//  The buffer is going to contain "TargetDuration" seconds worth of PCM data.  That means 
			//  we're going to have TargetDuration*samples/second frames multiplied by the frame size.
			//
			size_t captureBufferSize = (int)capturer->SamplesPerSecond() * 0.1 * capturer->FrameSize();		// Capture data for 1 second
			captureBuffer = new (std::nothrow) BYTE[captureBufferSize];			// Buffer to store received data
			captureBuffer1 = new (std::nothrow) BYTE[captureBufferSize];

			if (captureBuffer == NULL)
			{
				printf("Unable to allocate capture buffer\n");
				return -1;
			}

			// Start filling the audio buffer
			if (capturer->Begin())
			{
				// Start capturing the data by swapping two buffers, Whenever one buffer fills up, swap it with the other,
				// create a thread and send the data to server
				for (int i = 0; i < 999; i++)
				{
					if (i % 2 == 0) {
						if (capturer->Start(captureBuffer, captureBufferSize))
						{
							if (i > 0)
							{
								//std::thread t1(writeFileThread, &captureBuffer1, capturer->BytesCaptured(), capturer->MixFormat());
								//void sendDataToServer(SOCKET * connection, char ** sBuffer, int len)
								std::thread t1(sendDataToServer, &conn, (char **)&captureBuffer1,(int) capturer->BytesCaptured());
								t1.join();
							}
							do
							{
								//printf(".");
								Sleep(1);
							} while (!capturer->hasCaptured());
							printf("\n");
							capturer->Stop();
						}

					}
					else
					{
						if (capturer->Start(captureBuffer1, captureBufferSize))
						{
							if (i > 0)
							{
								//std::thread t1(writeFileThread, &captureBuffer, capturer->BytesCaptured(), capturer->MixFormat());
								std::thread t1(sendDataToServer,&conn, (char **)&captureBuffer,(int) capturer->BytesCaptured());
								t1.join();
							}
							do
							{
								//printf(".");
								Sleep(1);
							} while (!capturer->hasCaptured());
							printf("\n");
							capturer->Stop();
						}
					}
				}
				//std::thread t1(writeFileThread, &captureBuffer, capturer->BytesCaptured(), capturer->MixFormat());
				std::thread t1(sendDataToServer, &conn, (char **)&captureBuffer,(int) capturer->BytesCaptured());
				t1.join();

				//
				//  Now shut down the capturer and release it we're done.
				//
				capturer->Destroy();
				capturer->Shutdown();
				SafeRelease(&capturer);
			}

			delete[]captureBuffer;
		}
	}

Exit:
	SafeRelease(&device);
	CoUninitialize();
	system("pause");
	return 0;
}
