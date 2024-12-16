export module polymorphic;

/*
*  A value-semantic wrapper around polymorphic objects, such that they are copied and destructed automatically without user boilerplate
*  Heavily inspired by P3019, which was approved for C++26.
*/

import std;

template<typename BaseT, typename DerivedT, typename... Args>
concept constructible_subclass_of_base = std::derived_from<DerivedT, BaseT> && std::constructible_from<DerivedT, Args...>;

export template<typename BaseT>
class polymorphic {

    static_assert(std::has_virtual_destructor_v<BaseT>, "polymorphic requires classes with virtual destructors");

    template<typename T>
    struct manager {
        static constexpr BaseT* clone(BaseT* in) {
            T* derived_ptr = static_cast<T*>(in);
            T* copy = new T(*derived_ptr);
            return copy;
        }
    };

    using clone_fun_t = BaseT * (*)(BaseT*);
    clone_fun_t cloner = nullptr;

    std::unique_ptr<BaseT> ptr;

public:

    template<typename U> requires std::derived_from<U, BaseT>
    constexpr explicit polymorphic(U&& val) : cloner{ manager<U>::clone }, ptr{ std::make_unique<U>(std::forward<U>(val)) } {}

    //"Emplacing" constructor
    template<typename U, typename... Args> requires constructible_subclass_of_base<BaseT, U, Args...>
    constexpr explicit polymorphic(std::in_place_type_t<U>, Args&&... args) : cloner{ manager<U>::clone }, ptr{ std::make_unique<U>(std::forward<Args>(args)...) } {}

    constexpr polymorphic(const polymorphic& other) : cloner{ other.cloner }, ptr{ cloner(other.ptr.get()) } {}

    constexpr polymorphic& operator=(const polymorphic& other) {
        //Exception guarantee...
        ptr.reset(other.cloner(other.ptr.get()));
        cloner = other.cloner;
        return *this;
    }

    template<typename U> requires std::derived_from<U, BaseT>
    constexpr polymorphic& operator=(U&& val) {
        auto new_val{ std::forward<U>(val) };
        auto new_ptr = manager<U>::clone(std::addressof(new_val));
        ptr.reset(new_ptr);
        cloner = manager<U>::clone;
        return *this;
    }


    constexpr polymorphic(polymorphic&&) noexcept = default;
    constexpr polymorphic& operator=(polymorphic&&) noexcept = default;

    constexpr bool valueless_after_move() const noexcept {
        return !ptr;
    }

    /*
    *  I would have dearly liked to use deducing this for operator*() and operator->()
    *  However, at time of writing, MSVC forbids deducing this functions on classes in modules because of ABI concerns.
    *  Until that's fixed, we have to do it the old fashioned way
    */

    constexpr auto&& operator*() const& {
        return *ptr;
    }
    constexpr auto& operator*()& {
        return *ptr;
    }
    constexpr auto&& operator*()&& {
        //Unique ptr appears to only ever return an lvalue reference on its operator*()
        //But if this is an rvalue, we want to move out of it
        return std::move(*ptr);
    }

    constexpr auto operator->() {
        return ptr.operator->();
    }

    constexpr const auto operator->() const {
        return ptr.operator->();
    }


};