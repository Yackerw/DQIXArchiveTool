#pragma once
#include <stdint.h>
#include "Reader.h"

class GP2File {
protected:
	struct GP2Header
	{
		uint32_t magic;
		uint16_t packedFileCount;
		uint16_t headerLength;
		uint16_t fileInfoLength;
		uint16_t firstFileOffs;
		uint16_t decompressedFileInfoLength;
		uint16_t decompressedFilenameLength;
		uint32_t totalFileSize;
	};

	struct GP2FileStorage {
		char* name;
		uint8_t* data;
		uint32_t dataLength;
	};

	GP2File() {
		f = NULL;
		files = NULL;
		fileCount = 0;
		header = { 0 };
	}

	struct GP2Header header;

	FileReader* f;
	GP2FileStorage** files;
	uint32_t fileCount;

	bool ParseFile();
	void CreateFromFiles(GP2FileStorage* files, uint32_t fileCount);
public:
	static GP2File *ReadFile(const char *fileName);
	static uint8_t* DecompressSelection(FileReader* f, uint32_t fileEnd);
	static GP2File *CreateFromDirectory(const char* dirName);

	static uint32_t HashKey[256];

	void SaveArchive(const char* fileName);
};