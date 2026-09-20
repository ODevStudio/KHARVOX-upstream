#include "../src/common/GameSignature.h"
#include "../src/common/GameImports.h"
#include "../src/camera/CameraPoseState.h"
#include <atomic>
#include <cassert>
#include <thread>

using namespace kharvox;

void memoryFaults() {
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const size_t page = system.dwPageSize;
    auto memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, page * 3,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(memory);
    gameMemory::Image image{memory, page * 3};
    bool changed{};
    assert(gameMemory::enableBooleanByte(memory, changed) && changed && memory[0] == 1);
    assert(gameMemory::enableBooleanByte(memory, changed) && !changed);
    memory[0] = 2;
    assert(!gameMemory::enableBooleanByte(memory, changed) && memory[0] == 2);
    constexpr unsigned char expected[]{1, 2, 3, 4};
    std::memcpy(memory + page - 2, expected, sizeof(expected));
    assert(image.matches(page - 2, expected, sizeof(expected)));
    assert(!image.matches(page * 3 - 1, expected, sizeof(expected)));
    assert(!image.matches(SIZE_MAX, expected, sizeof(expected)));
    assert(!gameMemory::range(reinterpret_cast<void*>(UINTPTR_MAX - 1), 4));
    assert(!gameMemory::range(nullptr, 4));
    assert(gameMemory::compareImage(memory, expected, sizeof(expected)));
    const WORD dos = IMAGE_DOS_SIGNATURE;
    assert(!gameMemory::compareImage(gameMemory::mainImage().base, &dos, sizeof(dos)));
    assert(gameMemory::compareImage(gameMemory::mainImage().base + gameMemory::mainImage().size,
        expected, sizeof(expected)));
    DWORD old{};
    for (DWORD protection : {PAGE_NOACCESS, PAGE_READWRITE | PAGE_GUARD, PAGE_EXECUTE}) {
        assert(VirtualProtect(memory + page, page, protection, &old));
        assert(!image.matches(page - 2, expected, sizeof(expected)));
        MEMORY_BASIC_INFORMATION info{};
        assert(VirtualQuery(memory + page, &info, sizeof(info)));
        assert(info.Protect == protection);
    }
    assert(VirtualProtect(memory + page, page, PAGE_READONLY, &old));
    assert(!gameMemory::enableBooleanByte(memory + page, changed));
    assert(image.matches(page - 2, expected, sizeof(expected)));
    assert(!gameMemory::range(memory + page - 2, 4, true));
    assert(VirtualFree(memory + page, page, MEM_DECOMMIT));
    assert(!image.matches(page - 2, expected, sizeof(expected)));
    assert(VirtualFree(memory, 0, MEM_RELEASE));
    unsigned char output{};
    assert(!gameMemory::copy(memory, &output, 1));
}

void signatures() {
    std::array<unsigned char, 4096> memory{};
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(memory.data());
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 128;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(memory.data() + dos->e_lfanew);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->FileHeader.NumberOfSections = 1;
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.SizeOfImage = static_cast<DWORD>(memory.size());
    auto section = IMAGE_FIRST_SECTION(nt);
    std::memcpy(section->Name, ".text", 5);
    section->VirtualAddress = 1024;
    section->Misc.VirtualSize = 2048;
    section->Characteristics = IMAGE_SCN_MEM_EXECUTE;
    constexpr unsigned char signature[]{0x89,0x81,0xe8,0,0,0,0x41,0x8b,0x40,0x20,0xc3};
    const gameMemory::Image image{memory.data(), memory.size()};
    assert(!gameMemory::findTextSignature(image, signature, sizeof(signature)));
    std::memcpy(memory.data() + 2048, signature, sizeof(signature));
    assert(gameMemory::findTextSignature(image, signature, sizeof(signature)) == memory.data() + 2048);
    std::memcpy(memory.data() + 2304, signature, sizeof(signature));
    assert(!gameMemory::findTextSignature(image, signature, sizeof(signature)));
    memory[2304] ^= 1;
    assert(gameMemory::findTextSignature(image, signature, sizeof(signature)));
    section->Misc.VirtualSize = UINT32_MAX;
    assert(!gameMemory::findTextSignature(image, signature, sizeof(signature)));
    dos->e_lfanew = -1;
    assert(!gameMemory::findTextSignature(image, signature, sizeof(signature)));
    dos->e_lfanew = INT32_MAX;
    assert(!gameMemory::findTextSignature(image, signature, sizeof(signature)));
    assert(!gameMemory::findTextSignature({memory.data(), 2}, signature, sizeof(signature)));
    RUNTIME_FUNCTION entry{1024, 1200, 512};
    bool chained{};
    assert(gameMemory::primaryFunction(image, entry, &chained) == 1024 && !chained);
    memory[512] = UNW_FLAG_CHAININFO << 3;
    const RUNTIME_FUNCTION parent{1000, 1100, 600};
    std::memcpy(memory.data() + 516, &parent, sizeof(parent));
    assert(gameMemory::primaryFunction(image, entry, &chained) == 1000 && chained);
    std::memcpy(memory.data() + 516, &entry, sizeof(entry));
    assert(!gameMemory::primaryFunction(image, entry, &chained));
    entry.UnwindData = UINT32_MAX;
    assert(!gameMemory::primaryFunction(image, entry, &chained));
}

void imports() {
    alignas(16) std::array<unsigned char, 4096> memory{};
    const gameMemory::Image image{memory.data(), memory.size()};
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(memory.data());
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 128;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(memory.data() + 128);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.SizeOfImage = static_cast<DWORD>(memory.size());
    nt->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    directory = {512, 40};
    auto descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(memory.data() + 512);
    descriptor->Name = 600;
    descriptor->OriginalFirstThunk = 640;
    descriptor->FirstThunk = 704;
    std::memcpy(memory.data() + 600, "xinput1_4.DLL", 14);
    auto names = reinterpret_cast<IMAGE_THUNK_DATA64*>(memory.data() + 640);
    auto addresses = reinterpret_cast<IMAGE_THUNK_DATA64*>(memory.data() + 704);
    names[0].u1.Ordinal = IMAGE_ORDINAL_FLAG64 | 2;
    addresses[0].u1.Function = 1234;
    assert(gameMemory::ordinalImport(image, "XINPUT1_4.dll", 2).rva == 704);
    assert(gameMemory::ordinalImport(image, "XINPUT1_4.dll", 2).function == 1234);
    assert(!gameMemory::ordinalImport(image, "XINPUT1_4.dll", 3).rva);
    names[1] = names[0];
    addresses[1] = addresses[0];
    assert(!gameMemory::ordinalImport(image, "XINPUT1_4.dll", 2).rva);
    names[1] = {};
    descriptor->FirstThunk = 705;
    assert(!gameMemory::ordinalImport(image, "XINPUT1_4.dll", 2).rva);
    descriptor->FirstThunk = 704;
    descriptor->OriginalFirstThunk = 0;
    assert(!gameMemory::ordinalImport(image, "XINPUT1_4.dll", 2).rva);
    descriptor->OriginalFirstThunk = 4088;
    std::memcpy(memory.data() + 4088, names, sizeof(*names));
    assert(!gameMemory::ordinalImport(image, "XINPUT1_4.dll", 2).rva);
    descriptor->OriginalFirstThunk = 640;
    descriptor->Name = 4095;
    memory[4095] = 'x';
    assert(!gameMemory::ordinalImport(image, "XINPUT1_4.dll", 2).rva);
    directory.Size = UINT32_MAX;
    assert(!gameMemory::ordinalImport(image, "XINPUT1_4.dll", 2).rva);
}

void protectionRace() {
    auto memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(memory);
    memory[0] = 42;
    std::atomic<bool> start{}, done{};
    std::thread writer([&] {
        while (!start.load()) std::this_thread::yield();
        for (int i = 0; i < 10000; ++i) {
            DWORD old{};
            assert(VirtualProtect(memory, 4096, PAGE_NOACCESS, &old));
            assert(VirtualProtect(memory, 4096, PAGE_READWRITE, &old));
        }
        done.store(true);
    });
    start.store(true);
    do {
        unsigned char output{};
        if (gameMemory::copy(memory, &output, 1)) assert(output == 42);
    } while (!done.load());
    writer.join();
    assert(VirtualFree(memory, 0, MEM_RELEASE));
}

void publicationRace() {
    CameraPoseState snapshot;
    const auto generation = snapshot.physics().generation;
    assert(snapshot.publishPhysics({{1,1,1},1,100,generation,true,1,true,true}));
    assert(!snapshot.physics(99).owner && !snapshot.physics(103).owner);
    assert(snapshot.physics(102).owner == 1);
    assert(!snapshot.publishPhysics({{1,1,1},1,99,generation,true}));
    assert(snapshot.invalidate());
    assert(!snapshot.publishPhysics({{1,1,1},1,100,generation,true}));
    assert(!snapshot.physics(100).owner);
    const auto current = snapshot.physics().generation;
    std::atomic<unsigned> done{};
    const auto publish = [&](uintptr_t owner) {
        const float value = static_cast<float>(owner);
        for (unsigned i = 0; i < 50000; ++i)
            assert(snapshot.publishPhysics({{value,value,value},owner,100,current,true,static_cast<uint32_t>(owner),true,true}));
        ++done;
    };
    std::thread first(publish, 1), second(publish, 2);
    while (done.load() != 2) {
        const auto value = snapshot.physics(100);
        if (!value.owner) continue;
        assert(value.inhibitFlags == value.owner);
        for (const auto axis : value.origin) assert(axis == static_cast<float>(value.owner));
    }
    first.join();
    second.join();
}


int main() {
    memoryFaults();
    signatures();
    imports();
    protectionRace();
    publicationRace();
}
