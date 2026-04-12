/** @file StructLayoutValidator.h
 *  @brief GPU Data Contract Validation (§15.6).
 *
 *  Validates that C++ struct layouts match Slang-reflected struct layouts.
 *  Mismatch = hard error in CI builds.
 *
 *  Namespace: miki::shader
 */
#pragma once

#include "miki/shader/ShaderTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace miki::shader {

    /** @brief One field of a C++ struct for layout validation. */
    struct CppStructField {
        std::string name;
        uint32_t offsetBytes = 0;
        uint32_t sizeBytes = 0;
    };

    /** @brief C++ struct layout descriptor for validation against Slang reflection. */
    struct CppStructLayout {
        std::string name;
        uint32_t sizeBytes = 0;
        std::vector<CppStructField> fields;
    };

    /** @brief Result of a struct layout validation check. */
    struct StructLayoutMismatch {
        std::string structName;
        std::string fieldName;
        std::string description;
    };

    /** @brief Validate a C++ struct layout against a Slang-reflected struct layout.
     *
     *  Compares field names, offsets, and sizes. Returns empty vector if layouts match.
     *
     *  @param iCpp   C++ struct layout (from offsetof() measurements).
     *  @param iGpu   GPU struct layout (from ShaderReflection::StructLayout).
     *  @return List of mismatches (empty = valid).
     */
    [[nodiscard]] auto ValidateStructLayout(CppStructLayout const& iCpp, ShaderReflection::StructLayout const& iGpu)
        -> std::vector<StructLayoutMismatch>;

    /** @brief Validate multiple C++ structs against reflected layouts.
     *
     *  @param iCppLayouts  C++ struct layouts.
     *  @param iGpuLayouts  GPU struct layouts from reflection.
     *  @return List of all mismatches across all structs.
     */
    [[nodiscard]] auto ValidateAllStructLayouts(
        std::vector<CppStructLayout> const& iCppLayouts, std::vector<ShaderReflection::StructLayout> const& iGpuLayouts
    ) -> std::vector<StructLayoutMismatch>;

}  // namespace miki::shader
