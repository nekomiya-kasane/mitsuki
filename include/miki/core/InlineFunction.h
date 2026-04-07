/** @file InlineFunction.h
 *  @brief Fixed-capacity, SBO-only move-only callable wrapper.
 *
 *  InlineFunction<Sig, Cap> stores a callable entirely within inline storage
 *  (no heap allocation). A static_assert fires at construction if the callable
 *  exceeds the capacity. This is the render-graph's replacement for
 *  std::function, eliminating per-pass heap allocations.
 *
 *  Namespace: miki::core
 */
#pragma once

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace miki::core {

    namespace detail {

        template <typename Sig>
        struct InlineFunctionTraits;

        template <typename R, typename... Args>
        struct InlineFunctionTraits<R(Args...)> {
            using ReturnType = R;
            using InvokerFn = R (*)(void* storage, Args...);
            using DestroyFn = void (*)(void* storage);
            using MoveFn = void (*)(void* dst, void* src);
        };

    }  // namespace detail

    /// @brief Fixed-capacity, move-only, SBO-only callable wrapper.
    /// @tparam Sig   Function signature, e.g. `void(int)`.
    /// @tparam Cap   Inline storage capacity in bytes. Default = 64.
    ///
    /// Construction from a callable that exceeds Cap triggers a static_assert.
    /// No heap allocation ever occurs. Move-only semantics (no copy).
    template <typename Sig, size_t Cap = 64>
    class InlineFunction;

    template <typename R, typename... Args, size_t Cap>
    class InlineFunction<R(Args...), Cap> {
        static_assert(Cap >= sizeof(void*), "Cap must be at least pointer-sized");
        static_assert(
            Cap % alignof(std::max_align_t) == 0 || Cap <= alignof(std::max_align_t),
            "Cap should be a multiple of max_align_t for optimal alignment"
        );

        using Traits = detail::InlineFunctionTraits<R(Args...)>;
        using InvokerFn = typename Traits::InvokerFn;
        using DestroyFn = typename Traits::DestroyFn;
        using MoveFn = typename Traits::MoveFn;

        struct Vtable {
            InvokerFn invoke;
            DestroyFn destroy;
            MoveFn move;
        };

        template <typename F>
        static auto MakeVtable() noexcept -> const Vtable* {
            static constexpr Vtable kVtable{
                .invoke = [](void* storage, Args... args) -> R {
                    return (*static_cast<F*>(storage))(std::forward<Args>(args)...);
                },
                .destroy = [](void* storage) { static_cast<F*>(storage)->~F(); },
                .move =
                    [](void* dst, void* src) {
                        ::new (dst) F(std::move(*static_cast<F*>(src)));
                        static_cast<F*>(src)->~F();
                    },
            };
            return &kVtable;
        }

       public:
        InlineFunction() noexcept = default;

        InlineFunction(std::nullptr_t) noexcept {}  // NOLINT(google-explicit-constructor)

        /// @brief Construct from a callable. static_assert if it doesn't fit.
        template <typename F>
            requires(
                !std::is_same_v<std::remove_cvref_t<F>, InlineFunction>
                && std::is_invocable_r_v<R, std::remove_cvref_t<F>&, Args...>
            )
        InlineFunction(F&& f) noexcept(std::is_nothrow_move_constructible_v<std::remove_cvref_t<F>>) {
            using Decayed = std::remove_cvref_t<F>;
            static_assert(
                sizeof(Decayed) <= Cap, "Callable exceeds InlineFunction capacity. Increase Cap or reduce capture size."
            );
            static_assert(alignof(Decayed) <= alignof(std::max_align_t), "Callable alignment exceeds max_align_t.");
            vtable_ = MakeVtable<Decayed>();
            ::new (static_cast<void*>(&storage_)) Decayed(std::forward<F>(f));
        }

        ~InlineFunction() {
            if (vtable_) {
                vtable_->destroy(&storage_);
            }
        }

        // Move-only
        InlineFunction(const InlineFunction&) = delete;
        auto operator=(const InlineFunction&) -> InlineFunction& = delete;

        InlineFunction(InlineFunction&& other) noexcept : vtable_(other.vtable_) {
            if (vtable_) {
                vtable_->move(&storage_, &other.storage_);
                other.vtable_ = nullptr;
            }
        }

        auto operator=(InlineFunction&& other) noexcept -> InlineFunction& {
            if (this != &other) {
                if (vtable_) {
                    vtable_->destroy(&storage_);
                }
                vtable_ = other.vtable_;
                if (vtable_) {
                    vtable_->move(&storage_, &other.storage_);
                    other.vtable_ = nullptr;
                }
            }
            return *this;
        }

        auto operator=(std::nullptr_t) noexcept -> InlineFunction& {
            if (vtable_) {
                vtable_->destroy(&storage_);
                vtable_ = nullptr;
            }
            return *this;
        }

        /// @brief Invoke the stored callable.
        auto operator()(Args... args) -> R { return vtable_->invoke(&storage_, std::forward<Args>(args)...); }

        /// @brief Check if a callable is stored.
        [[nodiscard]] explicit operator bool() const noexcept { return vtable_ != nullptr; }

        /// @brief Swap two InlineFunctions.
        void Swap(InlineFunction& other) noexcept {
            alignas(std::max_align_t) std::array<std::byte, Cap> tmp{};
            if (vtable_) {
                vtable_->move(tmp.data(), &storage_);
            }
            if (other.vtable_) {
                other.vtable_->move(&storage_, &other.storage_);
            }
            if (vtable_) {
                // vtable_ was our old vtable, move from tmp into other
                auto* oldVtable = vtable_;
                vtable_ = other.vtable_;
                other.vtable_ = oldVtable;
                other.vtable_->move(&other.storage_, tmp.data());
            } else {
                vtable_ = other.vtable_;
                other.vtable_ = nullptr;
            }
        }

       private:
        const Vtable* vtable_ = nullptr;
        alignas(std::max_align_t) std::array<std::byte, Cap> storage_{};
    };

    template <typename R, typename... Args, size_t Cap>
    auto operator==(const InlineFunction<R(Args...), Cap>& f, std::nullptr_t) noexcept -> bool {
        return !static_cast<bool>(f);
    }

}  // namespace miki::core
