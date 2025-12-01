#include "gp2.h"
#include "Reader.h"
#include "CompressA.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#ifdef _WIN32
#  include <direct.h>
#  define MKDIR(p, m) _mkdir(p)
#else
#  include <unistd.h>
#  define MKDIR(p, m) mkdir(p, m)
#endif

static std::string basename_only(const char* path) {
    std::string s(path ? path : "");
    for (auto& ch : s) if (ch == '\\') ch = '/';
    auto pos = s.find_last_of('/');
    if (pos != std::string::npos) return s.substr(pos + 1);
    return s;
}

static void ensure_export_dir() {
    struct stat sb{};
    if (stat("export", &sb) != 0) {
        MKDIR("export", 0777);
    }
}

extern "C" {
// Exposed to JS: Process a file within the Emscripten FS and write outputs under /export
void ProcessFile(const char* inputPath) {
    if (!inputPath) {
        std::fprintf(stderr, "ProcessFile: inputPath is null\n");
        return;
    }
    std::printf("Processing: %s\n", inputPath);

    ensure_export_dir();

    // First try GP2 archive route
    if (GP2File* file = GP2File::ReadFile(inputPath)) {
        std::printf("Recognized GP2 archive. Extracted to /export.\n");
        delete file; // extraction already done in ParseFile
        return;
    }

    // Fallback: treat as a single compressed blob with GP2-like header
    FileReader* fr = new FileReader(inputPath);
    if (!fr->IsValid()) {
        std::fprintf(stderr, "Couldn't open %s\n", inputPath);
        delete fr;
        return;
    }
    uint32_t header = fr->ReadUInt32();
    uint8_t* out = nullptr;
    uint32_t decompFileSize = 0;
    if ((header & 0x7) == 0) {
        decompFileSize = header >> 8;
        out = DecompressA(fr, decompFileSize, fr->GetLength());
    } else {
        fr->Seek(0);
        out = GP2File::DecompressSelection(fr, fr->GetLength());
        fr->Seek(0);
        decompFileSize = fr->ReadUInt32() >> 3;
    }
    delete fr;

    if (!out || decompFileSize == 0) {
        std::fprintf(stderr, "Decompression failed or produced no data.\n");
        return;
    }

    ensure_export_dir();
    std::string base = basename_only(inputPath);
    std::string outPath = std::string("export/") + base + ".dcmp";
    if (FILE* fo = std::fopen(outPath.c_str(), "wb")) {
        std::fwrite(out, decompFileSize, 1, fo);
        std::fclose(fo);
        std::printf("Wrote %s (%u bytes)\n", outPath.c_str(), (unsigned)decompFileSize);
    } else {
        std::fprintf(stderr, "Failed to write %s\n", outPath.c_str());
    }
    delete[] out;
}
}

// Provide a dummy entry point for Emscripten builds
int main(int argc, char** argv) {
    // No CLI in browser; all actions are initiated from JS via ProcessFile
    return 0;
}
