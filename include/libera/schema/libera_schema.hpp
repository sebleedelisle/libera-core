#pragma once
// libera_schema.hpp (C++17-only, header-only)
// -----------------------------------------------------------------------------
// WHAT: A tiny, declarative toolkit to describe binary packet schemas in C++,
//       then safely decode/encode them with validation.
// HOW:
//   struct MyPkt { uint8_t proto; uint32_t rate; };
//   using namespace libera::schema;
//   const auto mySchema = makeSchema<MyPkt>(
//     field<&MyPkt::proto>("proto", BeU8{}, NonZero{}),
//     field<&MyPkt::rate >("rate" , BeU32{}),
//     objectValidator([](const MyPkt& p)->expected<void,DecodeError>{
//         if (p.proto != 1) return unexpected<DecodeError>({"proto","unsupported"});
//         return {};
//     })
//   );
//   auto pkt  = decode(mySchema, ByteView(bytes));
//   auto blob = encode(mySchema, pkt.value());
//
// DEP: https://github.com/TartanLlama/expected (single header: tl/expected.hpp)
//
// All API lives in `namespace libera::schema`.
// -----------------------------------------------------------------------------

#include <cstddef>    // std::byte, std::size_t
#include <cstdint>    // fixed-width ints
#include <array>      // FixedAscii<N>
#include <vector>
#include <tuple>
#include <string>
#include <type_traits>
#include <utility>
#include <tl/expected.hpp>   // C++17-friendly expected<T,E>

namespace libera::schema {

// ============================================================================
// 1) C++17 shims / small utilities
// ============================================================================

// Make tl::expected look like std::expected to callers of this header.
template<class T, class E>
using expected = tl::expected<T, E>;
template<class E>
using unexpected = tl::unexpected<E>;

// Minimal read-only byte view (C++17 stand-in for std::span<const std::byte>)
struct ByteView {
    const std::byte* ptr = nullptr;
    std::size_t len = 0;

    ByteView() = default;
    ByteView(const std::byte* p, std::size_t n) : ptr(p), len(n) {}

    // Convenience: construct from any contiguous container
    template<class Container>
    explicit ByteView(const Container& c)
    : ptr(reinterpret_cast<const std::byte*>(c.data())), len(c.size()) {}

    std::size_t size() const { return len; }
    const std::byte* data() const { return ptr; }
    const std::byte& operator[](std::size_t i) const { return ptr[i]; }

    // Return a new view advanced by n bytes (does not mutate *this*)
    ByteView subspan(std::size_t n) const {
        if (n > len) return {};                 // invalid -> empty view
        return ByteView(ptr + n, len - n);
    }
};

// ---- forward decl so traits can “see” ObjectValidator below ----
template<class Fn>
struct ObjectValidator;

// trait: true iff T is exactly ObjectValidator<*>
template<class T> struct IsObjectValidator : std::false_type {};
template<class Fn> struct IsObjectValidator<ObjectValidator<Fn>> : std::true_type {};

// helper: extract the *last* type from a parameter pack
template<class... Ts> struct LastType;                  // primary (variadic)
template<class T> struct LastType<T> { using type = T; };
template<class T, class... Ts>
struct LastType<T, Ts...> { using type = typename LastType<Ts...>::type; };

// ============================================================================
// 2) Error type
// ============================================================================
struct DecodeError {
    std::string where; // field/step name
    std::string what;  // human message
};

// ============================================================================
// 3) Codecs (byte <-> value)
// ============================================================================
// A "codec" has:
//   expected<Value,DecodeError> read(ByteView& s, const char* where) const;
//   void write(const Value& v, std::vector<std::byte>& out) const;
//
// Rules:
//  - read MUST bounds-check and then advance s (s = s.subspan(N))
//  - write MUST append N bytes to out

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

// Fixed-length ASCII<N>: printable ASCII or zero padding.
// Maps to std::array<char,N>. (No trimming or null-termination is applied.)
template<std::size_t N>
struct FixedAscii {
    using Arr = std::array<char, N>;

    expected<Arr, DecodeError>
    read(ByteView& s, const char* where) const {
        if (s.size() < N) return unexpected<DecodeError>({where, "not enough bytes"});
        Arr out{};
        for (std::size_t i = 0; i < N; ++i) {
            const unsigned char u = static_cast<unsigned char>(s[i]);
            const char c = static_cast<char>(u);
            if (c != 0 && (c < 0x20 || c > 0x7E))
                return unexpected<DecodeError>({where, "non-ASCII char"});
            out[i] = c;
        }
        s = s.subspan(N);
        return out;
    }

    template<class ArrLike>
    void write(const ArrLike& a, std::vector<std::byte>& out) const {
        static_assert(std::tuple_size<ArrLike>::value == N, "FixedAscii<N>: size mismatch");
        for (char c : a) out.push_back(std::byte(static_cast<unsigned char>(c)));
    }
};

// ============================================================================
// 4) Validators (field-level)
// ============================================================================
// A validator is any callable:
//   expected<void,DecodeError> operator()(const char* where, const T& v) const;

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

// Validate raw enum underlying value is in [Min, Max] BEFORE casting.
template<class Enum, uint8_t Min, uint8_t Max>
struct EnumRange {
    expected<void, DecodeError>
    operator()(const char* where, uint8_t raw) const {
        if (raw < Min || raw > Max) return unexpected<DecodeError>({where, "unknown enum value"});
        return {};
    }
};

// ============================================================================
// 5) Field descriptor
// ============================================================================
// Binds: pointer-to-member, name, codec, and zero-or-more validators.
template<auto MemberPtr, class Codec, class... Validators>
struct Field {
    const char* name;
    Codec codec;
    std::tuple<Validators...> validators;
};

// Helper for nicer callsite syntax
template<auto MemberPtr, class Codec, class... Validators>
Field<MemberPtr, Codec, Validators...>
field(const char* name, Codec c, Validators... vs) {
    return { name, c, std::tuple<Validators...>{vs...} };
}

// ============================================================================
// 6) Object-level validator (cross-field rules)
// ============================================================================
template<class Fn>
struct ObjectValidator { Fn fn; };

template<class Fn>
ObjectValidator<Fn> objectValidator(Fn fn) { return ObjectValidator<Fn>{fn}; }

// ============================================================================
// 7) Schema + makeSchema (with overload-ambiguity fix)
// ============================================================================
template<class T, class FieldsTuple, class ObjValidator>
struct Schema {
    FieldsTuple  fields;
    ObjValidator objValidator;
};

// (A) No object validator: enabled only if the *last* arg is NOT ObjectValidator
template<class T, class... FieldDescs,
         class Last = typename LastType<FieldDescs...>::type,
         class = typename std::enable_if<!IsObjectValidator<Last>::value>::type>
Schema<T, std::tuple<FieldDescs...>, struct NoObjVal>
makeSchema(FieldDescs... fds) {
    struct NoObjVal {
        expected<void, DecodeError> operator()(const T&) const { return {}; }
    };
    return { std::tuple<FieldDescs...>{fds...}, NoObjVal{} };
}

// (B) With object validator (last arg is explicitly an ObjectValidator)
template<class T, class... FieldDescs, class Fn>
Schema<T, std::tuple<FieldDescs...>, ObjectValidator<Fn>>
makeSchema(FieldDescs... fds, ObjectValidator<Fn> ov) {
    return { std::tuple<FieldDescs...>{fds...}, ov };
}

// --- NEW: tuple-based overloads (rock-solid deduction on all compilers) ---

// (A) tuple of fields, no object validator
template<class T, class... FieldDescs>
Schema<T, std::tuple<FieldDescs...>, struct NoObjVal>
makeSchema(std::tuple<FieldDescs...> fds) {
    struct NoObjVal {
        expected<void, DecodeError> operator()(const T&) const { return {}; }
    };
    return { std::move(fds), NoObjVal{} };
}

// (B) tuple of fields + object validator
template<class T, class... FieldDescs, class Fn>
Schema<T, std::tuple<FieldDescs...>, ObjectValidator<Fn>>
makeSchema(std::tuple<FieldDescs...> fds, ObjectValidator<Fn> ov) {
    return { std::move(fds), ov };
}



// ============================================================================
// 8) Internal: run all validators attached to a field
// ============================================================================
namespace detail {
template<class FieldDesc, class V>
expected<void, DecodeError> runFieldValidators(const FieldDesc& fd, const V& v) {
    expected<void, DecodeError> ok{};
    std::apply([&](auto const&... val){
        ( ( [&](){
            auto r = val(fd.name, v);
            if (!r) ok = unexpected<DecodeError>(r.error());
        }() ), ... ); // fold across validators
    }, fd.validators);
    return ok; // {} if all passed, otherwise unexpected(error)
}
} // namespace detail

// ============================================================================
// 9) decode / encode
// ============================================================================
// decode(schema, bytes):
//   - makes local ByteView s
//   - for each field in order:
//       * raw = fd.codec.read(s, fd.name)
//       * detail::runFieldValidators(fd, raw)
//       * obj.*Member = raw (cast to enum if needed)
//   - run object validator; return expected<T,DecodeError>
//
// encode(schema, obj):
//   - run object validator first
//   - for each field in order:
//       * const auto& v = obj.*Member
//       * run validators on v
//       * fd.codec.write(v, out)

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

            using MemberT =
                typename std::remove_reference<decltype(obj.*(fd.MemberPtr))>::type;

            // 1) read
            auto raw = fd.codec.read(s, fd.name);
            if (!raw) { failed = true; err = raw.error(); return; }

            // 2) validate raw
            if (auto ok = detail::runFieldValidators(fd, *raw); !ok) {
                failed = true; err = ok.error(); return;
            }

            // 3) assign (cast if enum)
            if (std::is_enum<MemberT>::value) {
                obj.*(fd.MemberPtr) = static_cast<MemberT>(*raw);
            } else {
                obj.*(fd.MemberPtr) = *raw;
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
    // Never emit illegal packets
    if (auto ok = sch.objValidator.fn(obj); !ok)
        return unexpected<DecodeError>(ok.error());

    std::vector<std::byte> out;
    bool failed = false;
    DecodeError err;

    std::apply([&](auto const&... fd){
        ( ( [&](){
            if (failed) return;

            const auto& v = obj.*(fd.MemberPtr);

            // validate before writing
            if (auto ok = detail::runFieldValidators(fd, v); !ok) {
                failed = true; err = ok.error(); return;
            }

            fd.codec.write(v, out);
        }() ), ... );
    }, sch.fields);

    if (failed) return unexpected<DecodeError>(err);
    return out;
}

} // namespace libera::schema
