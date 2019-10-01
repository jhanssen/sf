#include "print2.h"

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

inline void clearState(State& state)
{
    state.flags = State::Flag_None;
    state.length = State::Length_None;
    state.width = state.precision = State::None;
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

template <std::size_t N, typename T>
constexpr std::array<T, N> make_array(const T& value)
{
    return detail::make_array(value, detail::make_index_sequence<N>());
}

template<char Pad>
void writePad(BufferWriter& writer, int num)
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
inline int print2_error(const char (&type)[N])
{
    fwrite(type, 1, N, stderr);
    fwrite("\n", 1, 2, stderr);
    fflush(stderr);
    abort();
    return 0;
}

template<typename ArgType, typename ReturnArgType = ArgType>
struct ArgumentGetter
{
    static ReturnArgType get(const Arguments& args, size_t idx);
};

template<>
struct ArgumentGetter<void*, uintptr_t>
{
    static uintptr_t get(const Arguments& args, size_t idx)
    {
        assert(idx < args.count);
        return reinterpret_cast<uintptr_t>(args.args[idx].value.ptr);
    }
};

template<>
struct ArgumentGetter<int*, int*>
{
    static int* get(const Arguments& args, size_t idx)
    {
        assert(idx < args.count);
        return reinterpret_cast<int*>(args.args[idx].value.ptr);
    }
};

template<>
struct ArgumentGetter<int64_t, int64_t>
{
    static int64_t get(const Arguments& args, size_t idx)
    {
        assert(idx < args.count);
        const auto& arg = args.args[idx];
        switch (arg.type) {
        case Argument::Int32:
            return static_cast<int64_t>(arg.value.i32);
        case Argument::Uint32:
            return static_cast<int64_t>(arg.value.u32);
        case Argument::Int64:
            return static_cast<int64_t>(arg.value.i64);
        case Argument::Uint64:
            return static_cast<int64_t>(arg.value.u64);
        default:
            return print2_error("Invalid int type");
        }
    }
};

template<>
struct ArgumentGetter<uint64_t, uint64_t>
{
    static uint64_t get(const Arguments& args, size_t idx)
    {
        assert(idx < args.count);
        const auto& arg = args.args[idx];
        switch (arg.type) {
        case Argument::Int32:
            return static_cast<uint64_t>(arg.value.i32);
        case Argument::Uint32:
            return static_cast<uint64_t>(arg.value.u32);
        case Argument::Int64:
            return static_cast<uint64_t>(arg.value.i64);
        case Argument::Uint64:
            return static_cast<uint64_t>(arg.value.u64);
        default:
            return print2_error("Invalid int type");
        }
    }
};

#define GET_ARG(tp, rtp, val)                                   \
    template<>                                                  \
    struct ArgumentGetter<tp, rtp>                              \
    {                                                           \
        static rtp get(const Arguments& args, size_t idx)       \
        {                                                       \
            assert(idx < args.count);                           \
            return static_cast<rtp>(args.args[idx].value.val);  \
        }                                                       \
    }

GET_ARG(typename Argument::StringType, typename Argument::StringType, str);
GET_ARG(int32_t, int32_t, i32);
GET_ARG(double, double, dbl);

#undef GET_ARG

inline void print2_format_buffer(BufferWriter& writer, const State& state, const char* buffer, size_t bufsiz, const char* extra, size_t extrasiz)
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

inline void print2_format_ch(BufferWriter& writer, const State& state, const Arguments& args, int argno)
{
    const uint32_t ch = static_cast<uint32_t>(ArgumentGetter<int32_t>::get(args, argno)) % 256;

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

template<typename ArgType>
void print2_format_float(BufferWriter& writer, const State& state, const Arguments& args, int argno)
{
    ArgType number = ArgumentGetter<ArgType>::get(args, argno);

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

template<typename ArgType>
void print2_format_float_exp(BufferWriter& writer, const State& state, const Arguments& args, int argno)
{
    ArgType number = ArgumentGetter<ArgType>::get(args, argno);

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

template<typename ArgType>
void print2_format_float_shortest(BufferWriter& writer, const State& state, const Arguments& args, int argno)
{
    ArgType number = ArgumentGetter<ArgType>::get(args, argno);

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

template<typename UnsignedArgType>
void print2_format_int_8(BufferWriter& writer, const State& state, const Arguments& args, int argno)
{
    typedef std::numeric_limits<UnsignedArgType> Info;

    UnsignedArgType number = ArgumentGetter<UnsignedArgType>::get(args, argno);

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


template<typename ArgType>
void print2_format_int_10(BufferWriter& writer, const State& state, const Arguments& args, int argno)
{
    // adapted from http://ideone.com/nrQfA8

    typedef typename std::make_unsigned<ArgType>::type UnsignedArgType;
    typedef std::numeric_limits<ArgType> Info;

    const ArgType arg = ArgumentGetter<ArgType>::get(args, argno);
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

template<typename ArgType, typename UnsignedArgType = ArgType>
void print2_format_int_16(BufferWriter& writer, const State& state, const char* alphabet, const Arguments& args, int argno)
{
    typedef std::numeric_limits<ArgType> Info;

    UnsignedArgType number = ArgumentGetter<ArgType, UnsignedArgType>::get(args, argno);

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

inline void print2_format_ptr(BufferWriter& writer, const State& state, const char* alphabet, const Arguments& args, int argno)
{
    uintptr_t number = ArgumentGetter<void*, uintptr_t>::get(args, argno);
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

void print2_format_generic(BufferWriter& writer, const State& state, const typename Argument::StringType& str)
{
    size_t sz = str.len;
    if (state.precision != State::None && static_cast<size_t>(state.precision) < sz) {
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

inline void print2_format_str(BufferWriter& writer, const State& state, const Arguments& args, int argno)
{
    const auto& arg = args.args[argno];
    switch (arg.type) {
    case Argument::String:
        print2_format_generic(writer, state, arg.value.str);
        break;
    case Argument::Custom:
        arg.value.custom.format(writer, state, arg.value.custom.data);
        break;
    default:
        // badness
        abort();
    }
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
    constexpr int mul = 10;
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

int print2_helper(char* buffer, size_t bufsiz, const char* format, const Arguments& args)
{
    State state;
    BufferWriter writer(buffer, bufsiz);

    int formatoff = 0;
    int arg = 0;
    for (;;) {
        switch (format[formatoff]) {
        case '%':
            clearState(state);
            if (format[formatoff + 1] != '%') {
                formatoff = print2_parse_state(format, formatoff + 1, state);
                if (state.width == State::Star)
                    state.width = ArgumentGetter<int32_t>::get(args, arg++);
                if (state.precision == State::Star)
                    state.precision = ArgumentGetter<int32_t>::get(args, arg++);
                switch (format[formatoff++]) {
                case 'd':
                case 'i':
                    print2_format_int_10<int64_t>(writer, state, args, arg++);
                    break;
                case 'u':
                    print2_format_int_10<uint64_t>(writer, state, args, arg++);
                    break;
                case 'o':
                    print2_format_int_8<uint64_t>(writer, state, args, arg++);
                    break;
                case 'x':
                    print2_format_int_16<uint64_t>(writer, state, "0123456789abcdefx", args, arg++);
                    break;
                case 'X':
                    print2_format_int_16<uint64_t>(writer, state, "0123456789ABCDEFX", args, arg++);
                    break;
                case 'f':
                case 'F':
                    print2_format_float<double>(writer, state, args, arg++);
                    break;
                case 'e':
                    print2_format_float_exp<double>(writer, state, args, arg++);
                    break;
                case 'g':
                    print2_format_float_shortest<double>(writer, state, args, arg++);
                    break;
                case 'E':
                case 'a':
                case 'A':
                case 'G':
                    return print2_error("E/a/A/G not supported");
                case 'c':
                    print2_format_ch(writer, state, args, arg++);
                    break;
                case 's':
                    print2_format_str(writer, state, args, arg++);
                    break;
                case 'p':
                    print2_format_ptr(writer, state, "0123456789abcdefx", args, arg++);
                    break;
                case 'n': {
                    int* ptr = ArgumentGetter<int*>::get(args, arg++);
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
