#include "gp2.h"
#include "CompressB.h"
#include "CompressA.h"
#include "CompressC.h"
#include <stdexcept>
#include <stdlib.h>
#include <sys/stat.h>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

struct FileEntry {
	uint32_t hash;
	uint32_t offs;
	uint32_t size;
};

uint32_t GP2File::HashKey[256];

uint32_t HashFileName(const char* fileName) {
	uint32_t hash = 0xFFFFFFFF;
	for (uint32_t i = 0; fileName[i] != 0; ++i) {
		uint32_t tmp = fileName[i] ^ hash;
		hash = (hash >> 8) ^ (GP2File::HashKey[tmp & 0xFF]);
	}
	return ~hash;
}

uint8_t *GP2File::DecompressSelection(FileReader* f, uint32_t fileEnd) {
	uint32_t compressionFlags = f->ReadUInt32();
	uint32_t compressType = compressionFlags & 0x7; // yacker note: 5-7 are unknown
	uint32_t decompressSize = compressionFlags >> 3;
	uint8_t* retValue;
	switch (compressType) {
	case 0:
		// uncompressed
		retValue = new uint8_t[decompressSize];
		for (uint32_t i = 0; i < decompressSize; ++i) {
			retValue[i] = f->ReadUInt8();
		}
		return retValue;
		break;
	case 1:
		return DecompressA(f, decompressSize, fileEnd);
		break;
	case 2:
	case 3:
		return DecompressB(f, decompressSize, fileEnd, 1 << compressType);
		break;
	case 4:
		return DecompressC(f, decompressSize, fileEnd);
		break;
	default:
		uint32_t debugHelper = f->GetPosition();
		throw new std::exception("Unknown compression type!");
		break;
	}
}


GP2File *GP2File::ReadFile(const char *fileName) {
	GP2File* gp2 = new GP2File();
	gp2->f = new FileReader(fileName);
	if (gp2->f->IsValid() == false) {
		delete gp2;
		return NULL;
	}
	if (!gp2->ParseFile()) {
		delete gp2;
		return NULL;
	}
	return gp2;
}

int __cdecl fileEntrySorter(FileEntry* a, FileEntry* b) {
	return (a->offs & 0xFFFFFF) - (b->offs & 0xFFFFFF);
}

int __cdecl fileEntryHashSorter(FileEntry* a, FileEntry* b) {
	return a->hash > b->hash ? 1 : -1;
}

bool GP2File::ParseFile() {
	header.magic = f->ReadUInt32();
	if (header.magic != 0x32435047) {
		delete f;
		return false;
	}
	header.packedFileCount = f->ReadUInt16();
	header.headerLength = f->ReadUInt16();
	header.fileInfoLength = f->ReadUInt16();
	header.firstFileOffs = f->ReadUInt16();
	header.decompressedFileInfoLength = f->ReadUInt16();
	header.decompressedFilenameLength = f->ReadUInt16(); // doesn't seem useful?
	header.totalFileSize = f->ReadUInt32();

	int32_t maxBinaryTreeIndices = 1 << ((header.packedFileCount & 0xF000) >> 12);
	fileCount = header.packedFileCount & 0xFFF;
	uint32_t firstFileOffs = header.firstFileOffs * 4;
	f->Seek((header.headerLength << 2));
	uint8_t* fileInfoTree = DecompressSelection(f, header.fileInfoLength * 4);
	
	f->Seek(header.fileInfoLength * 4);
	uint8_t* fileNames = DecompressSelection(f, header.firstFileOffs * 4);

	// handled as a binary tree; start with the max indices, only bitshift right 1 if hash for file you want is < file hash stored, add the current value to a separate value also used in the check (ie fileInfo[bitshiftValue+separateValue]) and then bitshift right 1

	struct FileEntry* entries = (FileEntry*)fileInfoTree;

	qsort(entries, fileCount, sizeof(FileEntry), (_CoreCrtNonSecureSearchSortCompareFunction)fileEntrySorter);

	char** fileNamesLinear = new char* [fileCount];
	char* fileIters = (char*)fileNames;
	for (uint32_t i = 0; i < fileCount; ++i) {
		fileNamesLinear[i] = fileIters;
		fileIters += strlen(fileIters) + 1;
	}

	bool compressedFiles = (header.totalFileSize & 0x10000000) == 0;

	files = new GP2FileStorage * [fileCount];
	for (uint32_t i = 0; i < fileCount; ++i) {
		files[i] = new GP2FileStorage();
		files[i]->name = new char[strlen(fileNamesLinear[i]) + 1];
		strcpy(files[i]->name, fileNamesLinear[i]);
		uint32_t fileOffs = ((entries[i].offs & 0xFFFFFF) * 4);
		f->Seek(fileOffs + firstFileOffs);
		if (!compressedFiles) { // leaving here, but there doesn't seem to be a real indicator for file compression, you just gotta look :( (thankfully archives seem consistent about whether they use it or not for files)
			files[i]->data = new uint8_t[entries[i].size & 0xFFFFFF];
			files[i]->dataLength = entries[i].size & 0xFFFFFF;
			for (uint32_t j = 0; j < files[i]->dataLength; ++j) {
				files[i]->data[j] = f->ReadUInt8();
			}
		}
		else {
			files[i]->dataLength = f->ReadUInt32() >> 3;
			f->Seek(fileOffs + firstFileOffs);
			files[i]->data = DecompressSelection(f, fileOffs + firstFileOffs + (entries[i].size & 0xFFFFFF));
		}
	}

	delete[] fileNames;
	delete[] fileInfoTree;
	delete[] fileNamesLinear;
	delete f;
	f = NULL;

	struct stat sb;

	if (stat("export", &sb) != 0) {
		fs::create_directory("export");
	}

	for (uint32_t i = 0; i < fileCount; ++i) {
		char fileNames[256];
		sprintf(fileNames, "export/%s", files[i]->name);
		FILE* f = fopen(fileNames, "wb");
		fwrite(files[i]->data, 1, files[i]->dataLength, f);
		fclose(f);
	}

	return true;
}

GP2File* GP2File::CreateFromDirectory(const char* dirName) {
	std::vector<GP2FileStorage> files;
	for (const auto& entry : fs::directory_iterator(dirName)) {
		GP2FileStorage newFile;
		std::string nameStr = entry.path().filename().generic_u8string();
		const char* name = nameStr.c_str();
		newFile.name = new char[strlen(name)+1];
		strcpy(newFile.name, name);

		std::string dirStr = entry.path().generic_u8string();

		FILE* f = fopen(dirStr.c_str(), "rb");
		fseek(f, 0, SEEK_END);
		newFile.dataLength = ftell(f);
		fseek(f, 0, SEEK_SET);
		newFile.data = new uint8_t[newFile.dataLength];
		fread(newFile.data, 1, newFile.dataLength, f);
		fclose(f);

		files.push_back(newFile);
	}

	GP2File* ret = new GP2File();
	ret->CreateFromFiles(&files[0], files.size());

	return ret;
}

void GP2File::CreateFromFiles(GP2FileStorage* files, uint32_t fileCount) {
	this->fileCount = fileCount;
	this->files = new GP2FileStorage * [fileCount];
	for (uint32_t i = 0; i < fileCount; ++i) {
		GP2FileStorage* newFile = new GP2FileStorage();
		newFile->data = files[i].data;
		newFile->name = files[i].name;
		newFile->dataLength = files[i].dataLength;
		this->files[i] = newFile;
	}
}

void GP2File::SaveArchive(const char* fileName) {
	// error out early if the file isn't available
	FILE* f = fopen(fileName, "wb");
	if (f == NULL) {
		// well, that sucks
		printf("Failed to open output file!");
		return;
	}

	std::vector<uint8_t> fileData;
	std::vector<uint32_t> fileOffsets;
	for (uint32_t i = 0; i < fileCount; ++i) {
		fileOffsets.push_back(fileData.size());
		for (uint32_t j = 0; j < files[i]->dataLength; ++j) {
			fileData.push_back(files[i]->data[j]);
		}
		while (fileData.size() % 16 != 0) {
			fileData.push_back(0);
		}
	}

	std::vector<FileEntry> fileEntries;
	// hash the file names
	for (uint32_t i = 0; i < fileCount; ++i) {
		FileEntry newEntry;
		newEntry.offs = ((fileOffsets[i] >> 2) & 0xFFFFFF) | ((i & 0xFF) << 24);
		newEntry.size = ((files[i]->dataLength) & 0xFFFFFF) | ((i & 0xFF00) << 16);
		newEntry.hash = HashFileName(files[i]->name);

		fileEntries.push_back(newEntry);
	}

	qsort(&fileEntries[0], fileCount, sizeof(FileEntry), (_CoreCrtNonSecureSearchSortCompareFunction)fileEntryHashSorter);

	uint32_t treeDepth = fileCount;
	uint32_t treeShift = 0;
	while (treeDepth != 0) {
		treeDepth = fileCount >> treeShift;
		++treeShift;
	}
	--treeShift;
	header.packedFileCount = ((treeShift & 0xF) << 12) | (fileCount & 0xFFF);
	header.magic = 0x32435047;
	header.headerLength = 0x5;

	// compress header info and file names
	uint32_t fileInfoLength;
	//uint8_t *fileInfo = CompressA((uint8_t*)&fileEntries[0], fileEntries.size() * sizeof(FileEntry), &fileInfoLength);
	fileInfoLength = fileEntries.size() * sizeof(FileEntry);
	uint8_t* fileInfo = new uint8_t[fileInfoLength];
	memcpy(fileInfo, &fileEntries[0], fileInfoLength);

	header.fileInfoLength = ((fileInfoLength + 7) >> 2) + header.headerLength;
	
	std::vector<char> flatNames;
	for (uint32_t i = 0; i < fileCount; ++i) {
		uint32_t j = 0;
		do {
			flatNames.push_back(files[i]->name[j]);
		} while (files[i]->name[j++] != 0);
	}

	uint32_t fileNameLength;
	uint8_t* fileNamesCompress = CompressA((uint8_t*)&flatNames[0], flatNames.size(), &fileNameLength);

	header.firstFileOffs = ((((fileNameLength + (header.fileInfoLength << 2) + 19) / 16 ) * 16) >> 2);

	header.decompressedFileInfoLength = (fileEntries.size() * sizeof(FileEntry) + 3) >> 2;
	header.decompressedFilenameLength = (flatNames.size() + 3) >> 2;

	header.totalFileSize = ((fileData.size() + 3) >> 2) | 0x10000000;

	// all the data *should* be good to write now
	fwrite(&header, sizeof(header), 1, f);
	
	uint32_t compressedDataHeader = 0x0 | ((fileEntries.size() * sizeof(FileEntry)) << 3);
	fwrite(&compressedDataHeader, 4, 1, f);
	fwrite(fileInfo, fileInfoLength, 1, f);
	while ((ftell(f) % 4) != 0) {
		uint8_t zero = 0;
		fwrite(&zero, 1, 1, f);
	}
	
	compressedDataHeader = 0x1 | ((flatNames.size()) << 3);
	fwrite(&compressedDataHeader, 4, 1, f);
	fwrite(fileNamesCompress, fileNameLength, 1, f);
	while ((ftell(f) % 16) != 0) {
		uint8_t zero = 0;
		fwrite(&zero, 1, 1, f);
	}
	fwrite(&fileData[0], fileData.size(), 1, f);

	fclose(f);
	delete[] fileNamesCompress;
	delete[] fileInfo;
	// winner
}