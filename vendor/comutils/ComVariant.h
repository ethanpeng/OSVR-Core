/** @file
    @brief Header defining a wrapper for COM's VARIANT type.

    @date 2015

    @author
    Sensics, Inc.
    <http://sensics.com/osvr>
*/

// Copyright 2015 Sensics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef INCLUDED_ComVariant_h_GUID_A0A043D9_A146_4693_D351_06F6A04ABADA
#define INCLUDED_ComVariant_h_GUID_A0A043D9_A146_4693_D351_06F6A04ABADA

#ifdef _WIN32

// Internal Includes
// - none

// Library/third-party includes
#include <oaidl.h>

// Standard includes
#include <cassert>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace comutils {
namespace variant {
    namespace detail {

        /// @brief A template alias for use in static asserts..
        template <typename T>
        using is_variant_type =
            std::integral_constant<bool,
                                   std::is_same<T, VARIANT>::value ||
                                       std::is_same<T, VARIANTARG>::value>;

        /// @brief An enable_if_t-alike that has the specific condition built in
        /// as well as a default result type.
        template <typename T, typename Result = void *>
        using enable_for_variants_t =
            typename std::enable_if<is_variant_type<T>::value, Result>::type;

        template <typename OutputType> struct VariantTypeTraits;
/// @todo extend the VariantTypeTraits implementation
/// https://msdn.microsoft.com/en-us/library/windows/desktop/ms221170(v=vs.85).aspx

#if 0 // I don't actually see any members in the union to hold these...
template <> struct VariantTypeTraits<const wchar_t *> {
    static const auto vt = VT_LPWSTR;
};
template <> struct VariantTypeTraits<const char *> {
    static const auto vt = VT_LPSTR;
};
#endif

        template <> struct VariantTypeTraits<BSTR> {
            static const auto vt = VT_BSTR;
            template <typename T> static BSTR get(T &v) { return v.bstrVal; }
        };

        // std::wstring is the "C++-side" wrapper for a BSTR here.
        template <>
        struct VariantTypeTraits<std::wstring> : VariantTypeTraits<BSTR> {
            template <typename T> static std::wstring get(T &v) {
                auto bs = v.bstrVal;
                return std::wstring(static_cast<const wchar_t *>(bs),
                                    SysStringLen(bs));
            }
        };
        template <typename Dest> struct VariantArrayTypeTraits {};
        template <> struct VariantArrayTypeTraits<BSTR> {
            static const auto vt = VT_BSTR;
        };

        template <>
        struct VariantArrayTypeTraits<std::wstring>
            : VariantArrayTypeTraits<BSTR> {
            static std::wstring get(SAFEARRAY &arr, std::size_t i) {
                BSTR bs = nullptr;
                auto idx = static_cast<long>(i);
                std::wstring ret;
                auto hr =
                    SafeArrayGetElement(&arr, &idx, static_cast<void *>(&bs));
                if (SUCCEEDED(hr) && bs) {
                    ret = std::wstring(static_cast<const wchar_t *>(bs),
                                       SysStringLen(bs));
                    SysFreeString(bs);
                }
                return ret;
            }
        };

        /// Possible ways to handle the HRESULT from VariantClear in the
        /// destructor of our wrapper.
        namespace DestructionPolicies {
            /// Default policy
            struct SilentAndAssert {
                static void handle(HRESULT hr) {
                    switch (hr) {
                    case S_OK:
                        break;
                    case DISP_E_ARRAYISLOCKED:
                        /*
                            OutputDebugStringA("VariantClear failed on variant "
                                "destruction: Variant "
                                "contains an array that is locked");
                                */
                        break;
                    case DISP_E_BADVARTYPE:
                        /*
                            OutputDebugStringA("VariantClear failed on variant "
                                "destruction: Variant is "
                                "not a valid type");
                                */
                        break;
                    case E_INVALIDARG:
                        assert(hr != E_INVALIDARG &&
                               // shouldn't happen.
                               "VariantClear failed on variant "
                               "destruction: invalid "
                               "argument.");
                        break;
                    }
                }
            };

/// If the user has already included windows.h, we'll offer this one.
#ifdef _INC_WINDOWS
            struct DebugStringAndAssert {
                static void handle(HRESULT hr) {
                    switch (hr) {
                    case S_OK:
                        break;
                    case DISP_E_ARRAYISLOCKED:
                        OutputDebugStringA("VariantClear failed on variant "
                                           "destruction: Variant "
                                           "contains an array that is locked");
                        break;
                    case DISP_E_BADVARTYPE:
                        OutputDebugStringA("VariantClear failed on variant "
                                           "destruction: Variant is "
                                           "not a valid type");
                        break;
                    case E_INVALIDARG:
                        assert(hr != E_INVALIDARG &&
                               // shouldn't happen.
                               "VariantClear failed on variant "
                               "destruction: invalid "
                               "argument.");
                        break;
                    }
                }
            };
#endif

            /// Technically illegal since destructors are implicitly noexcept.
            struct ThrowAndAssert {
                static void handle(HRESULT hr) {
                    switch (hr) {
                    case S_OK:
                        break;
                    case DISP_E_ARRAYISLOCKED:
                        throw std::runtime_error(
                            "VariantClear failed on variant "
                            "destruction: Variant "
                            "contains an array that is locked");
                        break;
                    case DISP_E_BADVARTYPE:
                        throw std::runtime_error(
                            "VariantClear failed on variant "
                            "destruction: Variant is "
                            "not a valid type");
                        break;
                    case E_INVALIDARG:
                        assert(hr != E_INVALIDARG &&
                               // shouldn't happen.
                               "VariantClear failed on variant "
                               "destruction: invalid "
                               "argument.");
                        break;
                    }
                }
            };

            using Default = SilentAndAssert;
        } // namespace DestructionPolicies

        /// @brief Low-level variant holder: just handles creation and
        /// destruction.
        ///
        /// Could be used by itself on the stack, but not terribly likely.
        template <typename T = VARIANT,
                  typename DestructionPolicy = DestructionPolicies::Default>
        struct VariantHolder {
          public:
            static_assert(
                is_variant_type<T>::value,
                "Only valid type parameters are VARIANT or VARIANTARG");
            using type = VariantHolder<T>;
            using unique_type = std::unique_ptr<type>;
            static unique_type make_unique() {
                unique_type ret(new type);
                return ret;
            }
            VariantHolder() { VariantInit(getPtr()); }
            ~VariantHolder() {
                auto hr = VariantClear(getPtr());
                DestructionPolicy::handle(hr);
            }
            /// non-copyable
            VariantHolder(VariantHolder const &) = delete;
            /// non-assignable
            VariantHolder &operator=(VariantHolder const &) = delete;
            T &get() { return data_; }
            T const &get() const { return data_; }
            T *getPtr() { return &data_; }
            T const *getPtr() const { return &data_; }

          private:
            T data_;
        };

        /// Proxy class to let you use range-based for loops on SafeArrays like
        /// those coming out of VARIANTs. Construct with getArray below.
        template <typename Dest> struct VariantSafeArrayRange {
          public:
            explicit VariantSafeArrayRange(SAFEARRAY &arr) : arr_(arr) {
                if (SafeArrayGetDim(&arr_) != numDims_) {
                    // assumes dimensions = 1;
                    throw std::runtime_error(
                        "Can't use this class on this array: "
                        "we assume a 1-dimensional array, "
                        "which this is not!");
                }
                if (!SUCCEEDED(SafeArrayGetUBound(&arr_, dim_, &uBound_))) {
                    throw std::runtime_error(
                        "Couldn't get 0th dimension upper bound.");
                }

                if (!SUCCEEDED(SafeArrayGetLBound(&arr_, dim_, &lBound_))) {
                    throw std::runtime_error(
                        "Couldn't get 0th dimension lower bound.");
                }
            }
            using type = VariantSafeArrayRange<Dest>;

            Dest get(std::size_t i) const {
                return detail::VariantArrayTypeTraits<Dest>::get(arr_, i);
            }
            struct SafeArrayRangeIterator {
                // default constructor: invalid/end iterator
                SafeArrayRangeIterator() {}
                // Construct with index.
                SafeArrayRangeIterator(type const &range, long idx)
                    : elt_(idx), range_(&range) {
                    checkIndex();
                }

                explicit operator bool() const { return range_ != nullptr; }

                bool operator==(SafeArrayRangeIterator const &other) const {
                    return (elt_ == other.elt_) && (range_ == other.range_);
                }
                bool operator!=(SafeArrayRangeIterator const &other) const {
                    return elt_ != other.elt_ || range_ != other.range_;
                }

                Dest operator*() const { return range_->get(elt_); }

                SafeArrayRangeIterator &operator++() {
                    increment();
                    return *this;
                }
                SafeArrayRangeIterator &operator++(int) {
                    SafeArrayRangeIterator copy(*this);
                    increment();
                    return copy;
                }

              private:
                void increment() {
                    if (*this) {
                        ++elt_;
                        checkIndex();
                    }
                }
                void checkIndex() {
                    if (range_) {
                        if (!range_->inBounds(elt_)) {
                            range_ = nullptr;
                        }
                    }
                    if (!range_) {
                        elt_ = INVALID_ELEMENT;
                    }
                }
                static const long INVALID_ELEMENT = -1;
                long elt_ = INVALID_ELEMENT;
                type const *range_ = nullptr;
            };
            bool inBounds(long idx) const {
                return idx >= lBound_ && idx <= uBound_;
            }

            using iterator = SafeArrayRangeIterator;
            using const_iterator = SafeArrayRangeIterator;
            SafeArrayRangeIterator begin() const {
                return SafeArrayRangeIterator(*this, lBound_);
            }
            SafeArrayRangeIterator end() const {
                return SafeArrayRangeIterator();
            }

          private:
            SAFEARRAY &arr_;
            const UINT numDims_ = 1;
            const UINT dim_ = 1;
            long uBound_ = 0;
            long lBound_ = 0;
        };
    } // namespace detail

    /// @brief A wrapper for VARIANT/VARIANTARG on Windows that is both copy and
    /// move aware, and does not require any Visual Studio libraries (unlike
    /// comutil.h)
    template <typename T = VARIANT,
              typename DestructionPolicy = detail::DestructionPolicies::Default>
    class VariantWrapper {

      public:
        static_assert(detail::is_variant_type<T>::value,
                      "Only valid type parameters are VARIANT or VARIANTARG");
        using variant_type = T;
        using destruction_policy = DestructionPolicy;
        using type = VariantWrapper<T, DestructionPolicy>;
        using holder_type = detail::VariantHolder<T, DestructionPolicy>;
        using holder_unique_ptr = typename holder_type::unique_type;

        /// @brief Default constructor - sets up an empty variant
        VariantWrapper() : data_(holder_type::make_unique()) {}

        /// @brief Constructor from nullptr - does not allocate a variant.
        explicit VariantWrapper(std::nullptr_t) {}

        /// @brief Copy constructor - copies a variant (without following
        /// indirection of VT_BYREF)
        VariantWrapper(VariantWrapper const &other) : VariantWrapper() {
            if (!other) {
                throw std::logic_error(
                    "On variant copy-construction: tried to "
                    "copy from a variant in an invalid state "
                    "(moved-from)!");
            }
            (*this) = other;
        }

        /// @brief Move constructor - moves the variant out of the other object,
        /// leaving it as if initialized with nullptr.
        VariantWrapper(VariantWrapper &&other)
            : data_(std::move(other.data_)) {}

        /// @brief Move-assignment - moves the variant out of the other object,
        /// leaving it as if initialized with nullptr
        type &operator=(type &&other) {
            dealloc();
            data_ = std::move(other.data_);
            return *this;
        }

        /// @brief Copy-assignment - copies a variant (without following
        /// indirection of VT_BYREF)
        type &operator=(VariantWrapper const &other) {
            if (!other) {
                throw std::logic_error(
                    "On variant copy-assignment: tried to copy "
                    "from a variant in an invalid state "
                    "(moved-from)!");
            }
            ensureInit();
            auto hr = VariantCopy(getPtr(), other.getPtr());
            switch (hr) {
            case S_OK:
                break;
            case DISP_E_ARRAYISLOCKED:
                throw std::runtime_error(
                    "VariantCopy failed on variant "
                    "copy-construction/assignment: Variant "
                    "contains an array that is locked");
                break;
            case DISP_E_BADVARTYPE:
                throw std::runtime_error(
                    "VariantCopy failed on variant "
                    "copy-construction/assignment: Variant is "
                    "not a valid type");
                break;
            case E_INVALIDARG:
                throw std::invalid_argument(
                    "VariantCopy failed on variant "
                    "copy-construction/assignment: invalid "
                    "argument.");
                break;
            case E_OUTOFMEMORY:
                throw std::runtime_error("VariantCopy failed on variant "
                                         "copy-construction/assignment: "
                                         "insufficient memory.");
                break;
            }
            return *this;
        }

        /// @brief Checks to see if this variant is in a valid state: not
        /// moved-from or initialized with nullptr.
        explicit operator bool() const { return bool(data_); }

        /// @brief Ensures the variant is initialized.
        void ensureInit() {
            if (!data_) {
                data_ = holder_type::make_unique();
            }
        }

        /// @brief Uncommon: de-allocates the variant. Usually you just let the
        /// destructor do this.
        void dealloc() { data_.reset(); }

        /// @name Access to the underlying VARIANT/VARIANTARG
        /// @{
        variant_type &get() { return data_->get(); }
        variant_type const &get() const { return data_->get(); }
        variant_type *getPtr() { return data_->getPtr(); }
        variant_type const *getPtr() const { return data_->getPtr(); }
        /// @}

      private:
        holder_unique_ptr data_;
    };

    /// @brief Function similar to AttachPtr, but for variants.
    template <typename T> inline T *AttachVariant(VariantWrapper<T> &v) {
        return v.getPtr();
    }

    using Variant = VariantWrapper<VARIANT>;
    using VariantArg = VariantWrapper<VARIANTARG>;

    /// @brief Determines if the type of data in the variant can be described as
    /// the type parameter @p Dest (without coercion)
    template <typename Dest, typename T>
    inline detail::enable_for_variants_t<T, bool> contains(T const &v) {
        return (v.vt == detail::VariantTypeTraits<Dest>::vt);
    }
    /// @overload
    /// For wrapped variants.
    template <typename Dest, typename T>
    inline bool contains(VariantWrapper<T> const &v) {
        return v && contains<Dest>(v.get());
    }

    /// @brief Determines if the type of data in the variant can be described as
    /// an array of the type parameter @p Dest (without coercion)
    template <typename Dest, typename T>
    inline detail::enable_for_variants_t<T, bool> containsArray(T const &v) {
        return (v.vt == (detail::VariantTypeTraits<Dest>::vt | VT_ARRAY));
    }
    /// @overload
    /// For wrapped variants.
    template <typename Dest, typename T>
    inline bool containsArray(VariantWrapper<T> const &v) {
        return v && containsArray<Dest>(v.get());
    }

    /// @brief Determines if the variant passed is "empty"
    template <typename T>
    inline detail::enable_for_variants_t<T, bool> is_empty(T const &v) {
        return (v.vt == VT_EMPTY);
    }

    /// @overload
    /// For wrapped variants. Also returns true if the wrapper itself is empty.
    template <typename T> inline bool is_empty(VariantWrapper<T> const &v) {
        return (!v) || is_empty(v.get());
    }

    /// @brief Get the data of type @p Dest from the variant: no
    /// conversion/coercion applied.
    template <typename Dest, typename T>
    inline detail::enable_for_variants_t<T, Dest> get(T const &v) {
        if (!contains<Dest>(v)) {
            throw std::invalid_argument(
                "Variant does not contain the type of data "
                "you are trying to access!");
        }
        return detail::VariantTypeTraits<Dest>::get(v);
    }

    /// @overload
    /// For wrapped variants.
    template <typename Dest, typename T>
    inline Dest get(VariantWrapper<T> const &v) {
        if (!contains<Dest>(v)) {
            throw std::invalid_argument(
                "Variant does not contain the type of data "
                "you are trying to access!");
        }
        return get<Dest>(v.get());
    }
    /// @brief Get the array element data of type @p Dest from the variant: no
    /// conversion/coercion applied.
    template <typename Dest, typename T>
    inline detail::enable_for_variants_t<T, Dest> getArray(T const &v,
                                                           std::size_t i) {
        if (!containsArray<Dest>(v)) {
            throw std::invalid_argument(
                "Variant does not contain an array of the type of data "
                "you are trying to access!");
        }
        SAFEARRAY *arr = V_ARRAY(&v);
        return detail::VariantArrayTypeTraits<Dest>::get(*arr, i);
    }
    /// @overload
    /// For wrapped variants.
    template <typename Dest, typename T>
    inline Dest getArray(VariantWrapper<T> const &v, std::size_t i) {
        if (!containsArray<Dest>(v)) {
            throw std::invalid_argument(
                "Variant does not contain an array of the type of data "
                "you are trying to access!");
        }
        return getArray<Dest>(v.get());
    }

    /// @brief Get a range for easily accessing array element data of type @p
    /// Dest from the variant: no conversion/coercion applied.
    template <typename Dest, typename T>
    inline detail::enable_for_variants_t<T, detail::VariantSafeArrayRange<Dest>>
    getArray(T const &v) {
        if (!containsArray<Dest>(v)) {
            throw std::invalid_argument(
                "Variant does not contain an array of the type of data "
                "you are trying to access!");
        }
        SAFEARRAY *arr = V_ARRAY(&v);
        return detail::VariantSafeArrayRange<Dest>(*arr);
    }
    /// @overload
    /// For wrapped variants.
    template <typename Dest, typename T>
    inline detail::VariantSafeArrayRange<Dest>
    getArray(VariantWrapper<T> const &v) {
        if (!containsArray<Dest>(v)) {
            throw std::invalid_argument(
                "Variant does not contain an array of the type of data "
                "you are trying to access!");
        }
        return getArray<Dest>(v.get());
    }
} // namespace variant

using namespace variant;

} // namespace comutils

#endif // _WIN32

#endif // INCLUDED_ComVariant_h_GUID_A0A043D9_A146_4693_D351_06F6A04ABADA
