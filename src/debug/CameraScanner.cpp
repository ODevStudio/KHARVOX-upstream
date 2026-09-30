#include "../common/GameMemory.h"
#include "../common/DiagnosticLogging.h"
#include "CameraScanner.h"
#include "../common/RuntimePaths.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

namespace {
struct Candidate { float* address{}; std::array<float,9> baseline{}; float matrixScore{}; float change{}; };
std::vector<Candidate> candidates;
std::atomic<bool> started{};

void log(const std::string& text){
    if (!kharvox::extendedDiagnosticsEnabled()) return;char temp[MAX_PATH]{};GetTempPathA(MAX_PATH,temp);std::ofstream out(std::string(temp)+"KHARVOX.log",std::ios::app);out<<"[KHARVOX][CAMSCAN] "<<text<<'\n';}
bool readableWritable(DWORD p){p&=0xff;return p==PAGE_READWRITE||p==PAGE_WRITECOPY||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;}
float score(const float*m){for(int i=0;i<9;i++)if(!std::isfinite(m[i])||std::fabs(m[i])>1.1f)return 0;float err=0;for(int r=0;r<3;r++){float l=0;for(int c=0;c<3;c++)l+=m[r*3+c]*m[r*3+c];err+=std::fabs(l-1);}for(int a=0;a<3;a++)for(int b=a+1;b<3;b++){float d=0;for(int c=0;c<3;c++)d+=m[a*3+c]*m[b*3+c];err+=std::fabs(d);}return std::max(0.f,1.f-err/6.f);}
bool safeRead(float* address, std::array<float,9>& out) {
    return kharvox::gameMemory::copy(address, out.data(), sizeof(out));
}
void capture() {
    candidates.clear();
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    auto cursor = reinterpret_cast<uintptr_t>(system.lpMinimumApplicationAddress);
    const auto maximum = reinterpret_cast<uintptr_t>(system.lpMaximumApplicationAddress);
    size_t scanned{};
    constexpr size_t budget = 256ull * 1024 * 1024;
    std::array<unsigned char, 65536> snapshot{};
    while (cursor < maximum && scanned < budget && candidates.size() < 50000) {
        MEMORY_BASIC_INFORMATION memory{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &memory, sizeof(memory))) break;
        const auto begin = reinterpret_cast<uintptr_t>(memory.BaseAddress);
        if (memory.RegionSize > UINTPTR_MAX - begin) break;
        const auto next = begin + memory.RegionSize;
        if (next <= cursor) break;
        const bool scoped = memory.Type == MEM_IMAGE
            || (memory.Type == MEM_PRIVATE && memory.RegionSize <= 8ull * 1024 * 1024);
        if (scoped && memory.State == MEM_COMMIT && readableWritable(memory.Protect)
            && !(memory.Protect & (PAGE_GUARD | PAGE_NOCACHE))) {
            for (size_t offset = 0; offset < memory.RegionSize && scanned < budget;) {
                const auto bytes = std::min({snapshot.size(), memory.RegionSize - offset, budget - scanned});
                scanned += bytes;
                if (kharvox::gameMemory::copy(reinterpret_cast<void*>(begin + offset), snapshot.data(), bytes)) {
                    for (size_t at = 0; at + 9 * sizeof(float) <= bytes; at += 16) {
                        Candidate candidate;
                        std::memcpy(candidate.baseline.data(), snapshot.data() + at, sizeof(candidate.baseline));
                        candidate.matrixScore = score(candidate.baseline.data());
                        if (candidate.matrixScore <= 0.992f) continue;
                        candidate.address = reinterpret_cast<float*>(begin + offset + at);
                        candidates.push_back(candidate);
                        if (candidates.size() >= 50000) break;
                    }
                }
                if (candidates.size() >= 50000 || bytes <= 32) break;
                offset += bytes - 32;
            }
        }
        cursor = next;
    }
    log("F6 baseline captured candidates=" + std::to_string(candidates.size())
        + " scannedMB=" + std::to_string(scanned / 1024 / 1024));
}
void compare(){std::vector<Candidate> ranked;ranked.reserve(candidates.size());for(auto c:candidates){std::array<float,9> now{};if(!safeRead(c.address,now))continue;float currentScore=score(now.data());if(currentScore<0.98f)continue;float delta=0;for(int i=0;i<9;i++)delta+=std::fabs(now[i]-c.baseline[i]);if(delta<0.03f)continue;c.change=delta;c.matrixScore=currentScore;ranked.push_back(c);}std::sort(ranked.begin(),ranked.end(),[](auto&a,auto&b){return a.matrixScore+a.change*.05f>b.matrixScore+b.change*.05f;});log("F7 reactive orthonormal candidates="+std::to_string(ranked.size()));for(size_t i=0;i<std::min<size_t>(20,ranked.size());i++){auto&c=ranked[i];std::ostringstream o;o<<'#'<<(i+1)<<" addr="<<c.address<<" matrixScore="<<c.matrixScore<<" change="<<c.change;log(o.str());}}
DWORD WINAPI worker(void*) {
    for (;;) {
        const auto captureMarker = kharvox::runtimePath(L"camscan_capture");
        const auto compareMarker = kharvox::runtimePath(L"camscan_compare");
        const bool captureRequested = DeleteFileW(captureMarker.c_str()) != FALSE;
        const bool compareRequested = DeleteFileW(compareMarker.c_str()) != FALSE;
        if ((GetAsyncKeyState(VK_F6) & 1) || captureRequested) capture();
        if ((GetAsyncKeyState(VK_F7) & 1) || compareRequested) compare();
        Sleep(20);
    }
}
}

void KharvoxCameraScannerStart(){if(started.exchange(true))return;auto thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);if(thread){CloseHandle(thread);log("read-only scanner: F6 baseline, move mouse camera, F7 rank");}else log("worker creation failed");}
