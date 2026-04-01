/** @brief SlangCompiler implementation.
 *
 * Uses the Slang COM-like C API via slang.h to compile shaders
 * to SPIR-V, DXIL, GLSL 4.30, and WGSL, and extract reflection data.
 *
 * Architecture improvements over reference (D:\repos\miki):
 *   - Session pool: reuses slang::ISession per (target, searchPaths) tuple
 *   - Structured diagnostics: ShaderDiagnostic vector, no stderr
 *   - Correct BackendType mapping for mitsuki (Vulkan14, VulkanCompat, OpenGL43)
 */

#include "miki/shader/SlangCompiler.h"

#include <slang.h>
#include <slang-com-ptr.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <expected>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace miki::shader {

    // ===========================================================================
    // Helpers -- miki enum <-> Slang enum mapping
    // ===========================================================================

    static auto ToSlangTarget(ShaderTargetType iType) -> SlangCompileTarget {
        switch (iType) {
            case ShaderTargetType::SPIRV: return SLANG_SPIRV;
            case ShaderTargetType::DXIL: return SLANG_DXIL;
            case ShaderTargetType::GLSL: return SLANG_GLSL;
            case ShaderTargetType::WGSL: return SLANG_WGSL;
        }
        return SLANG_TARGET_UNKNOWN;
    }

    static auto ToSlangStage(ShaderStage iStage) -> SlangStage {
        switch (iStage) {
            case ShaderStage::Vertex: return SLANG_STAGE_VERTEX;
            case ShaderStage::Fragment: return SLANG_STAGE_FRAGMENT;
            case ShaderStage::Compute: return SLANG_STAGE_COMPUTE;
            case ShaderStage::Mesh: return SLANG_STAGE_MESH;
            case ShaderStage::Task: return SLANG_STAGE_AMPLIFICATION;
            case ShaderStage::RayGen: return SLANG_STAGE_RAY_GENERATION;
            case ShaderStage::ClosestHit: return SLANG_STAGE_CLOSEST_HIT;
            case ShaderStage::Miss: return SLANG_STAGE_MISS;
            case ShaderStage::AnyHit: return SLANG_STAGE_ANY_HIT;
            case ShaderStage::Intersection: return SLANG_STAGE_INTERSECTION;
            case ShaderStage::Callable: return SLANG_STAGE_CALLABLE;
            default: return SLANG_STAGE_NONE;  // Composite bitmask values (AllGraphics, All)
        }
    }

    static auto ReadFileToString(std::filesystem::path const& iPath) -> std::string {
        std::ifstream file(iPath, std::ios::binary | std::ios::ate);
        if (!file) {
            return {};
        }
        auto size = file.tellg();
        if (size <= 0) {
            return {};
        }
        file.seekg(0);
        std::string content(static_cast<size_t>(size), '\0');
        file.read(content.data(), size);
        return content;
    }

    static auto ExtractDiagnosticString(slang::IBlob* iBlob) -> std::string {
        if (!iBlob || iBlob->getBufferSize() == 0) {
            return {};
        }
        return std::string(static_cast<char const*>(iBlob->getBufferPointer()), iBlob->getBufferSize());
    }

    // ===========================================================================
    // Slang type -> rhi::Format mapping for vertex inputs
    // ===========================================================================

    static auto SlangTypeToFormat(slang::TypeLayoutReflection* iTypeLayout) -> rhi::Format {
        if (!iTypeLayout) {
            return rhi::Format::Undefined;
        }
        auto* type = iTypeLayout->getType();
        if (!type) {
            return rhi::Format::Undefined;
        }

        auto scalar = type->getScalarType();
        auto cols = type->getColumnCount();

        if (scalar == slang::TypeReflection::ScalarType::Float32) {
            switch (cols) {
                case 1: return rhi::Format::R32_FLOAT;
                case 2: return rhi::Format::RG32_FLOAT;
                case 3: return rhi::Format::RGBA32_FLOAT;
                case 4: return rhi::Format::RGBA32_FLOAT;
            }
        } else if (scalar == slang::TypeReflection::ScalarType::Int32) {
            switch (cols) {
                case 1: return rhi::Format::R32_SINT;
                case 2: return rhi::Format::RG32_SINT;
                case 3: return rhi::Format::RGBA32_SINT;
                case 4: return rhi::Format::RGBA32_SINT;
            }
        } else if (scalar == slang::TypeReflection::ScalarType::UInt32) {
            switch (cols) {
                case 1: return rhi::Format::R32_UINT;
                case 2: return rhi::Format::RG32_UINT;
                case 3: return rhi::Format::RGBA32_UINT;
                case 4: return rhi::Format::RGBA32_UINT;
            }
        } else if (scalar == slang::TypeReflection::ScalarType::Float16) {
            switch (cols) {
                case 1: return rhi::Format::R16_FLOAT;
                case 2: return rhi::Format::RG16_FLOAT;
                case 3: return rhi::Format::RGBA16_FLOAT;
                case 4: return rhi::Format::RGBA16_FLOAT;
            }
        }
        return rhi::Format::Undefined;
    }

    // ===========================================================================
    // Anonymous helpers for reflection
    // ===========================================================================

    namespace {

        auto CollectModuleConstantVarsRecursive(
            slang::DeclReflection* decl, std::vector<ShaderReflection::ModuleConstant>& out
        ) -> void {
            if (!decl) {
                return;
            }
            if (decl->getKind() == slang::DeclReflection::Kind::Variable) {
                auto* v = decl->asVariable();
                if (v && v->hasDefaultValue()) {
                    int64_t iv = 0;
                    if (SLANG_SUCCEEDED(v->getDefaultValueInt(&iv))) {
                        ShaderReflection::ModuleConstant c;
                        c.name = v->getName() ? v->getName() : "";
                        c.hasIntValue = true;
                        c.intValue = iv;
                        out.push_back(std::move(c));
                    }
                }
            }
            for (unsigned i = 0; i < decl->getChildrenCount(); ++i) {
                CollectModuleConstantVarsRecursive(decl->getChild(i), out);
            }
        }

        auto CollectStructDeclNamesRecursive(slang::DeclReflection* decl, std::unordered_set<std::string>& oNames)
            -> void {
            if (!decl) {
                return;
            }
            if (decl->getKind() == slang::DeclReflection::Kind::Struct) {
                char const* n = decl->getName();
                if (n != nullptr && *n != '\0') {
                    oNames.insert(std::string(n));
                }
            }
            for (unsigned i = 0; i < decl->getChildrenCount(); ++i) {
                CollectStructDeclNamesRecursive(decl->getChild(i), oNames);
            }
        }

        auto TryAppendStructLayout(
            slang::ProgramLayout* layout, std::string_view iStructName, ShaderReflection& oReflection
        ) -> bool {
            std::string const structNameBuf(iStructName);
            auto* type = layout->findTypeByName(structNameBuf.c_str());
            if (!type) {
                return false;
            }

            auto* typeLayout = layout->getTypeLayout(type, slang::LayoutRules::DefaultStructuredBuffer);
            if (!typeLayout) {
                return false;
            }

            size_t const layoutSize = typeLayout->getSize(slang::ParameterCategory::Uniform);
            if (layoutSize == SLANG_UNBOUNDED_SIZE || layoutSize == SLANG_UNKNOWN_SIZE) {
                return false;
            }

            ShaderReflection::StructLayout result;
            result.name = structNameBuf;
            result.sizeBytes = static_cast<uint32_t>(layoutSize);
            result.alignment = static_cast<uint32_t>(typeLayout->getAlignment(slang::ParameterCategory::Uniform));

            auto const numFields = typeLayout->getFieldCount();
            for (unsigned fi = 0; fi < numFields; ++fi) {
                auto* fieldLayout = typeLayout->getFieldByIndex(fi);
                if (!fieldLayout) {
                    continue;
                }

                ShaderReflection::StructField field;
                field.name = fieldLayout->getName() ? fieldLayout->getName() : "";
                field.offsetBytes = static_cast<uint32_t>(fieldLayout->getOffset(slang::ParameterCategory::Uniform));
                auto* fieldTy = fieldLayout->getTypeLayout();
                size_t fsz = fieldTy ? fieldTy->getSize(slang::ParameterCategory::Uniform) : 0;
                if (fsz == SLANG_UNBOUNDED_SIZE || fsz == SLANG_UNKNOWN_SIZE) {
                    fsz = 0;
                }
                field.sizeBytes = static_cast<uint32_t>(fsz);
                result.fields.push_back(std::move(field));
            }

            oReflection.structLayouts.push_back(std::move(result));
            return true;
        }

    }  // namespace

    // ===========================================================================
    // Impl -- session pool + compilation
    // ===========================================================================

    struct SlangCompiler::Impl {
        Slang::ComPtr<slang::IGlobalSession> globalSession;
        std::vector<std::string> searchPaths;

        // Session pool: one cached session per ShaderTarget (type + version, no permutation macros)
        std::unordered_map<uint16_t, Slang::ComPtr<slang::ISession>> sessionPool;
        std::mutex sessionPoolMutex;

        // Diagnostics from last compilation
        std::vector<ShaderDiagnostic> lastDiagnostics;

        auto RecordDiagnostic(std::string msg, ShaderDiagnostic::Severity severity = ShaderDiagnostic::Severity::Error)
            -> void {
            ShaderDiagnostic d;
            d.message = std::move(msg);
            d.severity = severity;
            lastDiagnostics.push_back(std::move(d));
        }

        auto RecordBlobDiagnostic(
            slang::IBlob* blob, ShaderDiagnostic::Severity severity = ShaderDiagnostic::Severity::Error
        ) -> void {
            auto msg = ExtractDiagnosticString(blob);
            if (!msg.empty()) {
                RecordDiagnostic(std::move(msg), severity);
            }
        }

        auto GetSlangProfile(ShaderTarget iTarget) -> SlangProfileID {
            char profileStr[32];
            switch (iTarget.type) {
                case ShaderTargetType::SPIRV:
                    std::snprintf(
                        profileStr, sizeof(profileStr), "spirv_%u_%u", iTarget.versionMajor, iTarget.versionMinor
                    );
                    return globalSession->findProfile(profileStr);
                case ShaderTargetType::DXIL:
                    std::snprintf(
                        profileStr, sizeof(profileStr), "sm_%u_%u", iTarget.versionMajor, iTarget.versionMinor
                    );
                    return globalSession->findProfile(profileStr);
                case ShaderTargetType::GLSL:
                    std::snprintf(
                        profileStr, sizeof(profileStr), "glsl_%u%02u", iTarget.versionMajor, iTarget.versionMinor
                    );
                    return globalSession->findProfile(profileStr);
                case ShaderTargetType::WGSL: return globalSession->findProfile("wgsl");
            }
            return globalSession->findProfile("spirv_1_5");
        }

        auto CreateSession(ShaderTarget iTarget, std::span<const slang::PreprocessorMacroDesc> iMacros)
            -> core::Result<Slang::ComPtr<slang::ISession>> {
            slang::TargetDesc targetDesc = {};
            targetDesc.structureSize = sizeof(slang::TargetDesc);
            targetDesc.format = ToSlangTarget(iTarget.type);
            targetDesc.profile = GetSlangProfile(iTarget);

            std::vector<char const*> pathPtrs;
            pathPtrs.reserve(searchPaths.size());
            for (auto const& p : searchPaths) {
                pathPtrs.push_back(p.c_str());
            }

            slang::SessionDesc sessionDesc = {};
            sessionDesc.structureSize = sizeof(slang::SessionDesc);
            sessionDesc.targets = &targetDesc;
            sessionDesc.targetCount = 1;
            sessionDesc.searchPaths = pathPtrs.data();
            sessionDesc.searchPathCount = static_cast<SlangInt>(pathPtrs.size());
            sessionDesc.preprocessorMacros = iMacros.data();
            sessionDesc.preprocessorMacroCount = static_cast<SlangInt>(iMacros.size());

            Slang::ComPtr<slang::ISession> session;
            auto result = globalSession->createSession(sessionDesc, session.writeRef());
            if (SLANG_FAILED(result)) {
                return std::unexpected(core::ErrorCode::ShaderCompilationFailed);
            }
            return session;
        }

        /** @brief Get or create a cached session for the given target (no custom macros). */
        auto GetPooledSession(ShaderTarget iTarget) -> core::Result<slang::ISession*> {
            // Key combines type (4 bits) + versionMajor (4 bits) + versionMinor (8 bits)
            auto key = static_cast<uint16_t>(iTarget.type) | (static_cast<uint16_t>(iTarget.versionMajor) << 4)
                       | (static_cast<uint16_t>(iTarget.versionMinor) << 8);
            std::lock_guard lock(sessionPoolMutex);
            auto it = sessionPool.find(key);
            if (it != sessionPool.end()) {
                return it->second.get();
            }

            auto sessionResult = CreateSession(iTarget, {});
            if (!sessionResult) {
                return std::unexpected(sessionResult.error());
            }
            auto [inserted, _] = sessionPool.emplace(key, std::move(*sessionResult));
            return inserted->second.get();
        }

        /** @brief Create a session with custom macros (permutations + defines). Not pooled. */
        auto CreateSessionForDesc(ShaderCompileDesc const& iDesc) -> core::Result<Slang::ComPtr<slang::ISession>> {
            std::vector<slang::PreprocessorMacroDesc> macros;
            std::vector<std::string> permBitValues;

            for (auto const& [key, val] : iDesc.defines) {
                slang::PreprocessorMacroDesc m{};
                m.name = key.c_str();
                m.value = val.c_str();
                macros.push_back(m);
            }

            for (uint32_t bit = 0; bit < 64; ++bit) {
                if (iDesc.permutation.GetBit(bit)) {
                    auto& name = permBitValues.emplace_back("MIKI_PERMUTATION_BIT_" + std::to_string(bit));
                    slang::PreprocessorMacroDesc m{};
                    m.name = name.c_str();
                    m.value = "1";
                    macros.push_back(m);
                }
            }

            // If no custom macros and no permutation bits, use pooled session
            if (macros.empty()) {
                auto pooled = GetPooledSession(iDesc.target);
                if (!pooled) {
                    return std::unexpected(pooled.error());
                }
                // Wrap raw pointer into a new ComPtr with addRef for the caller
                Slang::ComPtr<slang::ISession> result;
                result = *pooled;
                return result;
            }

            return CreateSession(iDesc.target, macros);
        }

        auto CompileToBlob(
            slang::ISession* iSession, std::string const& iSource, std::string const& iSourcePath,
            std::string const& iEntryPoint, ShaderStage iStage, ShaderTarget iTarget
        ) -> core::Result<ShaderBlob> {
            Slang::ComPtr<slang::IBlob> diagnostics;

            auto* module = iSession->loadModuleFromSourceString(
                "mikiShader", iSourcePath.c_str(), iSource.c_str(), diagnostics.writeRef()
            );
            if (!module) {
                RecordBlobDiagnostic(diagnostics.get());
                return std::unexpected(core::ErrorCode::ShaderCompilationFailed);
            }

            Slang::ComPtr<slang::IEntryPoint> entryPoint;
            auto findResult = module->findAndCheckEntryPoint(
                iEntryPoint.c_str(), ToSlangStage(iStage), entryPoint.writeRef(), diagnostics.writeRef()
            );
            if (SLANG_FAILED(findResult)) {
                RecordBlobDiagnostic(diagnostics.get());
                return std::unexpected(core::ErrorCode::ShaderCompilationFailed);
            }

            slang::IComponentType* components[] = {module, entryPoint.get()};
            Slang::ComPtr<slang::IComponentType> composedProgram;
            auto composeResult = iSession->createCompositeComponentType(
                components, 2, composedProgram.writeRef(), diagnostics.writeRef()
            );
            if (SLANG_FAILED(composeResult)) {
                RecordBlobDiagnostic(diagnostics.get());
                return std::unexpected(core::ErrorCode::ShaderCompilationFailed);
            }

            Slang::ComPtr<slang::IComponentType> linkedProgram;
            auto linkResult = composedProgram->link(linkedProgram.writeRef(), diagnostics.writeRef());
            if (SLANG_FAILED(linkResult)) {
                RecordBlobDiagnostic(diagnostics.get());
                return std::unexpected(core::ErrorCode::ShaderCompilationFailed);
            }

            Slang::ComPtr<slang::IBlob> codeBlob;
            auto codeResult = linkedProgram->getEntryPointCode(0, 0, codeBlob.writeRef(), diagnostics.writeRef());
            if (SLANG_FAILED(codeResult)) {
                RecordBlobDiagnostic(diagnostics.get());
                return std::unexpected(core::ErrorCode::ShaderCompilationFailed);
            }

            ShaderBlob blob;
            auto const* data = static_cast<uint8_t const*>(codeBlob->getBufferPointer());
            auto size = codeBlob->getBufferSize();
            blob.data.assign(data, data + size);
            blob.target = iTarget;
            blob.stage = iStage;
            blob.entryPoint = iEntryPoint;

            // Post-process GLSL: Slang outputs Vulkan GLSL builtins, but OpenGL requires different names.
            // gl_VertexIndex -> gl_VertexID, gl_InstanceIndex -> gl_InstanceID
            if (iTarget.type == ShaderTargetType::GLSL) {
                std::string glsl(reinterpret_cast<char const*>(blob.data.data()), blob.data.size());
                bool modified = false;
                // Replace gl_VertexIndex with gl_VertexID
                for (size_t pos = 0; (pos = glsl.find("gl_VertexIndex", pos)) != std::string::npos;) {
                    glsl.replace(pos, 14, "gl_VertexID");
                    pos += 11;
                    modified = true;
                }
                // Replace gl_InstanceIndex with gl_InstanceID
                for (size_t pos = 0; (pos = glsl.find("gl_InstanceIndex", pos)) != std::string::npos;) {
                    glsl.replace(pos, 16, "gl_InstanceID");
                    pos += 13;
                    modified = true;
                }
                if (modified) {
                    blob.data.assign(glsl.begin(), glsl.end());
                }
            }

            return blob;
        }

        // -----------------------------------------------------------------------
        // Reflection helpers
        // -----------------------------------------------------------------------

        static auto ExtractArgValue(slang::UserAttribute* iAttr, unsigned iArgIdx) -> std::optional<std::string> {
            size_t outSize = 0;
            if (const char* str = iAttr->getArgumentValueString(iArgIdx, &outSize)) {
                return std::string(str, outSize);
            }
            int ival;
            if (SLANG_SUCCEEDED(iAttr->getArgumentValueInt(iArgIdx, &ival))) {
                return std::to_string(ival);
            }
            float fval;
            if (SLANG_SUCCEEDED(iAttr->getArgumentValueFloat(iArgIdx, &fval))) {
                return std::to_string(fval);
            }
            return std::nullopt;
        }

        static void ExtractUserAttribs(slang::VariableLayoutReflection* iParam, BindingInfo& oInfo) {
            auto* var = iParam->getVariable();
            if (!var) {
                return;
            }
            auto attrCount = var->getUserAttributeCount();
            for (unsigned a = 0; a < attrCount; ++a) {
                auto* attr = var->getUserAttributeByIndex(a);
                if (!attr) {
                    continue;
                }
                BindingInfo::UserAttrib uattr;
                uattr.name = attr->getName() ? attr->getName() : "";
                auto argCount = attr->getArgumentCount();
                for (unsigned arg = 0; arg < argCount; ++arg) {
                    if (auto val = ExtractArgValue(attr, arg)) {
                        uattr.args.push_back(std::move(*val));
                    }
                }
                oInfo.userAttribs.push_back(std::move(uattr));
            }
        }

        static void ExtractVertexInputs(slang::EntryPointReflection* iEp, ShaderReflection& oReflection) {
            auto epParamCount = iEp->getParameterCount();
            for (unsigned pi = 0; pi < epParamCount; ++pi) {
                auto* epParam = iEp->getParameterByIndex(pi);
                if (!epParam) {
                    continue;
                }
                auto* epTypeLayout = epParam->getTypeLayout();
                if (!epTypeLayout) {
                    continue;
                }
                if (epTypeLayout->getKind() != slang::TypeReflection::Kind::Struct) {
                    continue;
                }

                auto fieldCount = epTypeLayout->getFieldCount();
                for (unsigned fi = 0; fi < fieldCount; ++fi) {
                    auto* field = epTypeLayout->getFieldByIndex(fi);
                    if (!field) {
                        continue;
                    }
                    VertexInputInfo vi;
                    vi.name = field->getName() ? field->getName() : "";
                    vi.location = static_cast<uint32_t>(fi);
                    vi.offset = static_cast<uint32_t>(field->getOffset(slang::ParameterCategory::Uniform));
                    vi.format = SlangTypeToFormat(field->getTypeLayout());
                    oReflection.vertexInputs.push_back(std::move(vi));
                }
            }
        }

        auto ExtractReflection(
            slang::ISession* iSession, ShaderCompileDesc const& iDesc, std::string const& iSource,
            std::string const& iSourcePath
        ) -> core::Result<ShaderReflection> {
            Slang::ComPtr<slang::IBlob> diagnostics;

            auto* module = iSession->loadModuleFromSourceString(
                "mikiReflect", iSourcePath.c_str(), iSource.c_str(), diagnostics.writeRef()
            );
            if (!module) {
                RecordBlobDiagnostic(diagnostics.get());
                return std::unexpected(core::ErrorCode::ShaderCompilationFailed);
            }

            ShaderReflection reflection;
            auto* moduleRoot = module->getModuleReflection();
            if (!moduleRoot) {
                return std::unexpected(core::ErrorCode::ShaderCompilationFailed);
            }
            CollectModuleConstantVarsRecursive(moduleRoot, reflection.moduleConstants);

            Slang::ComPtr<slang::IEntryPoint> entryPoint;
            auto findResult = module->findAndCheckEntryPoint(
                iDesc.entryPoint.c_str(), ToSlangStage(iDesc.stage), entryPoint.writeRef(), diagnostics.writeRef()
            );
            if (SLANG_FAILED(findResult)) {
                RecordBlobDiagnostic(diagnostics.get());
                return std::unexpected(core::ErrorCode::ShaderCompilationFailed);
            }

            slang::IComponentType* components[] = {module, entryPoint.get()};
            Slang::ComPtr<slang::IComponentType> composedProgram;
            auto composeResult = iSession->createCompositeComponentType(
                components, 2, composedProgram.writeRef(), diagnostics.writeRef()
            );
            if (SLANG_FAILED(composeResult) || !composedProgram) {
                RecordBlobDiagnostic(diagnostics.get());
                return std::unexpected(core::ErrorCode::ShaderCompilationFailed);
            }

            Slang::ComPtr<slang::IComponentType> linkedProgram;
            auto linkResult = composedProgram->link(linkedProgram.writeRef(), diagnostics.writeRef());
            if (SLANG_FAILED(linkResult) || !linkedProgram) {
                RecordBlobDiagnostic(diagnostics.get());
                return std::unexpected(core::ErrorCode::ShaderCompilationFailed);
            }

            auto* layout = linkedProgram->getLayout(0, diagnostics.writeRef());
            if (!layout) {
                return std::unexpected(core::ErrorCode::ShaderCompilationFailed);
            }

            // Extract bindings
            auto paramCount = layout->getParameterCount();
            for (unsigned i = 0; i < paramCount; ++i) {
                auto* param = layout->getParameterByIndex(i);
                if (!param) {
                    continue;
                }
                auto* typeLayout = param->getTypeLayout();
                if (!typeLayout) {
                    continue;
                }

                BindingInfo info;
                info.name = param->getName() ? param->getName() : "";
                info.set = static_cast<uint32_t>(param->getBindingSpace());
                info.binding = static_cast<uint32_t>(param->getBindingIndex());
                ExtractUserAttribs(param, info);

                auto kind = typeLayout->getKind();
                switch (kind) {
                    case slang::TypeReflection::Kind::ConstantBuffer: info.type = BindingType::UniformBuffer; break;
                    case slang::TypeReflection::Kind::Resource: info.type = BindingType::SampledTexture; break;
                    case slang::TypeReflection::Kind::SamplerState: info.type = BindingType::Sampler; break;
                    case slang::TypeReflection::Kind::ShaderStorageBuffer:
                        info.type = BindingType::StorageBuffer;
                        break;
                    default: info.type = BindingType::UniformBuffer; break;
                }

                auto* type = typeLayout->getType();
                if (type && type->isArray()) {
                    auto elemCount = type->getElementCount();
                    info.count
                        = (elemCount != 0 && elemCount != SLANG_UNBOUNDED_SIZE) ? static_cast<uint32_t>(elemCount) : 1;
                } else {
                    info.count = 1;
                }
                reflection.bindings.push_back(std::move(info));
            }

            // Extract entry point info
            auto* ep = (layout->getEntryPointCount() > 0) ? layout->getEntryPointByIndex(0) : nullptr;
            if (ep) {
                SlangUInt sizes[3] = {0, 0, 0};
                ep->getComputeThreadGroupSize(3, sizes);
                reflection.threadGroupSize[0] = static_cast<uint32_t>(sizes[0]);
                reflection.threadGroupSize[1] = static_cast<uint32_t>(sizes[1]);
                reflection.threadGroupSize[2] = static_cast<uint32_t>(sizes[2]);

                if (iDesc.stage == ShaderStage::Vertex) {
                    ExtractVertexInputs(ep, reflection);
                }
            }

            reflection.pushConstantSize = static_cast<uint32_t>(layout->getGlobalConstantBufferSize());

            // Struct layouts from AST
            std::unordered_set<std::string> structNames;
            CollectStructDeclNamesRecursive(moduleRoot, structNames);
            std::vector<std::string> sortedStructNames(structNames.begin(), structNames.end());
            std::ranges::sort(sortedStructNames);
            for (auto const& structName : sortedStructNames) {
                (void)TryAppendStructLayout(layout, structName, reflection);
            }

            return reflection;
        }
    };

    // ===========================================================================
    // Public API
    // ===========================================================================

    SlangCompiler::SlangCompiler(std::unique_ptr<Impl> iImpl) : impl_(std::move(iImpl)) {}
    SlangCompiler::~SlangCompiler() = default;
    SlangCompiler::SlangCompiler(SlangCompiler&&) noexcept = default;
    auto SlangCompiler::operator=(SlangCompiler&&) noexcept -> SlangCompiler& = default;

    auto SlangCompiler::Create() -> core::Result<SlangCompiler> {
        auto impl = std::make_unique<Impl>();
        auto result = slang::createGlobalSession(impl->globalSession.writeRef());
        if (SLANG_FAILED(result)) {
            return std::unexpected(core::ErrorCode::ShaderCompilationFailed);
        }
        return SlangCompiler(std::move(impl));
    }

    auto SlangCompiler::Compile(ShaderCompileDesc const& iDesc) -> core::Result<ShaderBlob> {
        impl_->lastDiagnostics.clear();
        auto sessionResult = impl_->CreateSessionForDesc(iDesc);
        if (!sessionResult) {
            return std::unexpected(sessionResult.error());
        }

        std::string source;
        if (!iDesc.sourceCode.empty()) {
            source = iDesc.sourceCode;
        } else {
            source = ReadFileToString(iDesc.sourcePath);
            if (source.empty()) {
                return std::unexpected(core::ErrorCode::IoError);
            }
        }

        auto pathStr = iDesc.sourcePath.empty() ? std::string("<embedded>") : iDesc.sourcePath.string();
        return impl_->CompileToBlob(sessionResult->get(), source, pathStr, iDesc.entryPoint, iDesc.stage, iDesc.target);
    }

    auto SlangCompiler::CompileDualTarget(
        std::filesystem::path const& iSourcePath, std::string const& iEntryPoint, ShaderStage iStage
    ) -> core::Result<std::pair<ShaderBlob, ShaderBlob>> {
        impl_->lastDiagnostics.clear();
        auto source = ReadFileToString(iSourcePath);
        if (source.empty()) {
            return std::unexpected(core::ErrorCode::IoError);
        }
        auto pathStr = iSourcePath.string();

        auto spirvTarget = ShaderTarget::SPIRV_1_5();
        auto spirvSession = impl_->GetPooledSession(spirvTarget);
        if (!spirvSession) {
            return std::unexpected(spirvSession.error());
        }
        auto spirvBlob = impl_->CompileToBlob(*spirvSession, source, pathStr, iEntryPoint, iStage, spirvTarget);
        if (!spirvBlob) {
            return std::unexpected(spirvBlob.error());
        }

        auto dxilTarget = ShaderTarget::DXIL_6_6();
        auto dxilSession = impl_->GetPooledSession(dxilTarget);
        if (!dxilSession) {
            return std::unexpected(dxilSession.error());
        }
        auto dxilBlob = impl_->CompileToBlob(*dxilSession, source, pathStr, iEntryPoint, iStage, dxilTarget);
        if (!dxilBlob) {
            return std::unexpected(dxilBlob.error());
        }

        return std::make_pair(std::move(*spirvBlob), std::move(*dxilBlob));
    }

    auto SlangCompiler::CompileQuadTarget(
        std::filesystem::path const& iSourcePath, std::string const& iEntryPoint, ShaderStage iStage
    ) -> core::Result<std::array<ShaderBlob, kTargetCount>> {
        impl_->lastDiagnostics.clear();
        auto source = ReadFileToString(iSourcePath);
        if (source.empty()) {
            return std::unexpected(core::ErrorCode::IoError);
        }
        auto pathStr = iSourcePath.string();

        static constexpr std::array<ShaderTarget, kTargetCount> kTargets
            = {ShaderTarget::SPIRV_1_5(), ShaderTarget::DXIL_6_6(), ShaderTarget::GLSL_430(), ShaderTarget::WGSL_1_0()};

        std::array<ShaderBlob, kTargetCount> blobs;
        for (size_t i = 0; i < kTargetCount; ++i) {
            auto session = impl_->GetPooledSession(kTargets[i]);
            if (!session) {
                return std::unexpected(session.error());
            }
            auto blob = impl_->CompileToBlob(*session, source, pathStr, iEntryPoint, iStage, kTargets[i]);
            if (!blob) {
                return std::unexpected(blob.error());
            }
            blobs[i] = std::move(*blob);
        }
        return blobs;
    }

    auto SlangCompiler::Reflect(ShaderCompileDesc const& iDesc) -> core::Result<ShaderReflection> {
        impl_->lastDiagnostics.clear();
        auto sessionResult = impl_->CreateSessionForDesc(iDesc);
        if (!sessionResult) {
            return std::unexpected(sessionResult.error());
        }

        auto source = ReadFileToString(iDesc.sourcePath);
        if (source.empty()) {
            return std::unexpected(core::ErrorCode::IoError);
        }

        auto pathStr = iDesc.sourcePath.string();
        return impl_->ExtractReflection(sessionResult->get(), iDesc, source, pathStr);
    }

    auto SlangCompiler::AddSearchPath(std::filesystem::path const& iPath) -> void {
        impl_->searchPaths.push_back(iPath.string());
        InvalidateSessionCache();
    }

    auto SlangCompiler::GetLastDiagnostics() const -> std::span<const ShaderDiagnostic> {
        return impl_->lastDiagnostics;
    }

    auto SlangCompiler::InvalidateSessionCache() -> void {
        std::lock_guard lock(impl_->sessionPoolMutex);
        impl_->sessionPool.clear();
    }

}  // namespace miki::shader
