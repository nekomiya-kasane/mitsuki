/** @file VulkanCompatDevice.h
 *  @brief Vulkan 1.1 Compatibility (Tier 2) backend device.
 *  Namespace: miki::rhi
 */
#pragma once

#include "miki/rhi/backend/BackendStub.h"

namespace miki::rhi {

    class VulkanCompatDevice : public DeviceBase<VulkanCompatDevice> {
       public:
        MIKI_DEVICE_STUB_IMPL(VulkanCompatDevice) { return BackendType::VulkanCompat; }
    };

}  // namespace miki::rhi
