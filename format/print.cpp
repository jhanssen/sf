#include <string>
#include <array>
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
    // enum class Length
    // {
    //     None, hh, h, ll, l, j, z, t, L
    // };
    enum Specifier
    {
        Spec_None,
        Spec_d,
        Spec_i,
        Spec_u,
        Spec_o,
        Spec_x,
        Spec_X,
        Spec_f,
        Spec_F,
        Spec_e,
        Spec_E,
        Spec_g,
        Spec_G,
        Spec_a,
        Spec_A,
        Spec_c,
        Spec_s,
        Spec_p,
        Spec_n
    };
    int32_t flags;
    // Length length;
    int32_t width; // can be Star which means that an additional argument will contain the actual number
    int32_t precision; // can be Star which means that an additional argument will contain the actual number
    Specifier specifier;
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
    // state.length = State::Length::None;
    state.width = state.precision = State::None;
    state.specifier = State::Spec_None;
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
template<typename, typename = void> struct has_member_to_string_ref : std::false_type {};
template<typename, typename = void> struct has_member_to_string_ptr : std::false_type {};
template<typename T> struct has_global_to_string<T, void_t<decltype(to_string(std::declval<T>()))> > : std::true_type {};
template<typename T> struct has_member_to_string_ref<T, void_t<decltype(std::declval<T>().to_string())> > : std::true_type {};
template<typename T> struct has_member_to_string_ptr<T, void_t<decltype(std::declval<T>()->to_string())> > : std::true_type {};

template<class T>
struct is_c_string : std::integral_constant<
    bool,
    std::is_same<const char*, typename std::decay<T>::type>::value ||
    std::is_same<char*, typename std::decay<T>::type>::value>
{
};

template<class T>
struct is_stringish : std::integral_constant<
    bool,
    is_c_string<T>::value ||
    has_global_to_string<T>::value ||
    has_member_to_string_ref<T>::value ||
    has_member_to_string_ptr<T>::value ||
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

template<size_t N>
inline int print_error(const char (&type)[N], State& state, const char* format, size_t formatoff)
{
    fwrite(type, 1, N, stderr);
    fwrite("\n", 1, 2, stderr);
    fflush(stderr);
    abort();
}

template<typename Writer, typename ...Args>
int print_execute_helper(State& state, Writer& writer, const char* buffer, size_t bufsiz, const char* extra, size_t extrasiz, const char* format, size_t formatoff, Args&& ...args)
{
    const bool left = state.flags & State::Flag_LeftJustify;

    int precision = 0;
    if (state.precision != State::None) {
        assert(state.precision >= 0);
        precision = state.precision;

        // precision of 0 means that the number 0 should not be emitted
        if (!precision && bufsiz == 1 && buffer[bufsiz] == '0')
            return print_helper(state, writer, format, formatoff, std::forward<Args>(args)...);
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

    // do stuff
    return print_helper(state, writer, format, formatoff, std::forward<Args>(args)...);
}

template<typename Writer, typename ...Args>
int print_helper(State& state, Writer& writer, const char* format, size_t formatoff, Args&& ...args);

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<std::is_integral<typename std::decay<Arg>::type>::value, void>::type* = nullptr>
int print_execute_int_10_helper(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    // adapted from http://ideone.com/nrQfA8

    typedef typename std::decay<Arg>::type ArgType;
    typedef typename std::make_unsigned<ArgType>::type UnsignedArgType;
    typedef std::numeric_limits<ArgType> Info;

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

    return print_execute_helper(state, writer, buffer, bufptr - buffer, &extra, 1, format, formatoff, std::forward<Args>(args)...);
}

template<bool Signed, typename Writer, typename Arg, typename ...Args, typename std::enable_if<std::is_integral<typename std::decay<Arg>::type>::value, void>::type* = nullptr>
int print_execute_int_10(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    if (Signed) {
        return print_execute_int_10_helper(state, writer, format, formatoff, static_cast<typename std::make_signed<typename std::decay<Arg>::type>::type>(arg), std::forward<Args>(args)...);
    } else {
        return print_execute_int_10_helper(state, writer, format, formatoff, static_cast<typename std::make_unsigned<typename std::decay<Arg>::type>::type>(arg), std::forward<Args>(args)...);
    }
}

template<bool Signed, typename Writer, typename Arg, typename ...Args, typename std::enable_if<!std::is_integral<typename std::decay<Arg>::type>::value, void>::type* = nullptr>
int print_execute_int_10(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    return print_error("Argument 10 is not integral", state, format, formatoff);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<std::is_integral<typename std::decay<Arg>::type>::value, void>::type* = nullptr>
int print_execute_int_16(State& state, Writer& writer, const char* format, size_t formatoff, const char* alphabet, Arg&& arg, Args&& ...args)
{
    typedef typename std::decay<Arg>::type ArgType;
    typedef typename std::make_unsigned<ArgType>::type UnsignedArgType;
    typedef std::numeric_limits<ArgType> Info;

    UnsignedArgType number = static_cast<UnsignedArgType>(arg);

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

    return print_execute_helper(state, writer, buffer, bufptr - buffer, extra, 2, format, formatoff, std::forward<Args>(args)...);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<!std::is_integral<typename std::decay<Arg>::type>::value, void>::type* = nullptr>
int print_execute_int_16(State& state, Writer& writer, const char* format, size_t formatoff, const char* alphabet, Arg&& arg, Args&& ...args)
{
    return print_error("Argument 16 is not integral", state, format, formatoff);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<std::is_integral<typename std::decay<Arg>::type>::value, void>::type* = nullptr>
int print_execute_int_8(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    typedef typename std::decay<Arg>::type ArgType;
    typedef typename std::make_unsigned<ArgType>::type UnsignedArgType;
    typedef std::numeric_limits<ArgType> Info;

    UnsignedArgType number = static_cast<UnsignedArgType>(arg);

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

    return print_execute_helper(state, writer, buffer, bufptr - buffer, &extra, 1, format, formatoff, std::forward<Args>(args)...);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<!std::is_integral<typename std::decay<Arg>::type>::value, void>::type* = nullptr>
int print_execute_int_8(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    return print_error("Argument 8 is not integral", state, format, formatoff);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<std::is_pointer<typename std::decay<Arg>::type>::value, void>::type* = nullptr>
int print_execute_ptr(State& state, Writer& writer, const char* format, size_t formatoff, const char* alphabet, Arg&& arg, Args&& ...args)
{
    uintptr_t number = reinterpret_cast<uintptr_t>(arg);
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

    return print_execute_helper(state, writer, buffer, bufptr - buffer, extra, 2, format, formatoff, std::forward<Args>(args)...);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<!std::is_pointer<typename std::decay<Arg>::type>::value, void>::type* = nullptr>
int print_execute_ptr(State& state, Writer& writer, const char* format, size_t formatoff, const char* alphabet, Arg&& arg, Args&& ...args)
{
    return print_error("Argument is not pointer", state, format, formatoff);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<is_float_double<Arg>::value, void>::type* = nullptr>
int print_execute_float(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    Arg number = arg;

    char extra = 0;
    if (arg >= 0) {
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

    return print_execute_helper(state, writer, buffer, n, &extra, 1, format, formatoff, std::forward<Args>(args)...);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<std::is_same<long double, typename std::decay<Arg>::type>::value, void>::type* = nullptr>
int print_execute_float(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    return print_error("Long double not implemented", state, format, formatoff);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<!std::is_floating_point<Arg>::value, void>::type* = nullptr>
int print_execute_float(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    return print_error("Argument is not a floating point", state, format, formatoff);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<is_float_double<Arg>::value, void>::type* = nullptr>
int print_execute_float_shortest(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    Arg number = arg;

    char extra = 0;
    if (arg >= 0) {
        if (state.flags & State::Flag_Sign)
            extra = '+';
        else if (state.flags & State::Flag_Space)
            extra = ' ';
    } else {
        extra = '-';
        number = -number;
    }

    char buffer[2048];
    const int pr = state.precision == State::None ? 6 : state.precision;
    int n = d2fixed_buffered_n(number, pr, buffer);

    int sig = 0, i = 0;
    bool done = false, dot = false;
    done = false;
    for (; i < n; ++i) {
        switch (buffer[i]) {
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            ++sig;
            if (sig == pr)
                done = true;
            break;
        case '.':
            dot = true;
            break;
        }
        if (done) {
            ++i;
            break;
        }
    }

    if (dot || buffer[i] == '.') {
        return print_execute_helper(state, writer, buffer, i, &extra, 1, format, formatoff, std::forward<Args>(args)...);
    }

    n = d2exp_buffered_n(number, std::max(pr - 1, 0), buffer);
    const int orig = n;
    done = false;
    while (!done && n > 0) {
        switch(buffer[n - 1]) {
        case '0':
        case '+':
            --n;
            break;
        case 'e':
            --n;
            done = true;
            break;
        default:
            n = orig;
            done = true;
            break;
        }
    }

    return print_execute_helper(state, writer, buffer, n, &extra, 1, format, formatoff, std::forward<Args>(args)...);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<std::is_same<long double, typename std::decay<Arg>::type>::value, void>::type* = nullptr>
int print_execute_float_shortest(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    return print_error("Long double not implemented", state, format, formatoff);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<!std::is_floating_point<Arg>::value, void>::type* = nullptr>
int print_execute_float_shortest(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    return print_error("Argument is not a floating point", state, format, formatoff);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<std::is_same<int*, typename std::decay<Arg>::type>::value, void>::type* = nullptr>
int print_execute_store(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    *arg = static_cast<int>(writer.offset());
    return print_helper(state, writer, format, formatoff, std::forward<Args>(args)...);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<!std::is_same<int*, typename std::decay<Arg>::type>::value, void>::type* = nullptr>
int print_execute_store(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    return print_error("Argument is not an int pointer", state, format, formatoff);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<is_c_string<Arg>::value, void>::type* = nullptr>
int print_execute_str(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    size_t sz = stringLength(arg);
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

    writer.put(arg, sz);

    if (pad && (state.flags & State::Flag_LeftJustify)) {
        writePad<' '>(writer, pad);
    }

    return print_helper(state, writer, format, formatoff, std::forward<Args>(args)...);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<std::is_same<std::string, typename std::decay<Arg>::type>::value, void>::type* = nullptr>
int print_execute_str(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    size_t sz = arg.size();
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

    writer.put(arg.c_str(), sz);

    if (pad && (state.flags & State::Flag_LeftJustify)) {
        writePad<' '>(writer, pad);
    }

    return print_helper(state, writer, format, formatoff, std::forward<Args>(args)...);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<has_global_to_string<Arg>::value, void>::type* = nullptr>
int print_execute_str(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    return print_execute_str(state, writer, format, formatoff, to_string(std::forward<Arg>(arg)), std::forward<Args>(args)...);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<has_member_to_string_ref<Arg>::value, void>::type* = nullptr>
int print_execute_str(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    return print_execute_str(state, writer, format, formatoff, arg.to_string(), std::forward<Args>(args)...);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<has_member_to_string_ptr<Arg>::value, void>::type* = nullptr>
int print_execute_str(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    return print_execute_str(state, writer, format, formatoff, arg->to_string(), std::forward<Args>(args)...);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<!is_stringish<Arg>::value, void>::type* = nullptr>
int print_execute_str(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    return print_error("Argument is not a stringish", state, format, formatoff);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<std::is_integral<Arg>::value, void>::type* = nullptr>
int print_execute_ch(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    typedef typename std::make_unsigned<typename std::decay<Arg>::type>::type UnsignedArgType;

    const UnsignedArgType ch = static_cast<UnsignedArgType>(arg) % 256;

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

    return print_helper(state, writer, format, formatoff, std::forward<Args>(args)...);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<!std::is_integral<Arg>::value, void>::type* = nullptr>
int print_execute_ch(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    return print_error("Argument is not a char", state, format, formatoff);
}

template<typename Writer, typename Arg, typename ...Args>
int print_execute(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    switch (format[formatoff]) {
    case 'd':
    case 'i':
        return print_execute_int_10<true>(state, writer, format, formatoff + 1, std::forward<Arg>(arg), std::forward<Args>(args)...);
    case 'u':
        return print_execute_int_10<false>(state, writer, format, formatoff + 1, std::forward<Arg>(arg), std::forward<Args>(args)...);
    case 'o':
        return print_execute_int_8(state, writer, format, formatoff + 1, std::forward<Arg>(arg), std::forward<Args>(args)...);
    case 'x':
        return print_execute_int_16(state, writer, format, formatoff + 1, "0123456789abcdefx", std::forward<Arg>(arg), std::forward<Args>(args)...);
    case 'X':
        return print_execute_int_16(state, writer, format, formatoff + 1, "0123456789ABCDEFX", std::forward<Arg>(arg), std::forward<Args>(args)...);
    case 'f':
    case 'F':
        return print_execute_float(state, writer, format, formatoff + 1, std::forward<Arg>(arg), std::forward<Args>(args)...);
    case 'e':
    case 'E':
    case 'a':
    case 'A':
    case 'G':
        return print_error("e/E/a/A/G not implemented", state, format, formatoff);
    case 'g':
        return print_execute_float_shortest(state, writer, format, formatoff + 1, std::forward<Arg>(arg), std::forward<Args>(args)...);
    case 'c':
        return print_execute_ch(state, writer, format, formatoff + 1, std::forward<Arg>(arg), std::forward<Args>(args)...);
    case 's':
        return print_execute_str(state, writer, format, formatoff + 1, std::forward<Arg>(arg), std::forward<Args>(args)...);
    case 'p':
        return print_execute_ptr(state, writer, format, formatoff + 1, "0123456789abcdefx", std::forward<Arg>(arg), std::forward<Args>(args)...);
    case 'n':
        return print_execute_store(state, writer, format, formatoff + 1, std::forward<Arg>(arg), std::forward<Args>(args)...);
    default:
        return print_error("Invalid specifier", state, format, formatoff);
    }
    return print_helper(state, writer, format, formatoff + 1, std::forward<Args>(args)...);
}

template<typename Writer>
int print_execute(State& state, Writer& writer, const char* format, size_t formatoff)
{
    return print_error("Not enough arguments", state, format, formatoff);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<std::is_same<int, typename std::decay<Arg>::type>::value, void>::type* = nullptr>
int print_get_precision_argument(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    state.precision = arg;
    return print_execute(state, writer, format, formatoff, std::forward<Args>(args)...);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<!std::is_same<int, typename std::decay<Arg>::type>::value, void>::type* = nullptr>
int print_get_precision_argument(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    return print_error("Invalid precision argument", state, format, formatoff);
}

template<typename Writer>
int print_get_precision_argument(State& state, Writer& writer, const char* format, size_t formatoff)
{
    return print_error("No precision argument", state, format, formatoff);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<std::is_same<int, typename std::decay<Arg>::type>::value, void>::type* = nullptr>
int print_get_width_argument(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    state.width = arg;
    if (state.precision == State::Star) {
        return print_get_precision_argument(state, writer, format, formatoff, std::forward<Args>(args)...);
    }
    return print_execute(state, writer, format, formatoff, std::forward<Args>(args)...);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<!std::is_same<int, typename std::decay<Arg>::type>::value, void>::type* = nullptr>
int print_get_width_argument(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    return print_error("Invalid width argument", state, format, formatoff);
}

template<typename Writer>
int print_get_width_argument(State& state, Writer& writer, const char* format, size_t formatoff)
{
    return print_error("No width argument", state, format, formatoff);
}

template<typename Writer, typename ...Args>
int print_get_length(State& state, Writer& writer, const char* format, size_t formatoff, Args&& ...args)
{
    switch (format[formatoff]) {
    case 'h':
        ++formatoff;
        if (format[formatoff + 1] == 'h')
            ++formatoff;
        break;
    case 'l':
        ++formatoff;
        if (format[formatoff + 1] == 'l')
            ++formatoff;
        break;
    case 'j':
    case 'z':
    case 't':
    case 'L':
        ++formatoff;
        break;
    case '\0':
        return print_error("Zero termination encountered in length extraction", state, format, formatoff);
    }

    if (state.width == State::Star) {
        return print_get_width_argument(state, writer, format, formatoff, std::forward<Args>(args)...);
    } else if (state.precision == State::Star) {
        return print_get_precision_argument(state, writer, format, formatoff, std::forward<Args>(args)...);
    } else {
        return print_execute(state, writer, format, formatoff, std::forward<Args>(args)...);
    }
}

template<typename Writer, typename ...Args>
int print_get_precision(State& state, Writer& writer, const char* format, size_t formatoff, Args&& ...args)
{
    if (format[formatoff] != '.')
        return print_get_length(state, writer, format, formatoff, std::forward<Args>(args)...);
    ++formatoff;
    state.precision = 0;
    const int mul = 10;
    for (;; ++formatoff) {
        if (format[formatoff] >= '0' && format[formatoff] <= '9') {
            state.precision *= mul;
            state.precision += format[formatoff] - '0';
        } else if (format[formatoff] == '*') {
            state.precision = State::Star;
            return print_get_length(state, writer, format, formatoff + 1, std::forward<Args>(args)...);
        } else if (format[formatoff] == '\0') {
            return print_error("Zero termination encountered in precision extraction", state, format, formatoff);
        } else {
            return print_get_length(state, writer, format, formatoff, std::forward<Args>(args)...);
        }
    }
}

template<typename Writer, typename ...Args>
int print_get_width(State& state, Writer& writer, const char* format, size_t formatoff, Args&& ...args)
{
    state.width = 0;
    const int mul = 10;
    for (;; ++formatoff) {
        if (format[formatoff] >= '0' && format[formatoff] <= '9') {
            state.width *= mul;
            state.width += format[formatoff] - '0';
        } else if (format[formatoff] == '*') {
            state.width = State::Star;
            return print_get_precision(state, writer, format, formatoff + 1, std::forward<Args>(args)...);
        } else if (format[formatoff] == '\0') {
            return print_error("Zero termination encountered in width extraction", state, format, formatoff);
        } else {
            return print_get_precision(state, writer, format, formatoff, std::forward<Args>(args)...);
        }
    }
}

template<typename Writer, typename ...Args>
int print_get_flags(State& state, Writer& writer, const char* format, size_t formatoff, Args&& ...args)
{
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
            return print_error("Zero termination encountered in flags extraction", state, format, formatoff);
        default:
            return print_get_width(state, writer, format, formatoff, std::forward<Args>(args)...);
        }
    }
}

template<typename Writer, typename ...Args>
int print_helper(State& state, Writer& writer, const char* format, size_t formatoff, Args&& ...args)
{
    for (;;) {
        switch (format[formatoff]) {
        case '%':
            clearState(state);
            if (format[formatoff + 1] != '%') {
                return print_get_flags(state, writer, format, formatoff + 1, std::forward<Args>(args)...);
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
}

template<typename ...Args>
int snprint(char* buffer, size_t size, const char* format, Args&& ...args)
{
    State state;
    BufferWriter writer(buffer, size);
    return print_helper(state, writer, format, 0, std::forward<Args>(args)...);
}

template<typename ...Args>
int print(const char* format, Args&& ...args)
{
    State state;
    FileWriter writer(stdout);
    return print_helper(state, writer, format, 0, std::forward<Args>(args)...);
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
    // char buf[1024];
    Foobar foobar("abc", 123);
    Foobar2 foobar3("trall", 42);
    print("hello '%.*s' '%s' '%10.7s'\n", 2, "trakk", foobar, foobar3);
    // printf("%d -> '%s'\n", i, buf);
    //const int i = snprint(buf, sizeof(buf), "hello %s\n", "hello");

    std::string tang = "tang";
    char buffer1[1024];
    char buffer2[1024];

    int fn1, fn2, r1, r2;

    enum { Iter = 10000 };

    auto t1 = steady_clock::now();
    for (int i = 0; i < Iter; ++i) {
        //snprint(buffer, sizeof(buffer), "hello2 %f\n", 12234.15281);
        //snprint(buffer, sizeof(buffer), "hello2 %20s\n", "hipphipp");
        r1 = snprint(buffer1, sizeof(buffer1), "hello2 %#x%s%*u%p%s%f%-+20d\n%n", 1234567, "jappja", 140, 12345, &fn1, "trall og trall", 123.456, 99, &fn1);
    }

    auto t2 = steady_clock::now();
    double delta1 = duration_cast<nanoseconds>(t2 - t1).count() / static_cast<double>(Iter);

    auto t3 = steady_clock::now();
    for (int i = 0; i < Iter; ++i) {
        //snprintf(buffer, sizeof(buffer), "hello1 %f\n", 12234.15281);
        //snprintf(buffer, sizeof(buffer), "hello1 %20s\n", "hipphipp");
        r2 = snprintf(buffer2, sizeof(buffer2), "hello2 %#x%s%*u%p%s%f%-+20d\n%n", 1234567, "jappja", 140, 12345, &fn1, "trall og trall", 123.456, 99, &fn2);
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
