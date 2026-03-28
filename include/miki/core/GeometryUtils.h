/** @brief Geometry utility functions for Types.h primitives (AABB, Ray, Frustum).
 *
 * Header-only, inline. All functions operate on miki::core geometry types. Consumed by BVH, Octree, GPU culling,
 * picking, and any spatial query code.
 *
 * Namespace: miki::core
 */
#pragma once

#include "miki/core/Types.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace miki::core {

    // ─────────────────────────────────────────────────────────────────────────────
    // AABB operations
    // ─────────────────────────────────────────────────────────────────────────────

    /** @brief Union of two AABBs (smallest AABB enclosing both). */
    [[nodiscard]] inline auto UnionAABB(AABB const& a, AABB const& b) -> AABB {
        return AABB{
            .min = {.x = std::min(a.min.x, b.min.x), .y = std::min(a.min.y, b.min.y), .z = std::min(a.min.z, b.min.z)},
            .max = {.x = std::max(a.max.x, b.max.x), .y = std::max(a.max.y, b.max.y), .z = std::max(a.max.z, b.max.z)}
        };
    }

    /** @brief Surface area of an AABB (used by SAH cost evaluation). */
    [[nodiscard]] inline auto SurfaceArea(AABB const& box) -> float {
        float dx = box.max.x - box.min.x;
        float dy = box.max.y - box.min.y;
        float dz = box.max.z - box.min.z;
        return 2.0f * (dx * dy + dy * dz + dz * dx);
    }

    /** @brief Centroid of an AABB. */
    [[nodiscard]] inline auto Centroid(AABB const& box) -> float3 {
        return float3{
            .x = 0.5f * (box.min.x + box.max.x),
            .y = 0.5f * (box.min.y + box.max.y),
            .z = 0.5f * (box.min.z + box.max.z)
        };
    }

    /** @brief Extents (half-size per axis) of an AABB. */
    [[nodiscard]] inline auto Extents(AABB const& box) -> float3 {
        return float3{
            .x = 0.5f * (box.max.x - box.min.x),
            .y = 0.5f * (box.max.y - box.min.y),
            .z = 0.5f * (box.max.z - box.min.z)
        };
    }

    /** @brief Test if two AABBs overlap. */
    [[nodiscard]] inline auto AABBIntersectsAABB(AABB const& a, AABB const& b) -> bool {
        return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y
               && a.min.z <= b.max.z && a.max.z >= b.min.z;
    }

    /** @brief Test if a point is inside an AABB (inclusive). */
    [[nodiscard]] inline auto AABBContainsPoint(AABB const& box, float3 const& p) -> bool {
        return p.x >= box.min.x && p.x <= box.max.x && p.y >= box.min.y && p.y <= box.max.y && p.z >= box.min.z
               && p.z <= box.max.z;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Frustum tests
    // ─────────────────────────────────────────────────────────────────────────────

    /** @brief Test AABB against frustum (6 planes). Conservative: may return true for near-misses.
     *  Plane equation: dot(normal, point) + w >= 0 means inside. */
    [[nodiscard]] inline auto AABBIntersectsFrustum(AABB const& box, FrustumPlanes const& frustum) -> bool {
        for (int i = 0; i < 6; ++i) {
            auto const& p = frustum.planes[i];
            float px = (p.x >= 0.0f) ? box.max.x : box.min.x;
            float py = (p.y >= 0.0f) ? box.max.y : box.min.y;
            float pz = (p.z >= 0.0f) ? box.max.z : box.min.z;
            float dot = px * p.x + py * p.y + pz * p.z + p.w;
            if (dot < 0.0f) {
                return false;
            }
        }
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Point-AABB distance
    // ─────────────────────────────────────────────────────────────────────────────

    /** @brief Squared distance from a point to the nearest point on an AABB surface.
     *  Returns 0 if the point is inside or on the AABB. Used by nearest-neighbor queries. */
    [[nodiscard]] inline auto PointAABBDistSq(float3 const& p, AABB const& box) -> float {
        float dx = std::max(std::max(box.min.x - p.x, p.x - box.max.x), 0.0f);
        float dy = std::max(std::max(box.min.y - p.y, p.y - box.max.y), 0.0f);
        float dz = std::max(std::max(box.min.z - p.z, p.z - box.max.z), 0.0f);
        return dx * dx + dy * dy + dz * dz;
    }

    /** @brief Index of the largest extent axis (0=x, 1=y, 2=z). */
    [[nodiscard]] inline auto LargestAxis(AABB const& box) -> uint32_t {
        float dx = box.max.x - box.min.x;
        float dy = box.max.y - box.min.y;
        float dz = box.max.z - box.min.z;
        if (dx >= dy && dx >= dz) {
            return 0;
        }
        if (dy >= dz) {
            return 1;
        }
        return 2;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Ray precomputation
    // ─────────────────────────────────────────────────────────────────────────────

    /** @brief Precomputed ray data for fast BVH traversal.
     *  Compute once per ray at traversal entry, reuse for all node tests.
     *  invDir handles dir≈0 via copysign(1e20f) — IEEE 754 guarantees correct slab test. */
    struct RayPrecomp {
        float3 origin;
        float3 invDir;
        uint32_t dirSign[3];  // 1 if direction < 0, 0 otherwise
    };

    /** @brief Prepare a ray for fast traversal. O(1), call once per ray. */
    [[nodiscard]] inline auto PrepareRay(Ray const& ray) -> RayPrecomp {
        RayPrecomp r;
        r.origin = ray.origin;
        for (int i = 0; i < 3; ++i) {
            float d = (&ray.direction.x)[i];
            (&r.invDir.x)[i] = (std::abs(d) < 1e-20f) ? std::copysign(1e20f, d) : 1.0f / d;
            r.dirSign[i] = (d < 0.0f) ? 1u : 0u;
        }
        return r;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Ray-AABB intersection tests
    // ─────────────────────────────────────────────────────────────────────────────

    /** @brief Tavian Barnes 2022 NaN-safe, boundary-inclusive ray-AABB slab test.
     *
     *  Uses precomputed invDir. Handles:
     *  - Ray direction ≈ 0 (invDir = ±1e20, IEEE 754 correct)
     *  - Ray exactly on AABB face/edge/corner (boundary-inclusive, tmin <= tmax)
     *  - NaN from 0 * ∞ (pushed into inner min/max, at least one arg non-NaN)
     *
     *  Returns (tMin, tMax). Hit if tMin <= tMax. tMin < 0 means origin inside box.
     *
     *  Reference: Tavian Barnes, "Fast, Branchless Ray/Bounding Box Intersections,
     *  Part 3: Boundaries", 2022. https://tavianator.com/2022/ray_box_boundary.html */
    [[nodiscard]] inline auto RayAABBFast(RayPrecomp const& ray, AABB const& box) -> std::pair<float, float> {
        float tmin = 0.0f;
        float tmax = std::numeric_limits<float>::infinity();

        for (int i = 0; i < 3; ++i) {
            float orig = (&ray.origin.x)[i];
            float invD = (&ray.invDir.x)[i];
            float bmin = (&box.min.x)[i];
            float bmax = (&box.max.x)[i];

            float t1 = (bmin - orig) * invD;
            float t2 = (bmax - orig) * invD;

            // Tavian Barnes boundary-inclusive NaN-safe formula:
            // tmin = min(max(t1, tmin), max(t2, tmin))
            // tmax = max(min(t1, tmax), min(t2, tmax))
            tmin = std::min(std::max(t1, tmin), std::max(t2, tmin));
            tmax = std::max(std::min(t1, tmax), std::min(t2, tmax));
        }
        return {tmin, tmax};
    }

    /** @brief Legacy ray-AABB slab test (non-precomputed, backward compatible).
     *  Returns (tMin, tMax). If tMin > tMax, no intersection.
     *  tMin < 0 means the ray origin is inside the box. */
    [[nodiscard]] inline auto RayAABBIntersect(Ray const& ray, AABB const& box) -> std::pair<float, float> {
        auto precomp = PrepareRay(ray);
        return RayAABBFast(precomp, box);
    }

}  // namespace miki::core
