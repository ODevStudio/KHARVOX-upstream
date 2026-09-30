#include "../src/common/GameMemory.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <thread>

using Function = int(*)();
Function original{};
void* volatile* slot{};
unsigned protectionCalls{}, failProtectionCall{};
bool replaceBeforeCas{}, invokeBeforeRestore{};
std::promise<void> callbackEntered;
std::atomic<bool> callbackFinished{};
std::thread callback;

int nativeFunction() { return 17; }
int competingFunction() { return 29; }
int hookFunction();

BOOL WINAPI testVirtualProtect(LPVOID address, SIZE_T bytes, DWORD protection, PDWORD previous) {
    ++protectionCalls;
    if (protectionCalls == failProtectionCall) return FALSE;
    if (protectionCalls == 2 && invokeBeforeRestore) {
        const auto entered = callbackEntered.get_future();
        callback = std::thread([] {
            assert(reinterpret_cast<Function>(*slot)() == 17);
            callbackFinished.store(true);
        });
        assert(entered.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
        assert(!callbackFinished.load());
    }
    const auto result = VirtualProtect(address, bytes, protection, previous);
    if (result && protectionCalls == 1 && replaceBeforeCas)
        InterlockedExchangePointer(slot, reinterpret_cast<void*>(&competingFunction));
    return result;
}

#define VirtualProtect testVirtualProtect
#include "../src/hud/MenuVtableHook.h"
#undef VirtualProtect

int hookFunction() {
    callbackEntered.set_value();
    const auto function = kharvox::hud::originalVtableFunction(original);
    assert(function == &nativeFunction);
    return function();
}

void reset() {
    DWORD previous{};
    assert(VirtualProtect(const_cast<void**>(slot), 4096, PAGE_READWRITE, &previous));
    *slot = reinterpret_cast<void*>(&nativeFunction);
    assert(VirtualProtect(const_cast<void**>(slot), 4096, PAGE_READONLY, &previous));
    original = nullptr;
    protectionCalls = 0;
    failProtectionCall = 0;
    replaceBeforeCas = false;
    invokeBeforeRestore = false;
}

void checkProtection(DWORD protection) {
    MEMORY_BASIC_INFORMATION memory{};
    assert(VirtualQuery(const_cast<void**>(slot), &memory, sizeof(memory)));
    assert(memory.Protect == protection);
}

auto install() {
    return kharvox::hud::installVtableFunction(slot, &nativeFunction,
        reinterpret_cast<const void*>(&hookFunction), original);
}

int main() {
    slot = static_cast<void**>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(slot);
    reset();
    invokeBeforeRestore = true;
    const auto success = install();
    callback.join();
    assert(success.installed && success.protectionRestored && callbackFinished.load());
    assert(original == &nativeFunction && *slot == reinterpret_cast<void*>(&hookFunction));
    checkProtection(PAGE_READONLY);

    reset();
    replaceBeforeCas = true;
    const auto lost = install();
    assert(!lost.installed && lost.protectionRestored && !original);
    assert(*slot == reinterpret_cast<void*>(&competingFunction));
    checkProtection(PAGE_READONLY);

    reset();
    failProtectionCall = 1;
    const auto denied = install();
    assert(!denied.installed && denied.protectionRestored && !original);
    assert(*slot == reinterpret_cast<void*>(&nativeFunction));
    checkProtection(PAGE_READONLY);

    reset();
    failProtectionCall = 2;
    const auto installedWritable = install();
    assert(installedWritable.installed && !installedWritable.protectionRestored);
    assert(original == &nativeFunction && *slot == reinterpret_cast<void*>(&hookFunction));
    checkProtection(PAGE_READWRITE);

    reset();
    replaceBeforeCas = true;
    failProtectionCall = 2;
    const auto rejectedWritable = install();
    assert(!rejectedWritable.installed && !rejectedWritable.protectionRestored && !original);
    assert(*slot == reinterpret_cast<void*>(&competingFunction));
    checkProtection(PAGE_READWRITE);
    assert(VirtualFree(const_cast<void**>(slot), 0, MEM_RELEASE));
}
