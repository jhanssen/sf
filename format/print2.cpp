#include <string>
#include <array>
#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <ryu/ryu2.h>

#include <chrono>

using namespace std::chrono;

struct State
{
    enum { None = -1, Star = -2 };
    enum Flags
    {
        Flag_None        = 0x00,
        Flag_LeftJustify = 0x02,
        Flag_Sign        = 0x04,
        Flag_Space       = 0x08,
        Flag_Prefix      = 0x10,
        Flag_ZeroPad     = 0x20
    };
    enum Length
    {
        Length_None,
        Length_hh,
        Length_h,
        Length_ll,
        Length_l,
        Length_j,
        Length_z,
        Length_t,
        Length_L
    };
    int32_t flags;
    Length length;
    int32_t width; // can be Star which means that an additional argument will contain the actual number
    int32_t precision; // can be Star which means that an additional argument will contain the actual number
};

struct BufferWriter
{
    BufferWriter(char* b, size_t s)
        : buffer(b), buffersize(s), bufferoff(0)
    {
    }

    char* buffer;
    size_t buffersize;
    size_t bufferoff;

    void put(char c) { if (bufferoff < buffersize) buffer[bufferoff++] = c; else ++bufferoff; }
    void put(const char* c, size_t s) { const ssize_t m = std::min<ssize_t>(s, buffersize - bufferoff); if (m > 0) { memcpy(buffer + bufferoff, c, m); } bufferoff += s; }

    size_t offset() const { return bufferoff; }
    size_t size() const { return buffersize; }
    size_t terminate() { if (bufferoff < buffersize) buffer[bufferoff] = '\0'; else buffer[buffersize - 1] = '\0'; return bufferoff; }
};

struct FileWriter
{
    FileWriter(FILE* f)
        : file(f), bufferoff(0)
    {
    }

    FILE* file;
    size_t bufferoff;

    void put(char c) { fputc(c, file); ++bufferoff; }
    void put(const char* c, size_t s) { fwrite(c, 1, s, file); bufferoff += s; }

    size_t offset() const { return bufferoff; }
    size_t size() const { return std::numeric_limits<size_t>::max(); }
    size_t terminate() { return bufferoff; }
};

inline void clearState(State& state)
{
    state.flags = State::Flag_None;
    state.length = State::Length_None;
    state.width = state.precision = State::None;
};

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

template<class T>
struct is_stringish : std::integral_constant<
    bool,
    is_c_string<T>::value ||
    has_global_to_string<T>::value ||
    has_member_to_string<T>::value ||
    std::is_same<std::string, typename std::decay<T>::type>::value>
{
};

template<size_t N>
constexpr size_t stringLength(const char (&)[N])
{
    return N;
}

static inline size_t stringLength(const char* str)
{
    return strlen(str);
}

template<class T>
struct is_float_double : std::integral_constant<
    bool,
    std::is_same<float, typename std::decay<T>::type>::value ||
    std::is_same<double, typename std::decay<T>::type>::value>
{
};

template<typename T> struct dependent_false : std::false_type
{
};

template <std::size_t N, typename T>
constexpr std::array<T, N> make_array(const T& value)
{
    return detail::make_array(value, detail::make_index_sequence<N>());
}

template<char Pad, typename Writer>
void writePad(Writer& writer, int num)
{
    enum { Size = 64 };

    constexpr auto a = make_array<Size>(Pad);

    while (num > Size) {
        writer.put(a.data(), Size);
        num -= Size;
    }
    if (num > 0) {
        writer.put(a.data(), num);
    }
}

template<typename Writer>
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
        void (*format)(Writer& writer, const State& state, const void* data);
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

template<typename Writer, typename ...Args>
struct ArgumentStore
{
    static const size_t ArgCount = sizeof...(Args);
    Argument<Writer> args[ArgCount];

    ArgumentStore(Args&& ...args);
};

template<typename Writer>
struct Arguments
{
    const Argument<Writer>* args;
    const size_t count;

    Arguments() : args(nullptr), count(0) { }
    template<typename ...Args>
    Arguments(ArgumentStore<Writer, Args...>&& store) : args(store.args), count(store.ArgCount) { }
};

template<size_t N>
inline int print2_error(const char (&type)[N])
{
    fwrite(type, 1, N, stderr);
    fwrite("\n", 1, 2, stderr);
    fflush(stderr);
    abort();
    return 0;
}

template<typename Writer, typename ArgType, typename ReturnArgType = ArgType>
struct ArgumentGetter
{
    static ReturnArgType get(const Arguments<Writer>& args, size_t idx);
};

template<typename Writer>
struct ArgumentGetter<Writer, void*, uintptr_t>
{
    static uintptr_t get(const Arguments<Writer>& args, size_t idx)
    {
        assert(idx < args.count);
        return reinterpret_cast<uintptr_t>(args.args[idx].value.ptr);
    }
};

template<typename Writer>
struct ArgumentGetter<Writer, int*, int*>
{
    static int* get(const Arguments<Writer>& args, size_t idx)
    {
        assert(idx < args.count);
        return reinterpret_cast<int*>(args.args[idx].value.ptr);
    }
};

template<typename Writer>
struct ArgumentGetter<Writer, int64_t, int64_t>
{
    static int64_t get(const Arguments<Writer>& args, size_t idx)
    {
        assert(idx < args.count);
        const auto& arg = args.args[idx];
        switch (arg.type) {
        case Argument<Writer>::Int32:
            return static_cast<int64_t>(arg.value.i32);
        case Argument<Writer>::Uint32:
            return static_cast<int64_t>(arg.value.u32);
        case Argument<Writer>::Int64:
            return static_cast<int64_t>(arg.value.i64);
        case Argument<Writer>::Uint64:
            return static_cast<int64_t>(arg.value.u64);
        default:
            return print2_error("Invalid int type");
        }
    }
};

template<typename Writer>
struct ArgumentGetter<Writer, uint64_t, uint64_t>
{
    static uint64_t get(const Arguments<Writer>& args, size_t idx)
    {
        assert(idx < args.count);
        const auto& arg = args.args[idx];
        switch (arg.type) {
        case Argument<Writer>::Int32:
            return static_cast<uint64_t>(arg.value.i32);
        case Argument<Writer>::Uint32:
            return static_cast<uint64_t>(arg.value.u32);
        case Argument<Writer>::Int64:
            return static_cast<uint64_t>(arg.value.i64);
        case Argument<Writer>::Uint64:
            return static_cast<uint64_t>(arg.value.u64);
        default:
            return print2_error("Invalid int type");
        }
    }
};

#define GET_ARG(tp, rtp, val)                                           \
    template<typename Writer>                                           \
    struct ArgumentGetter<Writer, tp, rtp>                              \
    {                                                                   \
        static rtp get(const Arguments<Writer>& args, size_t idx)       \
        {                                                               \
            assert(idx < args.count);                                   \
            return static_cast<rtp>(args.args[idx].value.val);          \
        }                                                               \
    }

GET_ARG(typename Argument<Writer>::StringType, typename Argument<Writer>::StringType, str);
GET_ARG(int32_t, int32_t, i32);
GET_ARG(double, double, dbl);

#undef GET_ARG

template<typename Writer>
void print2_format_buffer(Writer& writer, const State& state, const char* buffer, size_t bufsiz, const char* extra, size_t extrasiz)
{
    const bool left = state.flags & State::Flag_LeftJustify;

    int precision = 0;
    if (state.precision != State::None) {
        assert(state.precision >= 0);
        precision = state.precision;

        // precision of 0 means that the number 0 should not be emitted
        if (!precision && bufsiz == 1 && buffer[bufsiz] == '0')
            return;
    }

    const bool hasextra = extrasiz && extra[0] != 0;

    int pad = 0;
    if (state.width != State::None) {
        assert(state.width >= 0);
        pad = std::max<int>(0, state.width - (bufsiz + (hasextra ? extrasiz : 0)));
    }
    char padchar = ' ';
    if ((state.flags & State::Flag_ZeroPad) && !left && !precision)
        padchar = '0';

    if (precision)
        pad = std::max(0, pad - precision);

    if (hasextra && padchar == '0')
        writer.put(extra, extrasiz);

    if (pad && !left) {
        if (padchar == '0')
            writePad<'0'>(writer, pad);
        else
            writePad<' '>(writer, pad);
    }

    if (hasextra && padchar == ' ')
        writer.put(extra, extrasiz);

    if (precision) {
        writePad<'0'>(writer, precision);
    }

    writer.put(buffer, bufsiz);

    if (pad && left) {
        if (padchar == '0')
            writePad<'0'>(writer, pad);
        else
            writePad<' '>(writer, pad);
    }
}

template<typename Writer>
void print2_format_ch(Writer& writer, const State& state, const Arguments<Writer>& args, int argno)
{
    const uint32_t ch = static_cast<uint32_t>(ArgumentGetter<Writer, int32_t>::get(args, argno)) % 256;

    int pad = 0;
    if (state.width != State::None) {
        assert(state.width >= 0);
        pad = std::max<int>(0, state.width - 1);
    }

    if (pad && !(state.flags & State::Flag_LeftJustify)) {
        writePad<' '>(writer, pad);
    }

    writer.put(static_cast<char>(ch));

    if (pad && (state.flags & State::Flag_LeftJustify)) {
        writePad<' '>(writer, pad);
    }
}

template<typename Writer, typename ArgType>
void print2_format_float(Writer& writer, const State& state, const Arguments<Writer>& args, int argno)
{
    ArgType number = ArgumentGetter<Writer, ArgType>::get(args, argno);

    char extra = 0;
    if (number >= 0) {
        if (state.flags & State::Flag_Sign)
            extra = '+';
        else if (state.flags & State::Flag_Space)
            extra = ' ';
    } else {
        extra = '-';
        number = -number;
    }

    char buffer[2048];
    const int n = d2fixed_buffered_n(number, state.precision == State::None ? 6 : state.precision, buffer);

    print2_format_buffer(writer, state, buffer, n, &extra, 1);
}

template<typename Writer, typename ArgType>
void print2_format_float_exp(Writer& writer, const State& state, const Arguments<Writer>& args, int argno)
{
    ArgType number = ArgumentGetter<Writer, ArgType>::get(args, argno);

    char extra = 0;
    if (number >= 0) {
        if (state.flags & State::Flag_Sign)
            extra = '+';
        else if (state.flags & State::Flag_Space)
            extra = ' ';
    } else {
        extra = '-';
        number = -number;
    }

    char buffer[2048];
    const int n = d2exp_buffered_n(number, state.precision == State::None ? 6 : state.precision, buffer);

    print2_format_buffer(writer, state, buffer, n, &extra, 1);
}

template<typename Writer, typename ArgType>
void print2_format_float_shortest(Writer& writer, const State& state, const Arguments<Writer>& args, int argno)
{
    ArgType number = ArgumentGetter<Writer, ArgType>::get(args, argno);

    char extra = 0;
    if (number >= 0) {
        if (state.flags & State::Flag_Sign)
            extra = '+';
        else if (state.flags & State::Flag_Space)
            extra = ' ';
    } else {
        extra = '-';
        number = -number;
    }

    auto chop = [](const char* b, size_t n, int& from, int& len, bool& allzero) -> int {
        int sub1 = n;
        int sub2 = -1;
        int dummy = 0;
        int e = -1;
        int* what = &sub1;
        for (; n > 0; --n) {
            switch(b[n - 1]) {
            case '0':
            case '+':
                --*what;
                break;
            case 'e':
                --*what;
                if (what == &dummy) {
                    sub2 = e = n - 1;
                    what = &sub2;
                }
                break;
            case '.':
                ++*what;
                if (what != &dummy)
                    what = &dummy;
                break;
            default:
                allzero = false;
                if (what != &dummy)
                    what = &dummy;
                break;
            }
        }
        if (e != -1 && sub2 != -1) {
            from = sub2;
            len = e - sub2;
        }
        return sub1;
    };

    const int precision = state.precision == State::None ? 6 : state.precision;

    char buffer1[2048];
    char buffer2[2048];
    int n1 = d2exp_buffered_n(number, precision, buffer1);
    int n2 = d2fixed_buffered_n(number, precision, buffer2);
    int from1 = 0, len1 = 0;
    int from2 = 0, len2 = 0;
    bool az1 = true, az2 = true;
    n2 = chop(buffer2, n2, from2, len2, az2);
    n1 = chop(buffer1, n1, from1, len1, az1);
    if (n1 - len1 < n2 || (!az1 && az2)) {
        if (len1 > 0) {
            n1 -= len1;
            memmove(buffer1 + from1, buffer1 + from1 + len1, n1);
        }
        print2_format_buffer(writer, state, buffer1, n1, &extra, 1);
    } else {
        print2_format_buffer(writer, state, buffer2, n2, &extra, 1);
    }
}

template<typename Writer, typename UnsignedArgType>
void print2_format_int_8(Writer& writer, const State& state, const Arguments<Writer>& args, int argno)
{
    typedef std::numeric_limits<UnsignedArgType> Info;

    UnsignedArgType number = ArgumentGetter<Writer, UnsignedArgType>::get(args, argno);

    const int digits = Info::digits10;
    const int bufsize = digits + 2;

    char buffer[bufsize];
    char* bufptr = buffer;
    char extra = 0;

    if (state.flags & State::Flag_Prefix)
        extra = '0';

    if (number == 0) {
        *bufptr++ = '0';
        extra = 0;
    } else {
        char* p_first = bufptr;
        while (number != 0)
        {
            *bufptr++ = '0' + (number & 0x7);
            number >>= 3;
        }
        std::reverse(p_first, bufptr);
    }

    print2_format_buffer(writer, state, buffer, bufptr - buffer, &extra, 1);
}


template<typename Writer, typename ArgType>
void print2_format_int_10(Writer& writer, const State& state, const Arguments<Writer>& args, int argno)
{
    // adapted from http://ideone.com/nrQfA8

    typedef typename std::make_unsigned<ArgType>::type UnsignedArgType;
    typedef std::numeric_limits<ArgType> Info;

    const ArgType arg = ArgumentGetter<Writer, ArgType>::get(args, argno);
    UnsignedArgType unumber = arg < 0 ? -arg : arg;

    const int digits = Info::digits10;
    const int bufsize = digits + 2;

    char buffer[bufsize];
    char* bufptr = buffer;
    char extra = 0;

    if (arg == 0) {
        *bufptr++ = '0';
    } else {
        if (arg < 0) {
            extra = '-';
        } else if (state.flags & State::Flag_Sign) {
            extra = '+';
        } else if (state.flags & State::Flag_Space) {
            extra = ' ';
        }
        char* p_first = bufptr;
        while (unumber != 0)
        {
            *bufptr++ = '0' + unumber % 10;
            unumber /= 10;
        }
        std::reverse(p_first, bufptr);
    }

    print2_format_buffer(writer, state, buffer, bufptr - buffer, &extra, 1);
}

template<typename Writer, typename ArgType, typename UnsignedArgType = ArgType>
void print2_format_int_16(Writer& writer, const State& state, const char* alphabet, const Arguments<Writer>& args, int argno)
{
    typedef std::numeric_limits<ArgType> Info;

    UnsignedArgType number = ArgumentGetter<Writer, ArgType, UnsignedArgType>::get(args, argno);

    const int digits = Info::digits10;
    const int bufsize = digits + 2;

    char buffer[bufsize];
    char* bufptr = buffer;
    char extra[2] = { 0, 0 };

    if (state.flags & State::Flag_Prefix) {
        extra[0] = '0';
        extra[1] = alphabet[16];
    }

    if (number == 0) {
        *bufptr++ = '0';
    } else {
        char* p_first = bufptr;
        while (number != 0)
        {
            *bufptr++ = alphabet[number & 0xf];
            number >>= 4;
        }
        std::reverse(p_first, bufptr);
    }

    print2_format_buffer(writer, state, buffer, bufptr - buffer, extra, 2);
}

template<typename Writer>
void print2_format_ptr(Writer& writer, const State& state, const char* alphabet, const Arguments<Writer>& args, int argno)
{
    uintptr_t number = ArgumentGetter<Writer, void*, uintptr_t>::get(args, argno);
    typedef std::numeric_limits<uintptr_t> Info;

    const int digits = Info::digits10;
    const int bufsize = digits + 2;

    char buffer[bufsize];
    char* bufptr = buffer;
    char extra[2] = { '0', 'x' };

    if (number == 0) {
        *bufptr++ = '(';
        *bufptr++ = 'n';
        *bufptr++ = 'i';
        *bufptr++ = 'l';
        *bufptr++ = ')';
        extra[0] = 0;
    } else {
        char* p_first = bufptr;
        while (number != 0)
        {
            *bufptr++ = alphabet[number & 0xf];
            number >>= 4;
        }
        std::reverse(p_first, bufptr);
    }

    print2_format_buffer(writer, state, buffer, bufptr - buffer, extra, 2);
}

template<typename Writer>
void print2_format_generic(Writer& writer, const State& state, const typename Argument<Writer>::StringType& str)
{
    size_t sz = str.len;
    if (state.precision != State::None && state.precision < sz) {
        assert(state.precision >= 0);
        sz = state.precision;
    }
    int pad = 0;
    if (state.width != State::None) {
        assert(state.width >= 0);
        pad = std::max<int>(0, state.width - sz);
    }

    if (pad && !(state.flags & State::Flag_LeftJustify)) {
        writePad<' '>(writer, pad);
    }

    writer.put(str.str, sz);

    if (pad && (state.flags & State::Flag_LeftJustify)) {
        writePad<' '>(writer, pad);
    }
}

template<typename Writer>
void print2_format_str(Writer& writer, const State& state, const Arguments<Writer>& args, int argno)
{
    const auto& arg = args.args[argno];
    switch (arg.type) {
    case Argument<Writer>::String:
        print2_format_generic(writer, state, arg.value.str);
        break;
    case Argument<Writer>::Custom:
        arg.value.custom.format(writer, state, arg.value.custom.data);
        break;
    default:
        // badness
        abort();
    }
}

#define MAKE_ARITHMETIC_ARG(tp, itp, val)               \
    template<typename Writer>                           \
    Argument<Writer> make_arithmetic_arg(tp arg)        \
    {                                                   \
        Argument<Writer> a;                             \
        a.type = Argument<Writer>::itp;                 \
        a.value.val = static_cast<tp>(arg);             \
        return a;                                       \
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

template<typename Writer, typename Arg, typename std::enable_if<std::is_arithmetic<typename std::decay<Arg>::type>::value, void>::type* = nullptr>
Argument<Writer> make_arg(Arg&& arg)
{
    return make_arithmetic_arg<Writer>(static_cast<typename std::decay<Arg>::type>(arg));
}

template<typename Writer, typename Arg, typename std::enable_if<is_c_string<typename std::decay<Arg>::type>::value, void>::type* = nullptr>
Argument<Writer> make_arg(Arg&& arg)
{
    Argument<Writer> a;
    a.type = Argument<Writer>::String;
    a.value.str = { arg, strlen(arg) };
    return a;
}

template<typename Writer, typename Arg, typename std::enable_if<std::is_same<std::string, typename std::decay<Arg>::type>::value, void>::type* = nullptr>
Argument<Writer> make_arg(Arg&& arg)
{
    Argument<Writer> a;
    a.type = Argument<Writer>::String;
    a.value.str = { arg.c_str(), arg.size() };
    return a;
}

template<typename Writer, typename Arg, typename std::enable_if<std::is_same<int*, typename std::remove_reference<Arg>::type>::value, void>::type* = nullptr>
Argument<Writer> make_arg(Arg&& arg)
{
    Argument<Writer> a;
    a.type = Argument<Writer>::IntPointer;
    a.value.ptr = arg;
    return a;
}

template<typename Writer, typename Arg, typename std::enable_if<!std::is_same<int*, typename std::remove_reference<Arg>::type>::value && std::is_pointer<typename std::remove_reference<Arg>::type>::value, void>::type* = nullptr>
Argument<Writer> make_arg(Arg&& arg)
{
    Argument<Writer> a;
    a.type = Argument<Writer>::Pointer;
    a.value.ptr = arg;
    return a;
}

template<typename Writer, typename Arg, typename std::enable_if<std::is_same<std::nullptr_t, typename std::remove_reference<Arg>::type>::value, void>::type* = nullptr>
Argument<Writer> make_arg(Arg&& arg)
{
    Argument<Writer> a;
    a.type = Argument<Writer>::Pointer;
    a.value.ptr = nullptr;
    return a;
}

template<typename Writer, typename Arg, typename std::enable_if<has_global_to_string<typename std::decay<Arg>::type>::value, void>::type* = nullptr>
Argument<Writer> make_arg(Arg&& arg)
{
    Argument<Writer> a;
    a.type = Argument<Writer>::Custom;
    a.value.custom = { &arg, [](Writer& writer, const State& state, const void* ptr) {
            typedef typename std::decay<Arg>::type ArgType;
            const ArgType& val = *reinterpret_cast<const ArgType*>(ptr);
            const std::string& str = to_string(val);
            print2_format_generic(writer, state, typename Argument<Writer>::StringType { str.c_str(), str.size() });
        } };
    return a;
}

template<typename Writer, typename Arg, typename std::enable_if<has_member_to_string<typename std::decay<Arg>::type>::value, void>::type* = nullptr>
Argument<Writer> make_arg(Arg&& arg)
{
    Argument<Writer> a;
    a.type = Argument<Writer>::Custom;
    a.value.custom = { &arg, [](Writer& writer, const State& state, const void* ptr) {
            typedef typename std::decay<Arg>::type ArgType;
            const ArgType& val = *reinterpret_cast<const ArgType*>(ptr);
            const std::string& str = val.to_string();
            print2_format_generic(writer, state, typename Argument<Writer>::StringType { str.c_str(), str.size() });
        } };
    return a;
}

template<typename Writer, typename ...Args>
inline ArgumentStore<Writer, Args...>::ArgumentStore(Args&& ...args)
    : args{make_arg<Writer>(args)...}
{
}

inline int print2_parse_state(const char* format, int formatoff, State& state)
{
    enum { Parse_Flags, Parse_Width, Parse_Precision, Parse_Length } parseState = Parse_Flags;

    // Flags
    for (;; ++formatoff) {
        switch (format[formatoff]) {
        case '-':
            state.flags |= State::Flag_LeftJustify;
            break;
        case '+':
            state.flags |= State::Flag_Sign;
            break;
        case ' ':
            state.flags |= State::Flag_Space;
            break;
        case '#':
            state.flags |= State::Flag_Prefix;
            break;
        case '0':
            state.flags |= State::Flag_ZeroPad;
            break;
        case '\0':
            return print2_error("Zero termination encountered in flags extraction");
        default:
            parseState = Parse_Width;
            break;
        }
        if (parseState != Parse_Flags)
            break;
    }

    // Width
    state.width = 0;
    const int mul = 10;
    for (; parseState == Parse_Width; ++formatoff) {
        if (format[formatoff] >= '0' && format[formatoff] <= '9') {
            state.width *= mul;
            state.width += format[formatoff] - '0';
        } else if (format[formatoff] == '*') {
            state.width = State::Star;
            ++formatoff;
            parseState = Parse_Precision;
            break;
        } else if (format[formatoff] == '\0') {
            return print2_error("Zero termination encountered in width extraction");
        } else {
            state.width = std::min(state.width, 1024);
            parseState = Parse_Precision;
            break;
        }
    }

    // Precision
    if (format[formatoff] == '.') {
        ++formatoff;
        state.precision = 0;
        const int mul = 10;
        for (; parseState == Parse_Precision; ++formatoff) {
            if (format[formatoff] >= '0' && format[formatoff] <= '9') {
                state.precision *= mul;
                state.precision += format[formatoff] - '0';
            } else if (format[formatoff] == '*') {
                state.precision = State::Star;
                ++formatoff;
                parseState = Parse_Length;
                break;
            } else if (format[formatoff] == '\0') {
                return print2_error("Zero termination encountered in precision extraction");
            } else {
                state.precision = std::min(state.precision, 200);
                parseState = Parse_Length;
                break;
            }
        }
    } else {
        parseState = Parse_Length;
    }

    // Length
    switch (format[formatoff]) {
    case 'h':
        ++formatoff;
        if (format[formatoff + 1] == 'h') {
            state.length = State::Length_hh;
            ++formatoff;
        } else {
            state.length = State::Length_h;
        }
        break;
    case 'l':
        ++formatoff;
        if (format[formatoff + 1] == 'l') {
            state.length = State::Length_ll;
            ++formatoff;
        } else {
            state.length = State::Length_l;
        }
        break;
    case 'j':
        ++formatoff;
        state.length = State::Length_j;
        break;
    case 'z':
        ++formatoff;
        state.length = State::Length_z;
        break;
    case 't':
        ++formatoff;
        state.length = State::Length_t;
        break;
    case 'L':
        ++formatoff;
        state.length = State::Length_L;
        break;
    case '\0':
        return print2_error("Zero termination encountered in length extraction");
    }

    return formatoff;
}

template<typename Writer>
int print2_helper(Writer& writer, State& state, const char* format, const Arguments<Writer>& args)
{
    int formatoff = 0;
    int arg = 0;
    for (;;) {
        switch (format[formatoff]) {
        case '%':
            clearState(state);
            if (format[formatoff + 1] != '%') {
                formatoff = print2_parse_state(format, formatoff + 1, state);
                if (state.width == State::Star)
                    state.width = ArgumentGetter<Writer, int32_t>::get(args, arg++);
                if (state.precision == State::Star)
                    state.precision = ArgumentGetter<Writer, int32_t>::get(args, arg++);
                switch (format[formatoff++]) {
                case 'd':
                case 'i':
                    print2_format_int_10<Writer, int64_t>(writer, state, args, arg++);
                    break;
                case 'u':
                    print2_format_int_10<Writer, uint64_t>(writer, state, args, arg++);
                    break;
                case 'o':
                    print2_format_int_8<Writer, uint64_t>(writer, state, args, arg++);
                    break;
                case 'x':
                    print2_format_int_16<Writer, uint64_t>(writer, state, "0123456789abcdefx", args, arg++);
                    break;
                case 'X':
                    print2_format_int_16<Writer, uint64_t>(writer, state, "0123456789ABCDEFX", args, arg++);
                    break;
                case 'f':
                case 'F':
                    print2_format_float<Writer, double>(writer, state, args, arg++);
                    break;
                case 'e':
                    print2_format_float_exp<Writer, double>(writer, state, args, arg++);
                    break;
                case 'g':
                    print2_format_float_shortest<Writer, double>(writer, state, args, arg++);
                    break;
                case 'E':
                case 'a':
                case 'A':
                case 'G':
                    return print2_error("E/a/A/G not supported");
                case 'c':
                    print2_format_ch<Writer>(writer, state, args, arg++);
                    break;
                case 's':
                    print2_format_str(writer, state, args, arg++);
                    break;
                case 'p':
                    print2_format_ptr<Writer>(writer, state, "0123456789abcdefx", args, arg++);
                    break;
                case 'n': {
                    int* ptr = ArgumentGetter<Writer, int*>::get(args, arg++);
                    *ptr = static_cast<int>(writer.offset());
                    break; }
                default:
                    return print2_error("Invalid specifier");
                }
            } else {
                writer.put(format[++formatoff]);
                ++formatoff;
            }
            break;
        case '\0':
            return writer.terminate();
        default:
            writer.put(format[formatoff++]);
            break;
        }
    }

    return 0;
}

template<typename Writer, typename ...Args>
ArgumentStore<Writer, Args...> make_args(Args&& ...args)
{
    return {args...};
}

template<typename ...Args>
int print2(const char* format, Args&& ...args)
{
    State state;
    FileWriter writer(stdout);
    return print2_helper(writer, state, format, Arguments<FileWriter>(make_args<FileWriter>(args...)));
}

template<typename ...Args>
int snprint2(char* buffer, size_t bufsiz, const char* format, Args&& ...args)
{
    State state;
    BufferWriter writer(buffer, bufsiz);
    return print2_helper(writer, state, format, Arguments<BufferWriter>(make_args<BufferWriter>(args...)));
}

struct Foobar
{
    Foobar(const char* s, int i)
    {
        str.resize(100);
        str.resize(snprintf(&str[0], 100, "%s:%d", s, i));
    }

    std::string str;
};

std::string to_string(const Foobar& f)
{
    return f.str;
}

struct Foobar2
{
    Foobar2(const char* s, int i)
    {
        str.resize(100);
        str.resize(snprintf(&str[0], 100, "%s:%d", s, i));
    }

    std::string str;

    std::string to_string() const
    {
        return str;
    }
};

class Foobar3
{
    Foobar3(const char* s, int i)
        : str(s), ii(i)
    {
    }

    std::string str;
    int ii;

    Foobar2 to_string() const
    {
        return Foobar2(str.c_str(), ii);
    }
};

int main(int, char**)
{
    Foobar foobar("abc", 123);
    Foobar2 foobar2("trall", 42);
    int ting1 = 0, ting2 = 0;
    print2("hello %n%d %f '%*.*s' '%s' %n\n", &ting1, 99, 1.234, 10, 2, "hi ho", foobar, &ting2);
    print2("got ting %d %d (%s) %p\n", ting1, ting2, foobar2, &foobar2);
    //print2("hello %d '%s'\n", 2, "foobar");

    std::string tang = "tang";
    char buffer1[1024];
    char buffer2[1024];

    int fn1, fn2, r1, r2;

    enum { Iter = 10000 };

    auto t1 = steady_clock::now();
    for (int i = 0; i < Iter; ++i) {
        //snprint(buffer, sizeof(buffer), "hello2 %f\n", 12234.15281);
        //snprint(buffer, sizeof(buffer), "hello2 %20s\n", "hipphipp");
        r1 = snprint2(buffer1, sizeof(buffer1), "hello2 %#x%s%*u%p%s%f%-+20d\n%n", 1234567, "jappja", 140, 12345, &fn1, "trall og trall", 123.456, 99, &fn1);
        //r1 = snprint2(buffer1, sizeof(buffer1), "h%p\n%n", &foobar, &fn1);
        //r1 = snprint2(buffer1, sizeof(buffer1), "h%p\n%n", &fn1, &fn1);
    }

    auto t2 = steady_clock::now();
    double delta1 = duration_cast<nanoseconds>(t2 - t1).count() / static_cast<double>(Iter);

    auto t3 = steady_clock::now();
    for (int i = 0; i < Iter; ++i) {
        //snprintf(buffer, sizeof(buffer), "hello1 %f\n", 12234.15281);
        //snprintf(buffer, sizeof(buffer), "hello1 %20s\n", "hipphipp");
        r2 = snprintf(buffer2, sizeof(buffer2), "hello2 %#x%s%*u%p%s%f%-+20d\n%n", 1234567, "jappja", 140, 12345, &fn1, "trall og trall", 123.456, 99, &fn2);
        //r2 = snprintf(buffer2, sizeof(buffer2), "h%p\n%n", &foobar, &fn2);
        //r2 = snprint2(buffer2, sizeof(buffer2), "h%p\n%n", &fn1, &fn2);
    }

    auto t4 = steady_clock::now();
    double delta2 = duration_cast<nanoseconds>(t4 - t3).count() / static_cast<double>(Iter);

    // verify
    bool ok = true;
    int off = 0;
    if (fn1 == fn2 && r1 == r2 && fn1 > 0) {
        for (off = 0; off < fn1; ++off) {
            if (buffer1[off] != buffer2[off]) {
                ok = false;
                break;
            }
        }
    } else {
        ok = false;
    }

    if (ok) {
        printf("took, me   %f\n", delta1);
        printf("took, them %f\n", delta2);

        printf("verified %d\n", fn1);
    } else {
        printf("verify failed at %d (%d,%d) - (%d,%d)\n", off, fn1, fn2, r1, r2);
    }

    return 0;
}
