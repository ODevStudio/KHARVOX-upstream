#include "../src/native/upstream/src/hooks/pattern_scan.cpp"
#include <cassert>

namespace kharvoxnative::log {
void info(std::string_view) {}
void warn(std::string_view) {}
void error(std::string_view) {}
}

int main() {
    using namespace kharvoxnative::scan;
    assert(parse("48 8B ?? ? 00") == std::vector<int>({0x48,0x8b,-1,-1,0}));
    assert(parse(" 48\t8b\r\n00 ") == std::vector<int>({0x48,0x8b,0}));
    for (const auto pattern : {"", "4", "48 G0", "48 1x", "48 8", "48 ???", "488B"})
        assert(parse(pattern).empty());
    std::array<unsigned char, 8> bytes{0x48,0x8b,0x12,0,0x48,0x8b,0x34,0};
    assert(find_all(bytes.data(), bytes.size(), parse("48 8b ?? 00")).size() == 2);
    assert(find_all(bytes.data(), bytes.size(), parse("48 8b 12 00")).size() == 1);
    assert(find_all(bytes.data(), 2, parse("48 8b 12")).empty());
    auto memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(memory);
    std::memcpy(memory, bytes.data(), bytes.size());
    DWORD old{};
    assert(VirtualProtect(memory + 4096, 4096, PAGE_NOACCESS, &old));
    assert(find_all(memory, 8192, parse("48 8b 12 00")).empty());
    assert(VirtualFree(memory, 0, MEM_RELEASE));
    assert(!module_build_info(L"missing-game-memory-audit-module.dll"));
    assert(module_build_info(nullptr));
    assert(!resolve_signature(nullptr, "wrong-build", "48 8b", ModuleBuildInfo{1,1}));
}
