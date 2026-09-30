#pragma once
#include <windows.h>
#include <psapi.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

namespace kharvox::gameMemory {
inline bool range(const void* address, size_t bytes, bool writable = false,
                  const void* allocation = nullptr) {
    auto cursor = reinterpret_cast<uintptr_t>(address);
    if (!cursor || !bytes || bytes > (std::numeric_limits<uintptr_t>::max)() - cursor)
        return false;
    const auto end = cursor + bytes;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info))
            || info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))
            || (allocation && (info.AllocationBase != allocation || info.Type != MEM_IMAGE)))
            return false;
        const auto protection = info.Protect & 0xff;
        const bool canWrite = protection == PAGE_READWRITE || protection == PAGE_WRITECOPY
            || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        if (!canWrite && (writable || (protection != PAGE_READONLY && protection != PAGE_EXECUTE_READ)))
            return false;
        const auto begin = reinterpret_cast<uintptr_t>(info.BaseAddress);
        if (cursor < begin || cursor - begin >= info.RegionSize) return false;
        cursor += (std::min)(end - cursor, info.RegionSize - (cursor - begin));
    }
    return true;
}

inline bool copy(const void* address, void* output, size_t bytes) {
    if (!output || !range(address, bytes)) return false;
    SIZE_T copied{};
    return ReadProcessMemory(GetCurrentProcess(), address, output, bytes, &copied)
        && copied == bytes;
}

inline bool enableBooleanByte(void* address, bool& changed) {
    changed = false;
    if (!range(address, 1, true)) return false;
#if defined(_MSC_VER)
    __try {
        const auto previous = _InterlockedCompareExchange8(static_cast<volatile char*>(address), 1, 0);
        changed = previous == 0;
        return previous == 0 || previous == 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    return false;
#endif
}

struct Image {
    const unsigned char* base{};
    size_t size{};

    bool contains(const void* address, size_t bytes) const {
        const auto start = reinterpret_cast<uintptr_t>(base);
        const auto at = reinterpret_cast<uintptr_t>(address);
        return start && at >= start && at - start < size && bytes && bytes <= size - (at - start);
    }

    bool read(size_t rva, void* output, size_t bytes) const {
        if (!base || rva >= size || !bytes || bytes > size - rva) return false;
        const auto address = reinterpret_cast<uintptr_t>(base);
        if (rva > (std::numeric_limits<uintptr_t>::max)() - address) return false;
        return copy(reinterpret_cast<const void*>(address + rva), output, bytes);
    }

    bool matches(size_t rva, const void* expected, size_t bytes) const {
        if (!expected || !bytes || rva >= size || bytes > size - rva) return false;
        std::array<unsigned char, 256> buffer{};
        for (size_t offset = 0; offset < bytes;) {
            const auto count = (std::min)(bytes - offset, buffer.size());
            if (!read(rva + offset, buffer.data(), count)
                || std::memcmp(buffer.data(), static_cast<const unsigned char*>(expected) + offset, count))
                return false;
            offset += count;
        }
        return true;
    }

    bool headers(IMAGE_NT_HEADERS64& nt, size_t* sections = nullptr) const {
        IMAGE_DOS_HEADER dos{};
        if (!read(0, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0
            || !read(static_cast<size_t>(dos.e_lfanew), &nt, sizeof(nt))
            || nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
            || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64
            || nt.FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64)
            || !nt.OptionalHeader.SizeOfImage || nt.OptionalHeader.SizeOfImage > size)
            return false;
        if (sections) *sections = static_cast<size_t>(dos.e_lfanew) + sizeof(nt);
        return true;
    }
};

inline Image module(HMODULE handle) {
    MODULEINFO info{};
    if (!handle || !GetModuleInformation(GetCurrentProcess(), handle, &info, sizeof(info))) return {};
    return {static_cast<const unsigned char*>(info.lpBaseOfDll), info.SizeOfImage};
}

inline const Image& mainImage() {
    static const Image image = module(GetModuleHandleW(nullptr));
    return image;
}

inline bool supportedDoomImage(const Image& image) {
    IMAGE_NT_HEADERS64 headers{};
    return image.headers(headers) && headers.FileHeader.TimeDateStamp == 1711036533
        && headers.OptionalHeader.SizeOfImage == 336990208;
}

inline bool supportedDoomImage() {
    static const bool supported = supportedDoomImage(mainImage());
    return supported;
}

inline bool imageRange(const void* address, size_t bytes, bool writable = false) {
    const auto& image = mainImage();
    return image.contains(address, bytes) && range(address, bytes, writable, image.base);
}

inline int compareImage(const void* address, const void* expected, size_t bytes) {
    const auto& image = mainImage();
    if (!imageRange(address, bytes)) return 1;
    return image.matches(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(image.base),
                         expected, bytes) ? 0 : 1;
}

inline bool hashImage(const void* address, size_t bytes, uint64_t expected) {
    if (!imageRange(address, bytes)) return false;
    std::array<unsigned char, 256> buffer{};
    uint64_t hash = 14695981039346656037ull;
    for (size_t offset = 0; offset < bytes;) {
        const auto count = (std::min)(bytes - offset, buffer.size());
        if (!copy(static_cast<const unsigned char*>(address) + offset, buffer.data(), count)) return false;
        for (size_t i = 0; i < count; ++i) hash = (hash ^ buffer[i]) * 1099511628211ull;
        offset += count;
    }
    return hash == expected;
}

inline bool functionEntry(const void* address, RUNTIME_FUNCTION& entry) {
    if (!imageRange(address, 1)) return false;
    DWORD64 base{};
    const auto found = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(address), &base, nullptr);
    unsigned char unwind{};
    return base == reinterpret_cast<DWORD64>(mainImage().base)
        && imageRange(found, sizeof(entry)) && copy(found, &entry, sizeof(entry))
        && entry.BeginAddress < entry.EndAddress && entry.EndAddress <= mainImage().size
        && base + entry.BeginAddress == reinterpret_cast<DWORD64>(address)
        && mainImage().read(entry.UnwindData, &unwind, sizeof(unwind))
        && !((unwind >> 3) & UNW_FLAG_CHAININFO);
}

inline uint32_t primaryFunction(const Image& image, RUNTIME_FUNCTION entry, bool* chained) {
    if (chained) *chained = false;
    for (unsigned depth = 0; depth < 8; ++depth) {
        std::array<unsigned char, 4> unwind{};
        if (entry.BeginAddress >= entry.EndAddress || entry.EndAddress > image.size
            || !image.read(entry.UnwindData, unwind.data(), unwind.size())) return 0;
        if (!((unwind[0] >> 3) & UNW_FLAG_CHAININFO)) return entry.BeginAddress;
        if (chained) *chained = true;
        const size_t next = static_cast<size_t>(entry.UnwindData) + 4 + ((unwind[2] + 1u) & ~1u) * 2;
        if (!image.read(next, &entry, sizeof(entry))) return 0;
    }
    return 0;
}
}
