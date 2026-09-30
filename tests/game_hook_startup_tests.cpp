#include <array>
#include <cassert>
#include <chrono>
#include <future>
#include <mutex>
#include <string_view>

static std::mutex mutex;
static struct { struct { bool layoutChecked{},layoutAvailable{}; } cinematicAdaptiveTick; } s;
static bool supported=true,layoutValid=true,installationSucceeds=true;
static unsigned identityChecks{},layoutChecks{},installations{},diagnostics{};
static unsigned char imageByte{};

namespace kharvox::gameMemory {
struct Image { const unsigned char* base; };
static Image mainImage(){return {&imageByte};}
static bool supportedDoomImage(){++identityChecks;return supported;}
}

static bool validateDoomAdaptiveTickLayout(unsigned char* image){
    assert(image==&imageByte);
    ++layoutChecks;
    return layoutValid;
}
static bool installImmersiveAdaptiveParticipantBypass(unsigned char* image){
    assert(image==&imageByte);
    ++installations;
    return installationSucceeds;
}
static void log(const char*){++diagnostics;}

#include "../src/openxr/GameHookStartup.inc"

int main(int argc,char** argv){
    assert(argc==2);
    const std::string_view mode=argv[1];
    supported=mode!="unsupported";
    layoutValid=mode!="mismatch";
    installationSucceeds=mode!="install-failure";
    std::array<std::future<void>,8> callers;
    for(auto& caller:callers)caller=std::async(std::launch::async,KharvoxXRInstallGameHooks);
    for(auto& caller:callers)caller.get();
    const bool available=supported&&layoutValid&&installationSucceeds;
    assert(s.cinematicAdaptiveTick.layoutChecked);
    assert(s.cinematicAdaptiveTick.layoutAvailable==available);
    assert(identityChecks==1);
    assert(layoutChecks==unsigned(supported));
    assert(installations==unsigned(supported&&layoutValid));
    assert(diagnostics==unsigned(!available));
    std::lock_guard<std::mutex> xrCall(mutex);
    auto runtimeThread=std::async(std::launch::async,KharvoxXRInstallGameHooks);
    assert(runtimeThread.wait_for(std::chrono::seconds(2))==std::future_status::ready);
    runtimeThread.get();
    KharvoxXRInstallGameHooks();
    assert(identityChecks==1);
    assert(installations==unsigned(supported&&layoutValid));
}
