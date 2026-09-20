#pragma once
#include "../common/GameMemory.h"
#include <mutex>

namespace kharvox::hud {
inline std::mutex menuVtableMutex;

struct VtableInstallResult {
    bool installed{};
    bool protectionRestored{true};
};

template<class Function>
Function originalVtableFunction(const Function& original) {
    std::lock_guard lock(menuVtableMutex);
    return original;
}

template<class Function>
VtableInstallResult installVtableFunction(void* volatile* entry, Function expected,
                                        const void* hook, Function& original) {
    std::lock_guard lock(menuVtableMutex);
    void* current{};
    if (!expected || !hook || reinterpret_cast<uintptr_t>(entry) % alignof(void*)
        || !gameMemory::copy(const_cast<void**>(entry), &current, sizeof(current))
        || current != reinterpret_cast<void*>(expected)) return {};
    DWORD oldProtect{};
    if (!VirtualProtect(const_cast<void**>(entry), sizeof(void*), PAGE_READWRITE, &oldProtect)) return {};
    const bool replaced = InterlockedCompareExchangePointer(entry, const_cast<void*>(hook),
        reinterpret_cast<void*>(expected)) == reinterpret_cast<void*>(expected);
    if (replaced) original = expected;
    DWORD ignored{};
    const bool restored = VirtualProtect(const_cast<void**>(entry), sizeof(void*), oldProtect, &ignored) != FALSE;
    return {replaced, restored};
}
}
