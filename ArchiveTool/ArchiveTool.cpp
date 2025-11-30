#include "gp2.h"
#include "CompressA.h"

#define EXEC_MODE 0

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