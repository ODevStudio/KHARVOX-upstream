#include "../src/common/GameMemory.h"
#include <memory>
#include <stdexcept>
#include <iostream>
void check(bool value) { if(!value) throw std::runtime_error("VT guard regression"); }

unsigned protectCalls{}, flushCalls{}, freeCalls{}, failProtectCall{};
void* allocatedThunk{};

LPVOID WINAPI testVirtualAlloc(LPVOID address, SIZE_T bytes, DWORD allocation, DWORD protection) {
    allocatedThunk = VirtualAlloc(address, bytes, allocation, protection);
    return allocatedThunk;
}
BOOL WINAPI testVirtualFree(LPVOID address, SIZE_T bytes, DWORD type) {
    check(address == allocatedThunk);
    ++freeCalls;
    return VirtualFree(address, bytes, type);
}
BOOL WINAPI testVirtualProtect(LPVOID address, SIZE_T bytes, DWORD protection, PDWORD previous) {
    if (++protectCalls == failProtectCall) return FALSE;
    return VirtualProtect(address, bytes, protection, previous);
}
BOOL WINAPI testFlushInstructionCache(HANDLE process, LPCVOID address, SIZE_T bytes) {
    ++flushCalls;
    return FlushInstructionCache(process, address, bytes);
}

#define VirtualAlloc testVirtualAlloc
#define VirtualFree testVirtualFree
#define VirtualProtect testVirtualProtect
#define FlushInstructionCache testFlushInstructionCache
#include "../src/vulkan/VirtualTextureGuard.h"
#undef FlushInstructionCache
#undef VirtualProtect
#undef VirtualFree
#undef VirtualAlloc

int main() {
    auto dst=std::make_unique<kharvox::VtPageList>();
    auto src=std::make_unique<kharvox::VtPageList>();
    src->count=3719; dst->count=4582;
    for(size_t i=0;i<8192;++i) {src->pages[i]=i+123;dst->pages[i]=42;}
    const auto original=*src;
    auto result=kharvox::appendVirtualTexturePages(dst.get(),src.get());
    check(result.valid&&result.appended==3610&&result.deferred==109&&dst->count==8192);
    check(dst->pages[4581]==42&&dst->pages[4582]==123&&dst->pages[8191]==3732);
    check(memcmp(src.get(),&original,sizeof(original))==0);
    result=kharvox::appendVirtualTexturePages(dst.get(),src.get());
    check(result.valid&&result.appended==0&&result.deferred==3719);
    dst->count=0;
    result=kharvox::appendVirtualTexturePages(dst.get(),src.get());
    check(result.valid&&result.appended==3719&&result.deferred==0);
    dst->count=8193;check(!kharvox::appendVirtualTexturePages(dst.get(),src.get()).valid);
    dst->count=0;src->count=8193;check(!kharvox::appendVirtualTexturePages(dst.get(),src.get()).valid);
    src->count=0;check(kharvox::appendVirtualTexturePages(dst.get(),src.get()).valid);
    check(!kharvox::appendVirtualTexturePages(src.get(),src.get()).valid);
    // Execute the actual generated x64 thunk and inline jump against synthetic
    // engine memory, not just the C++ policy. The native continuation returns.
    constexpr size_t imageSize=0x17e4000;
    auto image=static_cast<uint8_t*>(VirtualAlloc(nullptr,imageSize,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    check(image!=nullptr);
    const kharvox::gameMemory::Image synthetic{image,imageSize};
    check(!kharvox::installVirtualTextureGuard(kharvox::gameMemory::Image{}));
    check(!kharvox::installVirtualTextureGuard(synthetic));
    check(!kharvox::installVirtualTextureGuard(image,imageSize));
    memcpy(image+0x17e3567,kharvox::vtAppendSignature,sizeof(kharvox::vtAppendSignature));
    check(!kharvox::installVirtualTextureGuard(synthetic));
    memcpy(image+0x17e35d7,kharvox::vtResidencySignature,sizeof(kharvox::vtResidencySignature));
    check(!kharvox::installVirtualTextureGuard(image,imageSize));
    check(memcmp(image+0x17e3567,kharvox::vtAppendSignature,sizeof(kharvox::vtAppendSignature))==0);
    check(!kharvox::installVirtualTextureGuard(kharvox::gameMemory::Image{image,0x17e35d7}));
    DWORD old{};
    check(VirtualProtect(image,imageSize,PAGE_NOACCESS,&old)!=FALSE);
    check(!kharvox::installVirtualTextureGuard(synthetic));
    check(VirtualProtect(image,imageSize,PAGE_EXECUTE_READ,&old)!=FALSE);
    for (const unsigned failure : {1u,2u}) {
        protectCalls=0;flushCalls=0;freeCalls=0;failProtectCall=failure;
        check(!kharvox::installVirtualTextureGuard(synthetic));
        check(freeCalls==1);
        MEMORY_BASIC_INFORMATION released{};
        check(VirtualQuery(allocatedThunk,&released,sizeof(released))!=0);
        check(released.State==MEM_FREE);
        check(memcmp(image+0x17e3567,kharvox::vtAppendSignature,sizeof(kharvox::vtAppendSignature))==0);
    }
    protectCalls=0;flushCalls=0;freeCalls=0;failProtectCall=0;
    check(kharvox::installVirtualTextureGuard(synthetic));
    check(protectCalls==3&&flushCalls==2&&freeCalls==0);
    MEMORY_BASIC_INFORMATION memory{};
    check(VirtualQuery(image+0x17e3567,&memory,sizeof(memory))!=0);
    check(memory.Protect==PAGE_EXECUTE_READ);
    const uint8_t jump[]{0xff,0x25,0,0,0,0};
    check(memcmp(image+0x17e3567,jump,sizeof(jump))==0);
    void* thunk{};
    memcpy(&thunk,image+0x17e3567+sizeof(jump),sizeof(thunk));
    check(thunk==allocatedThunk);
    check(VirtualQuery(thunk,&memory,sizeof(memory))!=0);
    check(memory.Protect==PAGE_EXECUTE_READ);
    check(!kharvox::installVirtualTextureGuard(synthetic));
    check(VirtualProtect(image,imageSize,PAGE_EXECUTE_READWRITE,&old)!=FALSE);
    image[0x17e35d7]=0x5b;image[0x17e35d8]=0xc3; // synthetic continuation: pop rbx; ret
    auto object=std::make_unique<uint8_t[]>(0x20a00);
    auto dp=dst.get();auto sp=src.get();
    memcpy(object.get()+0x209a8,&dp,8);memcpy(object.get()+0x209c0,&sp,8);
    dst->count=4582;src->count=3719;
    uint8_t entry[]={0x53,0x48,0x89,0xcb,0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
    auto address=reinterpret_cast<uintptr_t>(image+0x17e3567);memcpy(entry+6,&address,8);
    memcpy(image,entry,sizeof(entry));
    check(VirtualProtect(image,imageSize,PAGE_EXECUTE_READ,&old)!=FALSE);
    check(FlushInstructionCache(GetCurrentProcess(),image,imageSize)!=FALSE);
    reinterpret_cast<void(*)(void*)>(image)(object.get());
    check(dst->count==8192&&src->count==3719&&dst->pages[8191]==3732);
    VirtualFree(image,0,MEM_RELEASE);
    VirtualFree(thunk,0,MEM_RELEASE);
    std::cout<<"VT overflow policy and executable thunk passed\n";
}
