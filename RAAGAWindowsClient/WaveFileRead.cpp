#include <iostream>
#include "WaveFileRead.h"

using namespace std;

ReadWaveFile::ReadWaveFile(char * filename)
{
	fin.open(filename, ios::binary);
	buffer = NULL;
	
}

ReadWaveFile::~ReadWaveFile()
{
	fin.close();
	if (buffer != NULL)
	{
		delete[] buffer;
		buffer = NULL;
	}
}

void ReadWaveFile::readFile()
{
#define DEBUG
	char c[4];

	// check if the file header is intact
	fin.read(c, 4);
	if (!(c[0] == 'R' && c[1] == 'I' && c[2] == 'F' && c[3] == 'F'))
	{
		cout << "RIFF tag not found" << endl;
		exit(1);
	}

	// Read file size
	int32_t filesize = 0;
	fin.read((char *)&filesize, 4);
#ifdef DEBUG
	cout << "File size: " << filesize << endl;
#endif // DEBUG

	// check of the WAVE header
	fin.read(c, 4);
	if (!(c[0] == 'W' && c[1] == 'A' && c[2] == 'V' && c[3] == 'E'))
	{
		cout << "WAVE tag not found" << endl;
		exit(1);
	}

	// Read fmt tag
	fin.read(c, 4);
	if (!(c[0] == 'f' && c[1] == 'm' && c[2] == 't' && c[3] == ' '))
	{
		cout << "fmt tag not found" << endl;
		exit(1);
	}
	
	// read format size
	int32_t fmtSize;
	fin.read((char *)& fmtSize, 4);
#ifdef DEBUG
	cout << "Format size: " << fmtSize << endl;
#endif // DEBUG

	// Read audio format
	int16_t audioFormat;
	fin.read((char *)& audioFormat, 2);
#ifdef DEBUG
	cout << "fmt format: " << audioFormat << endl;
#endif // DEBUG

	// Read number of channels
	int16_t numChannels;
	fin.read((char *)&numChannels, 2);
#ifdef DEBUG
	cout << "Number of channels: " << numChannels << endl;
#endif // DEBUG

	// Read sample rate
	int32_t sampleRate;
	fin.read((char *)&sampleRate, 4);
#ifdef DEBUG
	cout << "Sample rate: " << sampleRate << endl;
#endif // DEBUG

	// Read byte rate / average bytes per second
	int32_t byteRate;
	fin.read((char *)&byteRate, 4);
#ifdef DEBUG
	cout << "Byte rate: " << byteRate << endl;
#endif // DEBUG

	// Read block align / bytes per frame value
	int16_t blockAlign;
	fin.read((char *)&blockAlign, 2);
#ifdef DEBUG
	cout << "Block align: " << blockAlign << endl;
#endif // DEBUG

	// Bits per sample
	int16_t bitsPerSample;
	fin.read((char *)&bitsPerSample, 2);
#ifdef DEBUG
	cout << " Bits per sample: " << bitsPerSample << endl;
#endif // DEBUG

	//Read size of extra bites
	int16_t cbSize;
	fin.read((char *)&cbSize, 2);
#ifdef DEBUG
	cout << "Extra size: " << cbSize << endl;
#endif // DEBUG

	// Read and discard next cbsize of data
	if (cbSize != 0)
	{
		char * temp = new char[cbSize];
		if (temp == NULL)
		{
			cout << "Error allocating memory" << endl;
			exit(1);
		}
		fin.read(temp, cbSize);
		delete[] temp;
		temp = NULL;
	}

	// Check of data tag
	fin.read(c, 4);
	if (!(c[0] == 'd' && c[1] == 'a' && c[2] == 't' && c[3] == 'a'))
	{
		cout << "data tag not found" << endl;
		exit(1);
	}

	// read the data segment size
	int32_t dataSize;
	fin.read((char *)&dataSize, 4);
#ifdef DEBUG
	cout << "Data segment size: " << dataSize << endl;
#endif // DEBUG

	// Read the data
	buffer = new char[dataSize];
	fin.read(buffer, dataSize);
#ifdef DEBUG
	ofstream tfout("file.wav");
	tfout.write(buffer, 20);
	tfout.close();
#endif // DEBUG

	total_data_size_bytes = dataSize;
	frame_size = numChannels * bitsPerSample / 8;
	total_data_size_frames = dataSize / frame_size;
}

int ReadWaveFile::getData(char ** _buffer)
{
	*_buffer = new char[total_data_size_bytes];
	if (*_buffer == NULL)
	{
		perror("Error: allocating memory");
	}
	cout << "Total_data_size_bytes" << total_data_size_bytes << endl;
	memcpy(*_buffer, buffer, total_data_size_bytes);
	return total_data_size_frames;
}
