#ifndef PRINT2_H
#define PRINT2_H

#include <string>
#include <array>
#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <ryu/ryu2.h>

struct BufferWriter;
struct State;

namespace detail
{
template<typename... Ts> struct make_void { typedef void type;};

template <std::size_t ...>
struct index_sequence {};

template <std::size_t N, std::size_t ... Next>
struct index_sequence_helper : public index_sequence_helper<N-1U, N-1U, Next...> {};

template <std::size_t ... Next>
struct index_sequence_helper<0U, Next ... >
{ using type = index_sequence<Next ... >; };

template <std::size_t N>
using make_index_sequence = typename index_sequence_helper<N>::type;

template <typename T, std::size_t...Is>
constexpr std::array<T, sizeof...(Is)>
make_array(const T& value, index_sequence<Is...>)
{
    return {{(static_cast<void>(Is), value)...}};
}
} // namespace detail

template<typename... Ts> using void_t = typename detail::make_void<Ts...>::type;

template<typename, typename = void> struct has_global_to_string : std::false_type {};
template<typename, typename = void> struct has_member_to_string : std::false_type {};
template<typename T> struct has_global_to_string<T, void_t<decltype(to_string(std::declval<T>()))> > : std::true_type {};
template<typename T> struct has_member_to_string<T, void_t<decltype(std::declval<T>().to_string())> > : std::true_type {};

template<class T>
struct is_c_string : std::integral_constant<
    bool,
    std::is_same<const char*, typename std::decay<T>::type>::value ||
    std::is_same<char*, typename std::decay<T>::type>::value>
{
};

template<class T>
struct is_exact_c_string : std::integral_constant<
    bool,
    (std::is_same<const char*, T>::value && !std::is_reference<T>::value) ||
        (std::is_same<char*, T>::value && !std::is_reference<T>::value)>
{
};

struct Argument
{
    enum Type
    {
        Int32,
        Uint32,
        Int64,
        Uint64,
        Double,
        Pointer,
        IntPointer,
        String,
        Custom
    } type;
    struct StringType
    {
        const char* str;
        size_t len;
    };
    struct CustomType
    {
        const void* data;
        void (*format)(BufferWriter& writer, const State& state, const void* data);
    };
    union {
        int32_t i32;
        uint32_t u32;
        int64_t i64;
        uint64_t u64;
        double dbl;
        void* ptr;
        StringType str;
        CustomType custom;
    } value;
};

template<typename ...Args>
struct ArgumentStore
{
    static const size_t ArgCount = sizeof...(Args);
    Argument args[ArgCount];

    ArgumentStore(Args&& ...args);
};

struct Arguments
{
    const Argument* args;
    const size_t count;

    Arguments() : args(nullptr), count(0) { }
    template<typename ...Args>
    Arguments(ArgumentStore<Args...>&& store) : args(store.args), count(store.ArgCount) { }
};

void print2_format_generic(BufferWriter& writer, const State& state, const typename Argument::StringType& str);
int print2_helper(char* buffer, size_t bufsiz, const char* format, const Arguments& args);

#define MAKE_ARITHMETIC_ARG(tp, itp, val)       \
    inline Argument make_arithmetic_arg(tp arg) \
    {                                           \
        Argument a;                             \
        a.type = Argument::itp;                 \
        a.value.val = static_cast<tp>(arg);     \
        return a;                               \
    }

MAKE_ARITHMETIC_ARG(double, Double, dbl);
MAKE_ARITHMETIC_ARG(int32_t, Int32, i32);
MAKE_ARITHMETIC_ARG(uint32_t, Uint32, u32);
MAKE_ARITHMETIC_ARG(int64_t, Int64, i64);
MAKE_ARITHMETIC_ARG(uint64_t, Uint64, u64);

MAKE_ARITHMETIC_ARG(bool, Int32, i32);
MAKE_ARITHMETIC_ARG(char, Int32, i32);
MAKE_ARITHMETIC_ARG(signed char, Int32, i32);
MAKE_ARITHMETIC_ARG(unsigned char, Int32, i32);
MAKE_ARITHMETIC_ARG(float, Double, dbl);

#undef MAKE_ARITHMETIC_ARG

template<typename Arg, typename std::enable_if<std::is_arithmetic<typename std::decay<Arg>::type>::value, void>::type* = nullptr>
Argument make_arg(Arg&& arg)
{
    return make_arithmetic_arg(static_cast<typename std::decay<Arg>::type>(arg));
}

template<typename Arg, typename std::enable_if<is_c_string<typename std::decay<Arg>::type>::value, void>::type* = nullptr>
Argument make_arg(Arg&& arg)
{
    Argument a;
    a.type = Argument::String;
    a.value.str = { arg, strlen(arg) };
    return a;
}

template<typename Arg, typename std::enable_if<std::is_same<std::string, typename std::decay<Arg>::type>::value, void>::type* = nullptr>
Argument make_arg(Arg&& arg)
{
    Argument a;
    a.type = Argument::String;
    a.value.str = { arg.c_str(), arg.size() };
    return a;
}

template<typename Arg, typename std::enable_if<std::is_same<int*, typename std::remove_reference<Arg>::type>::value, void>::type* = nullptr>
Argument make_arg(Arg&& arg)
{
    Argument a;
    a.type = Argument::IntPointer;
    a.value.ptr = arg;
    return a;
}

template<typename Arg, typename std::enable_if<!std::is_same<int*, typename std::remove_reference<Arg>::type>::value && std::is_pointer<typename std::remove_reference<Arg>::type>::value, void>::type* = nullptr>
    Argument make_arg(Arg&& arg)
{
    Argument a;
    a.type = Argument::Pointer;
    a.value.ptr = arg;
    return a;
}

template<typename Arg, typename std::enable_if<std::is_same<std::nullptr_t, typename std::remove_reference<Arg>::type>::value, void>::type* = nullptr>
Argument make_arg(Arg&& arg)
{
    Argument a;
    a.type = Argument::Pointer;
    a.value.ptr = nullptr;
    return a;
}

template<typename Arg, typename std::enable_if<has_global_to_string<typename std::decay<Arg>::type>::value, void>::type* = nullptr>
Argument make_arg(Arg&& arg)
{
    Argument a;
    a.type = Argument::Custom;
    a.value.custom = { &arg, [](BufferWriter& writer, const State& state, const void* ptr) {
            typedef typename std::decay<Arg>::type ArgType;
            const ArgType& val = *reinterpret_cast<const ArgType*>(ptr);
            const std::string& str = to_string(val);
            print2_format_generic(writer, state, typename Argument::StringType { str.c_str(), str.size() });
        } };
    return a;
}

template<typename Arg, typename std::enable_if<has_member_to_string<typename std::decay<Arg>::type>::value, void>::type* = nullptr>
Argument make_arg(Arg&& arg)
{
    Argument a;
    a.type = Argument::Custom;
    a.value.custom = { &arg, [](BufferWriter& writer, const State& state, const void* ptr) {
            typedef typename std::decay<Arg>::type ArgType;
            const ArgType& val = *reinterpret_cast<const ArgType*>(ptr);
            const std::string& str = val.to_string();
            print2_format_generic(writer, state, typename Argument::StringType { str.c_str(), str.size() });
        } };
    return a;
}

template<typename ...Args>
inline ArgumentStore<Args...>::ArgumentStore(Args&& ...a)
    : args{make_arg(a)...}
{
}

template<typename ...Args>
ArgumentStore<Args...> make_args(Args&& ...args)
{
    return {args...};
}

template<typename ...Args>
int snprint2(char* buffer, size_t bufsiz, const char* format, Args&& ...args)
{
    return print2_helper(buffer, bufsiz, format, Arguments(make_args(args...)));
}

#endif // PRINT2_H
