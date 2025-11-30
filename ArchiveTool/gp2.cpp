#include "gp2.h"
#include "CompressB.h"
#include "CompressA.h"
#include "CompressC.h"
#include <stdexcept>
#include <stdlib.h>
#include <sys/stat.h>
#include <filesystem>

namespace fs = std::filesystem;

struct FileEntry {
	uint32_t hash;
	uint32_t offs;
	uint32_t size;
};

uint8_t *GP2File::DecompressSelection(FileReader* f, uint32_t fileEnd) {
	uint32_t compressionFlags = f->ReadUInt32();
	uint32_t compressType = compressionFlags & 0x7; // yacker note: 4-7 are unknown
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
	int32_t fileCount = header.packedFileCount & 0xFFF;
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

	bool compressedFiles = false;

	files = new GP2FileStorage * [fileCount];
	for (uint32_t i = 0; i < fileCount; ++i) {
		files[i] = new GP2FileStorage();
		files[i]->name = new char[strlen(fileNamesLinear[i])];
		strcpy(files[i]->name, fileNamesLinear[i]);
		// TODO: factor in compression
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