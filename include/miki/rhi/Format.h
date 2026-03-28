/** @brief Pixel format enum and format info queries for the miki RHI.
 *
 * Comprehensive pixel format list mapping 1:1 to VkFormat / DXGI_FORMAT /
 * GL internal format / WGSL format. Conversion functions are backend-internal.
 */
#pragma once

#include <cstdint>

namespace miki::rhi {

    /** @brief Pixel format enum.
     *
     * Covers common color, depth, stencil, and compressed formats.
     * Format::Undefined == 0 is the sentinel/invalid value.
     */
    enum class Format : uint32_t {
        Undefined = 0,

        // --- 8-bit per channel ---
        R8_UNORM,
        R8_SNORM,
        R8_UINT,
        R8_SINT,
        RG8_UNORM,
        RG8_SNORM,
        RG8_UINT,
        RG8_SINT,
        RGBA8_UNORM,
        RGBA8_SNORM,
        RGBA8_UINT,
        RGBA8_SINT,
        RGBA8_SRGB,
        BGRA8_UNORM,
        BGRA8_SRGB,

        // --- 16-bit per channel ---
        R16_UNORM,
        R16_SNORM,
        R16_UINT,
        R16_SINT,
        R16_FLOAT,
        RG16_UNORM,
        RG16_SNORM,
        RG16_UINT,
        RG16_SINT,
        RG16_FLOAT,
        RGBA16_UNORM,
        RGBA16_SNORM,
        RGBA16_UINT,
        RGBA16_SINT,
        RGBA16_FLOAT,

        // --- 32-bit per channel ---
        R32_UINT,
        R32_SINT,
        R32_FLOAT,
        RG32_UINT,
        RG32_SINT,
        RG32_FLOAT,
        RGB32_UINT,
        RGB32_SINT,
        RGB32_FLOAT,
        RGBA32_UINT,
        RGBA32_SINT,
        RGBA32_FLOAT,

        // --- Packed ---
        RGB10A2_UNORM,
        RG11B10_FLOAT,

        // --- Depth / Stencil ---
        D16_UNORM,
        D32_FLOAT,
        D24_UNORM_S8_UINT,
        D32_FLOAT_S8_UINT,

        // --- Block-compressed ---
        BC1_UNORM,
        BC1_SRGB,
        BC2_UNORM,
        BC2_SRGB,
        BC3_UNORM,
        BC3_SRGB,
        BC4_UNORM,
        BC4_SNORM,
        BC5_UNORM,
        BC5_SNORM,
        BC6H_UFLOAT,
        BC6H_SFLOAT,
        BC7_UNORM,
        BC7_SRGB,

        // --- ASTC (mobile/WebGPU) ---
        ASTC_4x4_UNORM,
        ASTC_4x4_SRGB,

        Count_
    };

    /** @brief Descriptor of a pixel format's properties. */
    struct FormatDesc {
        uint8_t bytesPerPixel = 0;
        uint8_t blockSize = 1;
        uint8_t channelCount = 0;
        bool isCompressed = false;
        bool isDepth = false;
        bool isStencil = false;
    };

    /** @brief Query properties of a pixel format.
     *  @param iFmt The format to query.
     *  @return A FormatDesc describing the format's properties.
     */
    [[nodiscard]] constexpr auto FormatInfo(Format iFmt) -> FormatDesc {
        switch (iFmt) {
            // 8-bit single channel
            case Format::R8_UNORM:
            case Format::R8_SNORM:
            case Format::R8_UINT:
            case Format::R8_SINT: return {1, 1, 1, false, false, false};

            // 8-bit two channel
            case Format::RG8_UNORM:
            case Format::RG8_SNORM:
            case Format::RG8_UINT:
            case Format::RG8_SINT: return {2, 1, 2, false, false, false};

            // 8-bit four channel
            case Format::RGBA8_UNORM:
            case Format::RGBA8_SNORM:
            case Format::RGBA8_UINT:
            case Format::RGBA8_SINT:
            case Format::RGBA8_SRGB:
            case Format::BGRA8_UNORM:
            case Format::BGRA8_SRGB: return {4, 1, 4, false, false, false};

            // 16-bit single channel
            case Format::R16_UNORM:
            case Format::R16_SNORM:
            case Format::R16_UINT:
            case Format::R16_SINT:
            case Format::R16_FLOAT: return {2, 1, 1, false, false, false};

            // 16-bit two channel
            case Format::RG16_UNORM:
            case Format::RG16_SNORM:
            case Format::RG16_UINT:
            case Format::RG16_SINT:
            case Format::RG16_FLOAT: return {4, 1, 2, false, false, false};

            // 16-bit four channel
            case Format::RGBA16_UNORM:
            case Format::RGBA16_SNORM:
            case Format::RGBA16_UINT:
            case Format::RGBA16_SINT:
            case Format::RGBA16_FLOAT: return {8, 1, 4, false, false, false};

            // 32-bit single channel
            case Format::R32_UINT:
            case Format::R32_SINT:
            case Format::R32_FLOAT: return {4, 1, 1, false, false, false};

            // 32-bit two channel
            case Format::RG32_UINT:
            case Format::RG32_SINT:
            case Format::RG32_FLOAT: return {8, 1, 2, false, false, false};

            // 32-bit three channel
            case Format::RGB32_UINT:
            case Format::RGB32_SINT:
            case Format::RGB32_FLOAT: return {12, 1, 3, false, false, false};

            // 32-bit four channel
            case Format::RGBA32_UINT:
            case Format::RGBA32_SINT:
            case Format::RGBA32_FLOAT: return {16, 1, 4, false, false, false};

            // Packed
            case Format::RGB10A2_UNORM: return {4, 1, 4, false, false, false};
            case Format::RG11B10_FLOAT: return {4, 1, 3, false, false, false};

            // Depth / Stencil
            case Format::D16_UNORM: return {2, 1, 1, false, true, false};
            case Format::D32_FLOAT: return {4, 1, 1, false, true, false};
            case Format::D24_UNORM_S8_UINT: return {4, 1, 2, false, true, true};
            case Format::D32_FLOAT_S8_UINT: return {8, 1, 2, false, true, true};

            // BC1: 8 bytes per 4x4 block
            case Format::BC1_UNORM:
            case Format::BC1_SRGB: return {8, 4, 4, true, false, false};

            // BC2/BC3: 16 bytes per 4x4 block
            case Format::BC2_UNORM:
            case Format::BC2_SRGB:
            case Format::BC3_UNORM:
            case Format::BC3_SRGB: return {16, 4, 4, true, false, false};

            // BC4: 8 bytes per 4x4 block, 1 channel
            case Format::BC4_UNORM:
            case Format::BC4_SNORM: return {8, 4, 1, true, false, false};

            // BC5: 16 bytes per 4x4 block, 2 channels
            case Format::BC5_UNORM:
            case Format::BC5_SNORM: return {16, 4, 2, true, false, false};

            // BC6H: 16 bytes per 4x4 block, 3 channels
            case Format::BC6H_UFLOAT:
            case Format::BC6H_SFLOAT: return {16, 4, 3, true, false, false};

            // BC7: 16 bytes per 4x4 block, 4 channels
            case Format::BC7_UNORM:
            case Format::BC7_SRGB: return {16, 4, 4, true, false, false};

            // ASTC 4x4: 16 bytes per 4x4 block, 4 channels
            case Format::ASTC_4x4_UNORM:
            case Format::ASTC_4x4_SRGB: return {16, 4, 4, true, false, false};

            case Format::Undefined:
            case Format::Count_:
            default: return {};
        }
    }

    /** @brief Bytes per pixel for a given format (0 for Undefined/compressed). */
    [[nodiscard]] constexpr auto FormatBytesPerPixel(Format iFmt) -> uint32_t {
        return FormatInfo(iFmt).bytesPerPixel;
    }

    /** @brief Align a byte row pitch to the given alignment (must be power of 2).
     *  WebGPU and D3D12 require 256-byte row alignment for texture copies.
     */
    [[nodiscard]] constexpr auto AlignRowPitch(uint32_t iBytesPerRow, uint32_t iAlignment = 256) -> uint32_t {
        return (iBytesPerRow + iAlignment - 1) & ~(iAlignment - 1);
    }

}  // namespace miki::rhi
