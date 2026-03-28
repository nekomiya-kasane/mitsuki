/** @file Query.h
 *  @brief GPU query pools: timestamp, occlusion, pipeline statistics.
 *  Namespace: miki::rhi
 */
#pragma once

#include <cstdint>
#include <span>

#include "miki/rhi/Handle.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/RhiError.h"

namespace miki::rhi {

    struct QueryPoolDesc {
        QueryType   type  = QueryType::Timestamp;
        uint32_t    count = 0;
        const char* debugName = nullptr;
    };

}  // namespace miki::rhi
