/** @brief Result type for the miki renderer.
 *
 * Result<T> = std::expected<T, ErrorCode> — the universal return type
 * for all fallible operations. Use monadic chaining:
 *   .and_then()  — chain on success
 *   .transform() — map success value
 *   .or_else()   — handle/recover error
 */
#pragma once

#include <expected>

#include "miki/core/ErrorCode.h"

namespace miki::core {

    /** @brief Fallible result type wrapping std::expected.
     *  @tparam T The success value type.
     */
    template <typename T>
    using Result = std::expected<T, ErrorCode>;

    /** @brief Void result for operations that succeed with no value. */
    using VoidResult = std::expected<void, ErrorCode>;

}  // namespace miki::core
