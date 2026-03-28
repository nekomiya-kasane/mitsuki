/** @file OpenGLDevice.h
 *  @brief OpenGL 4.3+ (Tier 4) backend device.
 *  Namespace: miki::rhi
 */
#pragma once

#include "miki/rhi/backend/BackendStub.h"

namespace miki::rhi {

    class OpenGLDevice : public DeviceBase<OpenGLDevice> {
       public:
        MIKI_DEVICE_STUB_IMPL(OpenGLDevice) { return BackendType::OpenGL43; }
    };

}  // namespace miki::rhi
