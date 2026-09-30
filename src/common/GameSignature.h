#pragma once
#include "GameMemory.h"
#include <vector>

namespace kharvox::gameMemory {
inline const unsigned char* findTextSignature(const Image& image,
                                             const unsigned char* signature, size_t bytes) {
    if (!signature || !bytes) return nullptr;
    IMAGE_NT_HEADERS64 nt{};
    size_t sections{};
    if (!image.headers(nt, &sections) || nt.FileHeader.NumberOfSections > 96) return nullptr;
    const unsigned char* match{};
    for (unsigned index = 0; index < nt.FileHeader.NumberOfSections; ++index) {
        IMAGE_SECTION_HEADER section{};
        if (!image.read(sections + index * sizeof(section), &section, sizeof(section))) return nullptr;
        if (std::memcmp(section.Name, ".text\0\0\0", IMAGE_SIZEOF_SHORT_NAME)) continue;
        const size_t size = section.Misc.VirtualSize;
        if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE) || section.VirtualAddress >= image.size
            || size > image.size - section.VirtualAddress || size < bytes) return nullptr;
        std::vector<unsigned char> snapshot(size);
        if (!image.read(section.VirtualAddress, snapshot.data(), size)) return nullptr;
        for (size_t offset = 0; offset <= size - bytes; ++offset) {
            if (std::memcmp(snapshot.data() + offset, signature, bytes)) continue;
            if (match) return nullptr;
            match = image.base + section.VirtualAddress + offset;
        }
    }
    return match;
}
}
