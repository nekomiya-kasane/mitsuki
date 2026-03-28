/** @file WindowHandle.h
 *  @brief Stable, generation-counted window identifier (POD).
 *
 *  Extracted from WindowManager.h so that SurfaceManager (miki::rhi) can
 *  reference windows without depending on WindowManager (miki::platform).
 *  This preserves the three-concern separation (spec SS2.1).
 *
 *  Namespace: miki::platform
 */
#pragma once

#include <cstdint>

namespace miki::platform {

    /** @brief Stable, monotonic, generation-counted window identifier.
     *
     *  Never reused within a process lifetime (generation prevents ABA).
     *  POD type -- trivially copyable, zero-cost to pass by value.
     */
    struct WindowHandle {
        uint32_t id = 0;
        uint16_t generation = 0;

        [[nodiscard]] constexpr auto IsValid() const noexcept -> bool { return id != 0; }
        constexpr auto operator==(const WindowHandle&) const noexcept -> bool = default;
    };

}  // namespace miki::platform
