/** @file D3D12Device.h
 *  @brief Direct3D 12 (Tier 1) backend device.
 *  Namespace: miki::rhi
 */
#pragma once

#include "miki/rhi/backend/BackendStub.h"

namespace miki::rhi {

    class D3D12Device : public DeviceBase<D3D12Device> {
       public:
        MIKI_DEVICE_STUB_IMPL(D3D12Device) { return BackendType::D3D12; }
    };

}  // namespace miki::rhi
