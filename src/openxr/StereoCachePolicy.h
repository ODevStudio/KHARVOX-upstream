#pragma once
#include <array>
#include <cstdint>

namespace kharvox {

template<class Handle>
constexpr bool stereoCacheMatches(
    const std::array<Handle,2>& images,
    uint32_t width, uint32_t height,
    uint32_t requestedWidth, uint32_t requestedHeight) {
    return images[0] != Handle{} && images[1] != Handle{}
        && width == requestedWidth && height == requestedHeight;
}

}
