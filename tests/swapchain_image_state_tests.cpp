#include "../src/openxr/SwapchainImageState.h"
#include "../src/openxr/StereoCachePolicy.h"
#include <array>
#include <cstdint>
#include <cassert>

int main(){
    kharvox::SwapchainImageState image;
    assert(!image.owned());
    assert(image.acquired());
    assert(image.owned());
    assert(!image.releasable());
    assert(!image.released());
    assert(!image.waited(false));
    assert(image.owned());
    assert(!image.releasable());
    assert(image.waited(true));
    assert(image.releasable());
    assert(!image.acquired());
    assert(image.released());
    assert(!image.owned());

    std::array<std::uintptr_t,2> cache{};
    assert(!kharvox::stereoCacheMatches(cache,1920,1080,1920,1080));
    cache[0]=1;
    // A failed rebuild that created only one eye must never be accepted just
    // because stale extent metadata happens to match a later request.
    assert(!kharvox::stereoCacheMatches(cache,1920,1080,1920,1080));
    cache[1]=2;
    assert(kharvox::stereoCacheMatches(cache,1920,1080,1920,1080));
    assert(!kharvox::stereoCacheMatches(cache,1920,1080,1600,900));
}
