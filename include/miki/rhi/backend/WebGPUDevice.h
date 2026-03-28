/** @file WebGPUDevice.h
 *  @brief WebGPU / Dawn / wgpu (Tier 3) backend device.
 *  Namespace: miki::rhi
 */
#pragma once

#include "miki/rhi/backend/BackendStub.h"

namespace miki::rhi {

    class WebGPUDevice : public DeviceBase<WebGPUDevice> {
       public:
        MIKI_DEVICE_STUB_IMPL(WebGPUDevice) { return BackendType::WebGPU; }
    };

}  // namespace miki::rhi
