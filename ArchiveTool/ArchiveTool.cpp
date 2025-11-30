#include "gp2.h"
#include "CompressA.h"
#include <sys/stat.h>

#define EXEC_MODE 1

void decomp_mode(int argc, char** argv)
{
    if (argc <= 1) {
        printf("Drag a file to decompress onto exe!");
        return;
    }
    GP2File *file = GP2File::ReadFile(argv[1]);
    if (file == NULL) {
        FileReader* f = new FileReader(argv[1]);
        if (f->IsValid() == false) {
            delete f;
            printf("Couldn't open file!");
            return;
        }
        // try to detect if it uses the standard GP2 compression header
        uint32_t header = f->ReadUInt32();
        uint8_t* out;
        uint32_t decompFileSize;
        if ((header & 0x7) == 0) {
            // assume CompressA! this seems to be the default for things like monsters
            decompFileSize = header >> 8;
            out = DecompressA(f, decompFileSize, f->GetLength());
        }
        else {
            f->Seek(0);
            out = GP2File::DecompressSelection(f, f->GetLength());
            f->Seek(0);
            decompFileSize = f->ReadUInt32() >> 3;
        }
        delete f;
        char outputFileName[512];
        sprintf(outputFileName, "%s.dcmp", argv[1]);
        FILE* fi = fopen(outputFileName, "wb");
        if (fi == NULL) {
            printf("Couldn't open output file!");
            delete[] out;
            return;
        }
        fwrite(out, decompFileSize, 1, fi);
        fclose(fi);
        delete[] out;
    }
    else {
        delete file;
    }
}

void comp_mode(int argc, char** argv) {
    if (argc < 1) {
        printf("Drag a folder or file onto the exe!");
        return;
    }

    struct stat sb;
    if (stat(argv[1], &sb) != 0) {
        printf("Couldn't find input file or folder?");
        return;
    }
    if (sb.st_mode & S_IFDIR) {
        FILE* hashKey = fopen("hashkey.bin", "rb");
        if (hashKey == NULL) {
            printf("Hashkey file not found!");
            return;
        }
        fread(GP2File::HashKey, sizeof(uint32_t) * 256, 1, hashKey);
        fclose(hashKey);
        GP2File* file = GP2File::CreateFromDirectory(argv[1]);
        char outFileName[512];
        sprintf(outFileName, "%s.gp2", argv[1]);
        file->SaveArchive(outFileName);
    }
    else {
        FILE* f = fopen(argv[1], "rb");
        fseek(f, 0, SEEK_END);
        uint32_t fileLen = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t* uncompressedFile = new uint8_t[fileLen];
        fread(uncompressedFile, fileLen, 1, f);
        fclose(f);
        uint32_t compressedLen;
        uint8_t *compressedFile = CompressA(uncompressedFile, fileLen, &compressedLen);

        uint32_t compressedHeader = (fileLen << 3) | 1;

        delete[] uncompressedFile;

        char outFileName[512];
        sprintf(outFileName, "%s.cmp", argv[1]);
        f = fopen(outFileName, "wb");
        fwrite(&compressedHeader, 4, 1, f);
        fwrite(compressedFile, compressedLen, 1, f);
        fclose(f);
        delete[] compressedFile;
    }
}

int main(int argc, char** argv) {
#if EXEC_MODE == 0
    decomp_mode(argc, argv);
#elif EXEC_MODE == 1
    comp_mode(argc, argv);
#elif EXEC_MODE == 2
    comp_mon_mode(argc, argv);
#endif

    return 0;
}