#include "gp2.h"
#include "CompressB.h"
#include "CompressA.h"
#include "CompressC.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <string>
#ifdef _WIN32
#  include <direct.h>
#  define MKDIR(p, m) _mkdir(p)
#else
#  include <unistd.h>
#  define MKDIR(p, m) mkdir(p, m)
#endif

namespace {
    static void make_dirs(const std::string& path) {
        if (path.empty()) return;
        std::string norm = path;
        for (auto& ch : norm) if (ch == '\\') ch = '/';
        std::string accum;
        size_t start = 0;
        if (!norm.empty() && norm[0] == '/') {
            accum = "/";
            start = 1;
        }
        for (size_t i = start; i <= norm.size(); ++i) {
            if (i == norm.size() || norm[i] == '/') {
                std::string part = norm.substr(start, i - start);
                if (!part.empty()) {
                    if (!accum.empty() && accum.back() != '/') accum.push_back('/');
                    accum += part;
                    #ifdef _WIN32
                    _mkdir(accum.c_str());
                    #else
                    mkdir(accum.c_str(), 0777);
                    #endif
                }
                start = i + 1;
            }
        }
    }
}

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
        {
            uint32_t debugHelper = f->GetPosition();
            (void)debugHelper;
            std::fprintf(stderr, "Unknown compression type: %u\n", compressType);
            return nullptr;
        }
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

static int fileEntrySorter(const void* pa, const void* pb) {
    auto a = reinterpret_cast<const FileEntry*>(pa);
    auto b = reinterpret_cast<const FileEntry*>(pb);
    uint32_t ao = (a->offs & 0xFFFFFF);
    uint32_t bo = (b->offs & 0xFFFFFF);
    if (ao < bo) return -1;
    if (ao > bo) return 1;
    return 0;
}

static inline std::string SanitizePath(const char* name) {
    std::string s(name ? name : "");
    for (auto& ch : s) {
        if (ch == '\\') ch = '/';
    }
    // Prevent absolute paths and parent traversal for safety in MEMFS
    if (!s.empty() && s[0] == '/') s.erase(s.begin());
    while (s.find("../") != std::string::npos) {
        s.replace(s.find("../"), 3, "");
    }
    return s;
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

 qsort(entries, fileCount, sizeof(FileEntry), fileEntrySorter);

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
     // allocate +1 for null terminator
     size_t nameLen = std::strlen(fileNamesLinear[i]);
     files[i]->name = new char[nameLen + 1];
     std::strcpy(files[i]->name, fileNamesLinear[i]);
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
     MKDIR("export", 0777);
 }

    for (uint32_t i = 0; i < fileCount; ++i) {
        // sanitize and ensure directories
        std::string rel = SanitizePath(files[i]->name);
        std::string full = std::string("export/") + rel;
        std::printf("export/%s\n", files[i]->name);
        // ensure parent directories exist
        size_t pos = full.find_last_of('/');
        if (pos != std::string::npos) {
            make_dirs(full.substr(0, pos));
        }
        FILE* of = std::fopen(full.c_str(), "wb");
        if (of) {
            std::fwrite(files[i]->data, 1, files[i]->dataLength, of);
            std::fclose(of);
        }
    }

    return true;
}