#include "CompressA.h"
#include <vector>

uint8_t* DecompressA(FileReader* f, uint32_t decompressedSize, uint32_t compressedEnd) {
    uint8_t* dcmp = new uint8_t[decompressedSize]();
    uint32_t dcmpSize = 0;
    uint32_t controlByte;
    uint8_t controlByteBits = 0;
    uint32_t copyBackControl = 0; // packed control character indicating how many to copy, how far back to copy, etc
    uint32_t copyBackByteCount = 3; // how many bytes to read for the copy back control; 1-3
    uint8_t compressionType = 0; // if set to 0, only 1 byte will ever be read for copy length/distance
    // maybe set to 1 based on first 3 bits of flags value; either invert bit 0 as i see 1 set on my examples, or bit 1. bit 2 seems to indicate a completely different compression
    uint8_t* ret = dcmp;
    for (;;) {
        if (decompressedSize <= 0) {
            break;
        }
        for (;;) {
            if (controlByteBits == 0) {
                break;
            }
            if (compressedEnd - f->GetPosition() <= 0) {
                goto FINISH_DECOMPRESSA;
            }
            if ((controlByte & 0x80) == 0) {
                *dcmp = f->ReadUInt8();
                --decompressedSize;
                ++dcmp;
                ++dcmpSize;
            }
            else {
                int32_t copyBackDistance;
                for (;;) {
                    if (copyBackByteCount == 0) {
                        goto BREAKCOMPRESSATYPE1;
                    }
                    if (compressionType == 1) { // ? this value is never written to? is it a versioning indicator?
                        --copyBackByteCount;
                        if (copyBackByteCount != 0) {
                            if (copyBackByteCount != 1) {
                                int extraToAdd; // this value is mildly confusing; wanting to copy 0x100 bytes is actually
                                // pretty reasonable, but you can't with this system?
                                copyBackControl = f->ReadUInt8();
                                if (copyBackControl & 0xE0) { // upper 3 bits are set means only 1 byte; add 1 since you wouldn't compress a single byte
                                    copyBackControl += 0x10;
                                    copyBackByteCount = 0;
                                    goto BYTE_READ_COMPRESSA;
                                }
                                else {
                                    extraToAdd = 0x110;
                                    if ((copyBackControl & 0x10) != 0) { // mark as 1 byte to copy back to indicate that it's actually encoded in 3 bytes, as 1 byte is unreasonable to compress
                                        extraToAdd += 0x1000;
                                        copyBackControl = extraToAdd + ((copyBackControl & 0xF) << 16);
                                    }
                                    else { // no bytes to copy back means it's encoded in 2 bytes
                                        copyBackControl = extraToAdd + ((copyBackControl & 0xF) << 8);
                                        copyBackByteCount = 1;
                                    }
                                }
                            }
                            else {
                                copyBackControl += f->ReadUInt8() << 8;
                            }
                        }
                        else {
                            copyBackControl += f->ReadUInt8();
                            goto BYTE_READ_COMPRESSA;
                        }

                    }
                    else {
                        break;
                    }
                    if (!(compressedEnd - f->GetPosition() <= 0)) {
                        continue;
                    }
                    goto FINISH_DECOMPRESSA;
                }
                copyBackControl = 0x30 + f->ReadUInt8();
                copyBackByteCount = 0;

            BYTE_READ_COMPRESSA:
                if (compressedEnd - f->GetPosition() <= 0) {
                    goto FINISH_DECOMPRESSA;
                }
            BREAKCOMPRESSATYPE1:
                copyBackDistance = 1 + (((copyBackControl & 0xF) << 8) | f->ReadUInt8());
                copyBackByteCount = 3;


                copyBackControl >>= 4;
                if (copyBackControl != 0) { // why isn't this just a while?
                    do {
                        *dcmp = dcmp[-copyBackDistance];
                        --decompressedSize;
                        ++dcmp;
                        ++dcmpSize;
                    } while (--copyBackControl > 0);
                }
            }
            if (decompressedSize == 0) {
                goto FINISH_DECOMPRESSA;
            }
            controlByte <<= 1;
            --controlByteBits;
        }
        if (compressedEnd - f->GetPosition() <= 0) break;
        controlByte = f->ReadUInt8();
        controlByteBits = 8;
    }
FINISH_DECOMPRESSA:
    return ret;
}

uint8_t* CompressA(uint8_t* input, uint32_t inputLength, uint32_t* outputLength) {
    std::vector<uint8_t> compressed;
    uint32_t controlByteTarget = 0;
    compressed.push_back(0);
    uint8_t controlByte = 0;
    uint8_t controlByteOffs = 3;
    for (uint32_t i = 0; i < 3 && i < inputLength; ++i) {
        compressed.push_back(input[i]);
    }
    for (uint32_t i = 3; i < inputLength; ) {
        uint32_t copyBackLength = 0;
        uint32_t copyBackOffs = 0;
        for (uint32_t j = 1; j < 4096 && j < i; ++j) {
            for (uint32_t k = 0; k < 0xF + 3 && i+k < inputLength; ++k) {
                if (!(input[(i - j) + k] == input[i + k])) {
                    if (copyBackLength < k) {
                        copyBackLength = k;
                        copyBackOffs = j;
                    }
                    break;
                }
            }
            if (copyBackLength == 0xF + 3) {
                break;
            }
        }
        if (copyBackLength < 3) {
            compressed.push_back(input[i]);
            ++i;
        }
        else {
            controlByte |= 0x1;
            copyBackOffs -= 1;
            compressed.push_back((((copyBackLength - 3) & 0xF) << 4) | (copyBackOffs >> 8));
            compressed.push_back(copyBackOffs & 0xFF);
            i += copyBackLength;
        }
        if (controlByteOffs == 7) {
            compressed[controlByteTarget] = controlByte;
            controlByte = 0;
            controlByteOffs = 0;
            controlByteTarget = compressed.size();
            compressed.push_back(0);
        }
        else {
            controlByte <<= 1;
            controlByteOffs += 1;
        }
    }
    if (controlByteOffs == 0) {
        compressed.pop_back();
    }
    else {
        compressed[controlByteTarget] = controlByte << (8 - controlByteOffs);
    }
    *outputLength = compressed.size();

    uint8_t *ret = new uint8_t[compressed.size()];

    memcpy(ret, &compressed[0], compressed.size());

    return ret;
}