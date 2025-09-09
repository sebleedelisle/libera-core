#pragma once
// libera_schema.hpp (C++17-only, header-only)
// -----------------------------------------------------------------------------
// Tiny declarative toolkit for binary packet schemas + safe decode/encode.
//
// Usage sketch:
//   struct MyPkt { uint8_t proto; uint32_t rate; };
//   using namespace libera::schema;
//   constexpr auto fields = std::make_tuple(
//       field<&MyPkt::proto>("proto", BeU8{}, NonZero{}),
//       field<&MyPkt::rate >("rate" , BeU32{})
//   );
//   const auto schema = makeSchema<MyPkt>(fields, objectValidator([](const MyPkt& p)
//       -> expected<void, DecodeError> {
//       if (p.proto != 1) return unexpected<DecodeError>({"proto","unsupported"});
//       return {};
//   }));
//   auto pkt  = decode(schema, ByteView(bytes));
//   auto blob = encode(schema, pkt.value());
//
// Depends on: TartanLlama expected (single header):  tl/expected.hpp
// -----------------------------------------------------------------------------

#include <cstddef>    // std::byte, std::size_t
#include <cstdint>    // fixed-width ints
#include <array>      // FixedAscii<N>
#include <vector>
#include <tuple>
#include <string>
#include <type_traits>
#include <utility>
#include <tl/expected.hpp>
#include <sstream>

namespace libera::schema {

// ============================================================================
// Basics (C++17 shims)
// ============================================================================
template<class T, class E>
using expected = tl::expected<T, E>;
template<class E>
using unexpected = tl::unexpected<E>;

// Minimal read-only byte slice (stand-in for std::span<const std::byte>)
struct ByteView {
    const std::byte* ptr = nullptr;
    std::size_t len = 0;

    ByteView() = default;
    ByteView(const std::byte* p, std::size_t n) : ptr(p), len(n) {}

    template<class Container>
    explicit ByteView(const Container& c)
    : ptr(reinterpret_cast<const std::byte*>(c.data())), len(c.size()) {}

    std::size_t size() const { return len; }
    const std::byte* data() const { return ptr; }
    const std::byte& operator[](std::size_t i) const { return ptr[i]; }

    ByteView subspan(std::size_t n) const {
        if (n > len) return {};
        return ByteView(ptr + n, len - n);
    }
};

// Error payload used by expected
struct DecodeError {
    std::string where;
    std::string what;
};

// ============================================================================
// Codecs (value <-> bytes) : Be == big-endian fixed width + fixed ASCII
// ============================================================================
struct BeU8 {
    expected<uint8_t, DecodeError>
    read(ByteView& s, const char* where) const {
        if (s.size() < 1) return unexpected<DecodeError>({where, "need 1 byte"});
        uint8_t v = static_cast<uint8_t>(s[0]);
        s = s.subspan(1);
        return v;
    }
    void write(uint8_t v, std::vector<std::byte>& out) const {
        out.push_back(static_cast<std::byte>(v));
    }
};

struct BeU16 {
    expected<uint16_t, DecodeError>
    read(ByteView& s, const char* where) const {
        if (s.size() < 2) return unexpected<DecodeError>({where, "need 2 bytes"});
        uint16_t v = (static_cast<uint16_t>(static_cast<uint8_t>(s[0])) << 8)
                   |  static_cast<uint16_t>(static_cast<uint8_t>(s[1]));
        s = s.subspan(2);
        return v;
    }
    void write(uint16_t v, std::vector<std::byte>& out) const {
        out.push_back(std::byte((v >> 8) & 0xFF));
        out.push_back(std::byte(v & 0xFF));
    }
};

struct BeU32 {
    expected<uint32_t, DecodeError>
    read(ByteView& s, const char* where) const {
        if (s.size() < 4) return unexpected<DecodeError>({where, "need 4 bytes"});
        uint32_t v = (static_cast<uint32_t>(static_cast<uint8_t>(s[0])) << 24)
                   | (static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 16)
                   | (static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 8)
                   |  static_cast<uint32_t>(static_cast<uint8_t>(s[3]));
        s = s.subspan(4);
        return v;
    }
    void write(uint32_t v, std::vector<std::byte>& out) const {
        out.push_back(std::byte((v >> 24) & 0xFF));
        out.push_back(std::byte((v >> 16) & 0xFF));
        out.push_back(std::byte((v >> 8 ) & 0xFF));
        out.push_back(std::byte(v & 0xFF));
    }
};

// Fixed-length ASCII (printable or zero padding). Maps to std::array<char,N>.
template<std::size_t N>
struct FixedAscii {
    using Arr = std::array<char, N>;

    expected<Arr, DecodeError>
    read(ByteView& s, const char* where) const {
        if (s.size() < N) return unexpected<DecodeError>({where, "not enough bytes"});
        Arr out{};
        for (std::size_t i = 0; i < N; ++i) {
            unsigned char u = static_cast<unsigned char>(s[i]);
            char c = static_cast<char>(u);
            if (c != 0 && (c < 0x20 || c > 0x7E))
                return unexpected<DecodeError>({where, "non-ASCII char"});
            out[i] = c;
        }
        s = s.subspan(N);
        return out;
    }

    template<class ArrLike>
    void write(const ArrLike& a, std::vector<std::byte>& out) const {
        static_assert(std::tuple_size<ArrLike>::value == N, "FixedAscii size mismatch");
        for (char c : a) out.push_back(std::byte(static_cast<unsigned char>(c)));
    }
};

// ============================================================================
// Validators
// ============================================================================
struct NonZero {
    template<class U>
    expected<void, DecodeError> operator()(const char* where, const U& v) const {
        if constexpr (std::is_integral<U>::value) {
            if (v == 0) return unexpected<DecodeError>({where, "must be non-zero"});
        }
        return {};
    }
};

struct NotEmptyAscii {
    template<class Arr>
    expected<void, DecodeError> operator()(const char* where, const Arr& a) const {
        bool any = false;
        for (char c : a) { if (c != 0) { any = true; break; } }
        if (!any) return unexpected<DecodeError>({where, "must not be empty"});
        return {};
    }
};

template<class Enum, uint8_t Min, uint8_t Max>
struct EnumRange {
    expected<void, DecodeError> operator()(const char* where, uint8_t raw) const {
        if (raw < Min || raw > Max) {
            std::ostringstream msg;
            msg << "unknown enum value " << static_cast<int>(raw)
                << " (expected " << static_cast<int>(Min)
                << "-" << static_cast<int>(Max) << ")";
            return unexpected<DecodeError>({where, msg.str()});
        }
        return {};
    }
};

// ============================================================================
// Field descriptor + helper
// ============================================================================
template<auto MemberPtr, class Codec, class... Validators>
struct Field {
      // Expose the pointer-to-member so instances can use it.
    static constexpr auto memberPtr = MemberPtr;
    const char* name;
    Codec codec;
    std::tuple<Validators...> validators;
};

template<auto MemberPtr, class Codec, class... Validators>
Field<MemberPtr, Codec, Validators...>
field(const char* name, Codec c, Validators... vs) {
    return { name, c, std::tuple<Validators...>{vs...} };
}

// ============================================================================
// Object-level validator
// ============================================================================
template<class Fn>
struct ObjectValidator { Fn fn; };

template<class Fn>
ObjectValidator<Fn> objectValidator(Fn fn) { return ObjectValidator<Fn>{fn}; }

// ============================================================================
// Schema + tuple-only makeSchema (no overload ambiguity)
// ============================================================================
template<class T, class FieldsTuple, class ObjValidator>
struct Schema {
    FieldsTuple  fields;
    ObjValidator objValidator;
};

// tuple of fields, no object validator
template<class T, class... FieldDescs>
Schema<T, std::tuple<FieldDescs...>, struct NoObjVal>
makeSchema(std::tuple<FieldDescs...> fds) {
    struct NoObjVal {
        expected<void, DecodeError> operator()(const T&) const { return {}; }
    };
    return { std::move(fds), NoObjVal{} };
}

// tuple of fields + object validator
template<class T, class... FieldDescs, class Fn>
Schema<T, std::tuple<FieldDescs...>, ObjectValidator<Fn>>
makeSchema(std::tuple<FieldDescs...> fds, ObjectValidator<Fn> ov) {
    return { std::move(fds), ov };
}
// ============================================================================
// Internals: run all validators for a field
// ============================================================================
namespace detail {

// Only instantiate underlying_type when T is an enum.
template<class T, bool IsEnum>
struct NormalizeImpl { using type = T; };

template<class T>
struct NormalizeImpl<T, true> { using type = typename std::underlying_type<T>::type; };

template<class T>
using normalize_t = typename NormalizeImpl<T, std::is_enum<T>::value>::type;

template<class FieldDesc, class V>
expected<void, DecodeError> runFieldValidators(const FieldDesc& fd, const V& v) {
    using NormT = normalize_t<V>;
    const NormT vv = static_cast<NormT>(v);

    expected<void, DecodeError> ok{};
    std::apply([&](auto const&... val){
        ( ( [&](){
            auto r = val(fd.name, vv);
            if (!r) ok = unexpected<DecodeError>(r.error());
        }() ), ... );
    }, fd.validators);
    return ok;
}

} // namespace detail



// ============================================================================
// decode / encode
// ============================================================================
template<class T, class FieldsTuple, class ObjValidatorT>
expected<T, DecodeError>
decode(const Schema<T, FieldsTuple, ObjValidatorT>& sch, ByteView bytes) {
    T obj{};
    ByteView s = bytes;

    bool failed = false;
    DecodeError err;

    std::apply([&](auto const&... fd){
        ( ( [&](){
            if (failed) return;

            using MemberT = typename std::remove_reference<decltype(obj.*(fd.memberPtr))>::type;

            auto raw = fd.codec.read(s, fd.name);
            if (!raw) { failed = true; err = raw.error(); return; }

            if (auto ok = detail::runFieldValidators(fd, *raw); !ok) {
                failed = true; err = ok.error(); return;
            }

            // assign (cast if enum)
            if constexpr (std::is_enum_v<MemberT>) {
                obj.*(fd.memberPtr) = static_cast<MemberT>(*raw);
            } else {
                obj.*(fd.memberPtr) = *raw;
            }
        }() ), ... );
    }, sch.fields);

    if (failed) return unexpected<DecodeError>(err);

    if (auto ok = sch.objValidator.fn(obj); !ok)
        return unexpected<DecodeError>(ok.error());

    return obj;
}

template<class T, class FieldsTuple, class ObjValidatorT>
expected<std::vector<std::byte>, DecodeError>
encode(const Schema<T, FieldsTuple, ObjValidatorT>& sch, const T& obj) {
    if (auto ok = sch.objValidator.fn(obj); !ok)
        return unexpected<DecodeError>(ok.error());

    std::vector<std::byte> out;
    bool failed = false;
    DecodeError err;

    std::apply([&](auto const&... fd){
        ( ( [&](){
            if (failed) return;

           // encode loop, inside the apply:
            const auto& v = obj.*(fd.memberPtr);

            // validate before writing
            if (auto ok = detail::runFieldValidators(fd, v); !ok) {
                failed = true; err = ok.error(); return;
            }

            // write, with enum→underlying conversion if needed
            if constexpr (std::is_enum_v<std::decay_t<decltype(v)>>) {
                using U = typename std::underlying_type<std::decay_t<decltype(v)>>::type;
                fd.codec.write(static_cast<U>(v), out);
            } else {
                fd.codec.write(v, out);
            }

        }() ), ... );
    }, sch.fields);

    if (failed) return unexpected<DecodeError>(err);
    return out;
}

} // namespace libera::schema
