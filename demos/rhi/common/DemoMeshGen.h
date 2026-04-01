/** @file DemoMeshGen.h
 *  @brief CPU-side parametric mesh generators for RHI demos.
 *
 *  Header-only. All generators return DemoMeshData with interleaved position + normal.
 *  Uses miki::core::float3 for GPU-compatible layout.
 */
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace miki::demo {

// ============================================================================
// Vertex and mesh types
// ============================================================================

struct DemoVertex {
    float px, py, pz;
    float nx, ny, nz;
};
static_assert(sizeof(DemoVertex) == 24);

struct DemoMeshData {
    std::vector<DemoVertex> vertices;
    std::vector<uint32_t> indices;
};

// ============================================================================
// Torus: (R + r*cos(v)) * cos(u), r*sin(v), (R + r*cos(v)) * sin(u)
// ============================================================================

[[nodiscard]] inline auto GenerateTorus(float R, float r, uint32_t rings, uint32_t segments) -> DemoMeshData {
    DemoMeshData mesh;
    constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
    uint32_t vertCount = (rings + 1) * (segments + 1);
    mesh.vertices.reserve(vertCount);
    for (uint32_t i = 0; i <= rings; ++i) {
        float u = static_cast<float>(i) / static_cast<float>(rings) * kTwoPi;
        float cu = std::cos(u), su = std::sin(u);
        for (uint32_t j = 0; j <= segments; ++j) {
            float v = static_cast<float>(j) / static_cast<float>(segments) * kTwoPi;
            float cv = std::cos(v), sv = std::sin(v);
            float x = (R + r * cv) * cu;
            float y = r * sv;
            float z = (R + r * cv) * su;
            mesh.vertices.push_back({x, y, z, cv * cu, sv, cv * su});
        }
    }
    mesh.indices.reserve(rings * segments * 6);
    for (uint32_t i = 0; i < rings; ++i) {
        for (uint32_t j = 0; j < segments; ++j) {
            uint32_t a = i * (segments + 1) + j;
            uint32_t b = a + segments + 1;
            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
            mesh.indices.push_back(a + 1);
            mesh.indices.push_back(a + 1);
            mesh.indices.push_back(b);
            mesh.indices.push_back(b + 1);
        }
    }
    return mesh;
}

// ============================================================================
// Cube: 6 faces, 4 vertices each, unique normals per face
// ============================================================================

[[nodiscard]] inline auto GenerateCube(float halfSize) -> DemoMeshData {
    DemoMeshData mesh;
    float h = halfSize;
    // clang-format off
    std::array<DemoVertex, 24> verts = {{
        // +Z
        {-h,-h, h,  0, 0, 1}, { h,-h, h,  0, 0, 1}, { h, h, h,  0, 0, 1}, {-h, h, h,  0, 0, 1},
        // -Z
        { h,-h,-h,  0, 0,-1}, {-h,-h,-h,  0, 0,-1}, {-h, h,-h,  0, 0,-1}, { h, h,-h,  0, 0,-1},
        // +X
        { h,-h, h,  1, 0, 0}, { h,-h,-h,  1, 0, 0}, { h, h,-h,  1, 0, 0}, { h, h, h,  1, 0, 0},
        // -X
        {-h,-h,-h, -1, 0, 0}, {-h,-h, h, -1, 0, 0}, {-h, h, h, -1, 0, 0}, {-h, h,-h, -1, 0, 0},
        // +Y
        {-h, h, h,  0, 1, 0}, { h, h, h,  0, 1, 0}, { h, h,-h,  0, 1, 0}, {-h, h,-h,  0, 1, 0},
        // -Y
        {-h,-h,-h,  0,-1, 0}, { h,-h,-h,  0,-1, 0}, { h,-h, h,  0,-1, 0}, {-h,-h, h,  0,-1, 0},
    }};
    // clang-format on
    mesh.vertices.assign(verts.begin(), verts.end());
    mesh.indices.reserve(36);
    for (uint32_t face = 0; face < 6; ++face) {
        uint32_t base = face * 4;
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 3);
    }
    return mesh;
}

// ============================================================================
// UV Sphere: latitude/longitude tessellation
// ============================================================================

[[nodiscard]] inline auto GenerateUVSphere(float radius, uint32_t stacks, uint32_t sectors) -> DemoMeshData {
    DemoMeshData mesh;
    constexpr float kPi = std::numbers::pi_v<float>;
    constexpr float kTwoPi = 2.0f * kPi;
    mesh.vertices.reserve((stacks + 1) * (sectors + 1));
    for (uint32_t i = 0; i <= stacks; ++i) {
        float phi = kPi * static_cast<float>(i) / static_cast<float>(stacks);
        float sp = std::sin(phi), cp = std::cos(phi);
        for (uint32_t j = 0; j <= sectors; ++j) {
            float theta = kTwoPi * static_cast<float>(j) / static_cast<float>(sectors);
            float st = std::sin(theta), ct = std::cos(theta);
            float nx = sp * ct, ny = cp, nz = sp * st;
            mesh.vertices.push_back({radius * nx, radius * ny, radius * nz, nx, ny, nz});
        }
    }
    mesh.indices.reserve(stacks * sectors * 6);
    for (uint32_t i = 0; i < stacks; ++i) {
        for (uint32_t j = 0; j < sectors; ++j) {
            uint32_t a = i * (sectors + 1) + j;
            uint32_t b = a + sectors + 1;
            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
            mesh.indices.push_back(a + 1);
            mesh.indices.push_back(a + 1);
            mesh.indices.push_back(b);
            mesh.indices.push_back(b + 1);
        }
    }
    return mesh;
}

// ============================================================================
// Cylinder: open-ended cylinder along Y axis
// ============================================================================

[[nodiscard]] inline auto GenerateCylinder(float radius, float height, uint32_t segments) -> DemoMeshData {
    DemoMeshData mesh;
    constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
    float halfH = height * 0.5f;
    // Side vertices: 2 rings
    mesh.vertices.reserve((segments + 1) * 2 + (segments + 1) * 2);
    for (uint32_t ring = 0; ring < 2; ++ring) {
        float y = (ring == 0) ? -halfH : halfH;
        for (uint32_t j = 0; j <= segments; ++j) {
            float theta = kTwoPi * static_cast<float>(j) / static_cast<float>(segments);
            float ct = std::cos(theta), st = std::sin(theta);
            mesh.vertices.push_back({radius * ct, y, radius * st, ct, 0.0f, st});
        }
    }
    // Side indices
    mesh.indices.reserve(segments * 6 + segments * 3 * 2);
    uint32_t stride = segments + 1;
    for (uint32_t j = 0; j < segments; ++j) {
        uint32_t a = j, b = j + stride;
        mesh.indices.push_back(a);
        mesh.indices.push_back(b);
        mesh.indices.push_back(a + 1);
        mesh.indices.push_back(a + 1);
        mesh.indices.push_back(b);
        mesh.indices.push_back(b + 1);
    }
    // Top cap
    uint32_t topCenter = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({0, halfH, 0, 0, 1, 0});
    for (uint32_t j = 0; j <= segments; ++j) {
        float theta = kTwoPi * static_cast<float>(j) / static_cast<float>(segments);
        mesh.vertices.push_back({radius * std::cos(theta), halfH, radius * std::sin(theta), 0, 1, 0});
    }
    for (uint32_t j = 0; j < segments; ++j) {
        mesh.indices.push_back(topCenter);
        mesh.indices.push_back(topCenter + 1 + j);
        mesh.indices.push_back(topCenter + 2 + j);
    }
    // Bottom cap
    uint32_t botCenter = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({0, -halfH, 0, 0, -1, 0});
    for (uint32_t j = 0; j <= segments; ++j) {
        float theta = kTwoPi * static_cast<float>(j) / static_cast<float>(segments);
        mesh.vertices.push_back({radius * std::cos(theta), -halfH, radius * std::sin(theta), 0, -1, 0});
    }
    for (uint32_t j = 0; j < segments; ++j) {
        mesh.indices.push_back(botCenter);
        mesh.indices.push_back(botCenter + 2 + j);
        mesh.indices.push_back(botCenter + 1 + j);
    }
    return mesh;
}

// ============================================================================
// Cone: apex at top, base at bottom, along Y axis
// ============================================================================

[[nodiscard]] inline auto GenerateCone(float radius, float height, uint32_t segments) -> DemoMeshData {
    DemoMeshData mesh;
    constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
    float halfH = height * 0.5f;
    float slopeLen = std::sqrt(radius * radius + height * height);
    float ny = radius / slopeLen;
    float nr = height / slopeLen;
    // Side: apex + ring
    uint32_t apexIdx = 0;
    mesh.vertices.push_back({0, halfH, 0, 0, 1, 0}); // apex placeholder normal
    for (uint32_t j = 0; j <= segments; ++j) {
        float theta = kTwoPi * static_cast<float>(j) / static_cast<float>(segments);
        float ct = std::cos(theta), st = std::sin(theta);
        mesh.vertices.push_back({radius * ct, -halfH, radius * st, nr * ct, ny, nr * st});
    }
    for (uint32_t j = 0; j < segments; ++j) {
        mesh.indices.push_back(apexIdx);
        mesh.indices.push_back(1 + j);
        mesh.indices.push_back(2 + j);
    }
    // Bottom cap
    uint32_t botCenter = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({0, -halfH, 0, 0, -1, 0});
    for (uint32_t j = 0; j <= segments; ++j) {
        float theta = kTwoPi * static_cast<float>(j) / static_cast<float>(segments);
        mesh.vertices.push_back({radius * std::cos(theta), -halfH, radius * std::sin(theta), 0, -1, 0});
    }
    for (uint32_t j = 0; j < segments; ++j) {
        mesh.indices.push_back(botCenter);
        mesh.indices.push_back(botCenter + 2 + j);
        mesh.indices.push_back(botCenter + 1 + j);
    }
    return mesh;
}

// ============================================================================
// Plane: XZ plane centered at origin, Y=0, normal (0,1,0)
// ============================================================================

[[nodiscard]] inline auto GeneratePlane(float width, float depth, uint32_t subdivX, uint32_t subdivZ) -> DemoMeshData {
    DemoMeshData mesh;
    float halfW = width * 0.5f, halfD = depth * 0.5f;
    mesh.vertices.reserve((subdivX + 1) * (subdivZ + 1));
    for (uint32_t iz = 0; iz <= subdivZ; ++iz) {
        float z = -halfD + depth * static_cast<float>(iz) / static_cast<float>(subdivZ);
        for (uint32_t ix = 0; ix <= subdivX; ++ix) {
            float x = -halfW + width * static_cast<float>(ix) / static_cast<float>(subdivX);
            mesh.vertices.push_back({x, 0, z, 0, 1, 0});
        }
    }
    mesh.indices.reserve(subdivX * subdivZ * 6);
    for (uint32_t iz = 0; iz < subdivZ; ++iz) {
        for (uint32_t ix = 0; ix < subdivX; ++ix) {
            uint32_t a = iz * (subdivX + 1) + ix;
            uint32_t b = a + subdivX + 1;
            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
            mesh.indices.push_back(a + 1);
            mesh.indices.push_back(a + 1);
            mesh.indices.push_back(b);
            mesh.indices.push_back(b + 1);
        }
    }
    return mesh;
}

// ============================================================================
// Icosphere: subdivided icosahedron
// ============================================================================

[[nodiscard]] inline auto GenerateIcosphere(float radius, uint32_t subdivisions) -> DemoMeshData {
    DemoMeshData mesh;
    // Start with icosahedron vertices
    constexpr float kPhi = 1.6180339887498949f; // golden ratio
    float a = 1.0f, b = kPhi;
    float invLen = 1.0f / std::sqrt(a * a + b * b);
    a *= invLen; b *= invLen;
    // clang-format off
    std::vector<std::array<float, 3>> positions = {
        {-a,  b,  0}, { a,  b,  0}, {-a, -b,  0}, { a, -b,  0},
        { 0, -a,  b}, { 0,  a,  b}, { 0, -a, -b}, { 0,  a, -b},
        { b,  0, -a}, { b,  0,  a}, {-b,  0, -a}, {-b,  0,  a},
    };
    std::vector<std::array<uint32_t, 3>> triangles = {
        {0,11,5}, {0,5,1}, {0,1,7}, {0,7,10}, {0,10,11},
        {1,5,9}, {5,11,4}, {11,10,2}, {10,7,6}, {7,1,8},
        {3,9,4}, {3,4,2}, {3,2,6}, {3,6,8}, {3,8,9},
        {4,9,5}, {2,4,11}, {6,2,10}, {8,6,7}, {9,8,1},
    };
    // clang-format on
    // Subdivision
    auto midpoint = [&](uint32_t i0, uint32_t i1) -> uint32_t {
        auto& p0 = positions[i0];
        auto& p1 = positions[i1];
        float mx = (p0[0] + p1[0]) * 0.5f;
        float my = (p0[1] + p1[1]) * 0.5f;
        float mz = (p0[2] + p1[2]) * 0.5f;
        float len = std::sqrt(mx * mx + my * my + mz * mz);
        float il = 1.0f / len;
        positions.push_back({mx * il, my * il, mz * il});
        return static_cast<uint32_t>(positions.size() - 1);
    };
    for (uint32_t s = 0; s < subdivisions; ++s) {
        std::vector<std::array<uint32_t, 3>> newTris;
        newTris.reserve(triangles.size() * 4);
        for (auto& tri : triangles) {
            uint32_t a0 = midpoint(tri[0], tri[1]);
            uint32_t b0 = midpoint(tri[1], tri[2]);
            uint32_t c0 = midpoint(tri[2], tri[0]);
            newTris.push_back({tri[0], a0, c0});
            newTris.push_back({tri[1], b0, a0});
            newTris.push_back({tri[2], c0, b0});
            newTris.push_back({a0, b0, c0});
        }
        triangles = std::move(newTris);
    }
    mesh.vertices.reserve(positions.size());
    for (auto& p : positions) {
        mesh.vertices.push_back({radius * p[0], radius * p[1], radius * p[2], p[0], p[1], p[2]});
    }
    mesh.indices.reserve(triangles.size() * 3);
    for (auto& tri : triangles) {
        mesh.indices.push_back(tri[0]);
        mesh.indices.push_back(tri[1]);
        mesh.indices.push_back(tri[2]);
    }
    return mesh;
}

}  // namespace miki::demo
