#pragma once

#include <fstream>

using namespace std;

class ReadWaveFile
{
public:
	ReadWaveFile(char * filename);
	~ReadWaveFile();

	void readFile();
	int getData(char ** buffer);

private:
	ifstream fin;
	char * buffer;
	int total_data_size_frames;		// Total audio size in frames
	int total_data_size_bytes;		// Total audio size in bytes
	int frame_size;					// One frame size in bytes

};
