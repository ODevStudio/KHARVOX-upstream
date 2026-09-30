#pragma once
#include "GameMemory.h"
#include <string_view>

namespace kharvox::gameMemory {
struct ImportSlot {
    size_t rva{};
    uintptr_t function{};
};

inline ImportSlot ordinalImport(const Image& image, std::string_view library, WORD ordinal) {
    IMAGE_NT_HEADERS64 nt{};
    if (!image.headers(nt) || nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT)
        return {};
    const auto directory = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress || directory.VirtualAddress >= image.size
        || directory.Size > image.size - directory.VirtualAddress) return {};
    for (size_t offset = 0; offset + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= directory.Size;
         offset += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        IMAGE_IMPORT_DESCRIPTOR descriptor{};
        if (!image.read(directory.VirtualAddress + offset, &descriptor, sizeof(descriptor)) || !descriptor.Name)
            return {};
        bool matches = true;
        for (size_t i = 0; i <= library.size(); ++i) {
            unsigned char actual{};
            if (!image.read(static_cast<size_t>(descriptor.Name) + i, &actual, 1)) return {};
            auto expected = i == library.size() ? 0 : static_cast<unsigned char>(library[i]);
            if (actual >= 'A' && actual <= 'Z') actual += 'a' - 'A';
            if (expected >= 'A' && expected <= 'Z') expected += 'a' - 'A';
            if (actual != expected) { matches = false; break; }
        }
        if (!matches) continue;
        if (!descriptor.OriginalFirstThunk || !descriptor.FirstThunk
            || descriptor.OriginalFirstThunk >= image.size || descriptor.FirstThunk >= image.size
            || descriptor.FirstThunk % alignof(uintptr_t)) return {};
        const size_t available = (std::min)(image.size - descriptor.OriginalFirstThunk,
                                          image.size - descriptor.FirstThunk);
        ImportSlot result{};
        for (size_t i = 0; i + sizeof(IMAGE_THUNK_DATA64) <= available; i += sizeof(IMAGE_THUNK_DATA64)) {
            IMAGE_THUNK_DATA64 name{}, address{};
            if (!image.read(descriptor.OriginalFirstThunk + i, &name, sizeof(name))
                || !image.read(descriptor.FirstThunk + i, &address, sizeof(address))) return {};
            if (!name.u1.AddressOfData) return result;
            if (!IMAGE_SNAP_BY_ORDINAL64(name.u1.Ordinal) || IMAGE_ORDINAL64(name.u1.Ordinal) != ordinal)
                continue;
            if (result.rva || !address.u1.Function) return {};
            result = {descriptor.FirstThunk + i, static_cast<uintptr_t>(address.u1.Function)};
        }
        return {};
    }
    return {};
}
}
