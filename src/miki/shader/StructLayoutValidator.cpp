/** @brief GPU Data Contract Validation (§15.6).
 *
 *  Compares C++ struct layouts (offsetof) against Slang-reflected struct layouts.
 */

#include "miki/shader/StructLayoutValidator.h"

#include <format>
#include <unordered_map>

namespace miki::shader {

    auto ValidateStructLayout(CppStructLayout const& iCpp, ShaderReflection::StructLayout const& iGpu)
        -> std::vector<StructLayoutMismatch> {
        std::vector<StructLayoutMismatch> mismatches;

        // Check total struct size
        if (iCpp.sizeBytes != iGpu.sizeBytes) {
            mismatches.push_back(
                {iCpp.name, "<struct>",
                 std::format("Size mismatch: C++ = {} bytes, GPU = {} bytes", iCpp.sizeBytes, iGpu.sizeBytes)}
            );
        }

        // Build GPU field lookup by name
        std::unordered_map<std::string, ShaderReflection::StructField const*> gpuFields;
        for (auto const& f : iGpu.fields) {
            gpuFields[f.name] = &f;
        }

        // Check each C++ field against GPU reflection
        for (auto const& cppField : iCpp.fields) {
            auto it = gpuFields.find(cppField.name);
            if (it == gpuFields.end()) {
                mismatches.push_back({iCpp.name, cppField.name, "Field exists in C++ but not in GPU reflection"});
                continue;
            }

            auto const* gpuField = it->second;
            if (cppField.offsetBytes != gpuField->offsetBytes) {
                mismatches.push_back(
                    {iCpp.name, cppField.name,
                     std::format("Offset mismatch: C++ = {}, GPU = {}", cppField.offsetBytes, gpuField->offsetBytes)}
                );
            }
            if (cppField.sizeBytes != gpuField->sizeBytes) {
                mismatches.push_back(
                    {iCpp.name, cppField.name,
                     std::format("Size mismatch: C++ = {}, GPU = {}", cppField.sizeBytes, gpuField->sizeBytes)}
                );
            }
        }

        // Check for GPU fields missing in C++
        std::unordered_map<std::string, bool> cppFieldSet;
        for (auto const& f : iCpp.fields) {
            cppFieldSet[f.name] = true;
        }
        for (auto const& gpuField : iGpu.fields) {
            if (!cppFieldSet.contains(gpuField.name)) {
                mismatches.push_back({iCpp.name, gpuField.name, "Field exists in GPU reflection but not in C++"});
            }
        }

        return mismatches;
    }

    auto ValidateAllStructLayouts(
        std::vector<CppStructLayout> const& iCppLayouts, std::vector<ShaderReflection::StructLayout> const& iGpuLayouts
    ) -> std::vector<StructLayoutMismatch> {
        std::vector<StructLayoutMismatch> allMismatches;

        // Build GPU layout lookup by name
        std::unordered_map<std::string, ShaderReflection::StructLayout const*> gpuLookup;
        for (auto const& gl : iGpuLayouts) {
            gpuLookup[gl.name] = &gl;
        }

        for (auto const& cppLayout : iCppLayouts) {
            auto it = gpuLookup.find(cppLayout.name);
            if (it == gpuLookup.end()) {
                allMismatches.push_back(
                    {cppLayout.name, "<struct>", "C++ struct has no matching GPU reflection entry"}
                );
                continue;
            }

            auto mismatches = ValidateStructLayout(cppLayout, *it->second);
            allMismatches.insert(allMismatches.end(), mismatches.begin(), mismatches.end());
        }

        return allMismatches;
    }

}  // namespace miki::shader
