#include <string>
#include <vector>
#include <stdio.h>
#include <string.h>
#include <assert.h>

struct State
{
    enum class Flags
    {
        None,
        LeftJustify,
        Sign,
        Space,
        Prefix,
        ZeroPad
    };
    enum class Length
    {
        None, hh, h, ll, l, j, z, t, L
    };
    enum class Specifier
    {
        None, d, i, u, o, x, X, f, F, e, E, g, G, a, A, c, s, p, n
    };
    Flags flags;
    Length length;
    int width; // -1 means *, additional argument will contain the actual number
    int precision; // // -1 means *, additional argument will contain the actual number
    Specifier specifier;
};

struct BufferWriter
{
    char* buffer { nullptr };
    size_t buffersize { 0 };
    size_t bufferoff { 0 };

    void put(char c) { buffer[bufferoff++] = c; }
    void put(const char* c, size_t s) { const size_t m = std::min(s, buffersize - bufferoff); memcpy(buffer + bufferoff, c, m); bufferoff += m; }
    void put(const char* c) { const size_t m = std::min(strlen(c), buffersize - bufferoff); memcpy(buffer + bufferoff, c, m); bufferoff += m; }
    template<size_t N>
    void put(const char (&c)[N]) { const size_t m = std::min(N, buffersize - bufferoff); memcpy(buffer + bufferoff, c, m); bufferoff += m; }

    size_t offset() const { return bufferoff; }
    size_t size() const { return buffersize; }
    size_t terminate() { if (bufferoff < buffersize) buffer[bufferoff] = '\0'; return bufferoff; }
};

struct FileWriter
{
    FILE* file;
    size_t bufferoff { 0 };

    void put(char c) { fputc(c, file); ++bufferoff; }
    void put(const char* c, size_t s) { fwrite(c, 1, s, file); bufferoff += s; }
    void put(const char* c) { const size_t s = strlen(c); fwrite(c, 1, s, file); bufferoff += s; }
    template<size_t N>
    void put(const char (&c)[N]) { fwrite(c, 1, N, file); bufferoff += N; }

    size_t offset() const { return bufferoff; }
    size_t size() const { return std::numeric_limits<size_t>::max(); }
    size_t terminate() { fputc('\0', file); return bufferoff; }
};

State::Flags& operator|=(State::Flags& l, State::Flags r)
{
    (*reinterpret_cast<std::underlying_type<State::Flags>::type*>(&l)) |= static_cast<uint32_t>(r);
    return l;
}

inline void clearState(State& state)
{
    state.flags = State::Flags::None;
    state.length = State::Length::None;
    state.width = state.precision = 0;
    state.specifier = State::Specifier::None;
};

template<size_t N>
inline int print_error(const char (&type)[N], State& state, const char* format, size_t formatoff)
{
    fwrite(type, 1, N, stderr);
    fwrite("\n", 1, 2, stderr);
    fflush(stderr);
    abort();
}

template<typename Writer, typename ...Args>
int print_helper(State& state, Writer& writer, const char* format, size_t formatoff, Args&& ...args);

template<typename Writer, typename Arg, typename ...Args>
int print_execute_int(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    if (!std::is_integral<Arg>::value)
        return print_error("not an int", state, format, formatoff);
    // do stuff
    return print_helper(state, writer, format, formatoff, std::forward<Args>(args)...);
}

template<typename, typename = void>
struct has_global_to_string : std::false_type
{
};

template<typename T>
struct has_global_to_string<T, std::void_t<decltype(to_string(std::declval<T>()))> > : std::true_type
{
};

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
    std::is_same<std::string, typename std::decay<T>::type>::value>
{
};

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

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<is_c_string<Arg>::value, void>::type* = nullptr>
int print_execute_str(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    writer.put(arg);
    return print_helper(state, writer, format, formatoff, std::forward<Args>(args)...);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<std::is_same<typename std::decay<Arg>::type, std::string>::value, void>::type* = nullptr>
int print_execute_str(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    writer.put(arg.c_str(), arg.size());
    return print_helper(state, writer, format, formatoff, std::forward<Args>(args)...);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<has_global_to_string<Arg>::value, void>::type* = nullptr>
int print_execute_str(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    return print_execute_str(state, writer, format, formatoff, to_string(std::forward<Arg>(arg)), std::forward<Args>(args)...);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<!is_stringish<Arg>::value, void>::type* = nullptr>
int print_execute_str(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    return print_error("not a stringish", state, format, formatoff);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<std::is_same<float, typename std::decay<Arg>::type>::value, void>::type* = nullptr>
int print_execute_float(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    return print_helper(state, writer, format, formatoff, std::forward<Args>(args)...);
}

template<typename Writer, typename Arg, typename ...Args, typename std::enable_if<!std::is_arithmetic<Arg>::value, void>::type* = nullptr>
int print_execute_float(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    return print_error("not an arithmetic", state, format, formatoff);
}

template<typename Writer, typename Arg, typename ...Args>
int print_execute(State& state, Writer& writer, const char* format, size_t formatoff, Arg&& arg, Args&& ...args)
{
    switch (format[formatoff]) {
    case 'd':
    case 'i':
        return print_execute_int(state, writer, format, formatoff + 1, std::forward<Arg>(arg), std::forward<Args>(args)...);
    case 'u':
        break;
    case 'o':
        break;
    case 'x':
        break;
    case 'X':
        break;
    case 'f':
        return print_execute_float(state, writer, format, formatoff + 1, std::forward<Arg>(arg), std::forward<Args>(args)...);
    case 'F':
        break;
    case 'e':
    case 'E':
    case 'a':
    case 'A':
        break;
    case 'g':
        break;
    case 'G':
        break;
    case 'c':
        break;
    case 's':
        return print_execute_str(state, writer, format, formatoff + 1, std::forward<Arg>(arg), std::forward<Args>(args)...);
    case 'p':
        break;
    case 'n':
        break;
    default:
        return print_error("specifier", state, format, formatoff);
        break;
    }
    return print_helper(state, writer, format, formatoff + 1, std::forward<Args>(args)...);
}

template<typename Writer>
int print_execute(State& state, Writer& writer, const char* format, size_t formatoff)
{
    return writer.offset();
}

template<typename Writer, typename ...Args>
int print_get_length(State& state, Writer& writer, const char* format, size_t formatoff, Args&& ...args)
{
    switch (format[formatoff]) {
    case 'h':
        if (format[formatoff + 1] == 'h') {
            state.length = State::Length::hh;
            formatoff += 2;
        } else {
            state.length = State::Length::h;
            ++formatoff;
        }
        break;
    case 'l':
        if (format[formatoff + 1] == 'l') {
            state.length = State::Length::ll;
            formatoff += 2;
        } else {
            state.length = State::Length::l;
            ++formatoff;
        }
        break;
    case 'j':
        state.length = State::Length::l;
        ++formatoff;
        break;
    case 'z':
        state.length = State::Length::z;
        ++formatoff;
        break;
    case 't':
        state.length = State::Length::t;
        ++formatoff;
        break;
    case 'L':
        state.length = State::Length::L;
        ++formatoff;
        break;
    }
    return print_execute(state, writer, format, formatoff, std::forward<Args>(args)...);
}

template<typename Writer, typename ...Args>
int print_get_precision(State& state, Writer& writer, const char* format, size_t formatoff, Args&& ...args)
{
    if (format[formatoff] != '.')
        return print_get_length(state, writer, format, formatoff, std::forward<Args>(args)...);
    ++formatoff;
    int mul = 1;
    for (;; ++formatoff) {
        if (format[formatoff] >= '0' && format[formatoff] <= '9') {
            state.precision *= mul;
            state.precision += format[formatoff] - '0';
            mul *= 10;
        } else if (format[formatoff] == '*') {
            state.precision = -1;
            return print_get_length(state, writer, format, formatoff + 1, std::forward<Args>(args)...);
        } else if (format[formatoff] == '\0') {
            return print_error("precision", state, format, formatoff);
        } else {
            return print_get_length(state, writer, format, formatoff, std::forward<Args>(args)...);
        }
    }
}

template<typename Writer, typename ...Args>
int print_get_width(State& state, Writer& writer, const char* format, size_t formatoff, Args&& ...args)
{
    int mul = 1;
    for (;; ++formatoff) {
        if (format[formatoff] >= '0' && format[formatoff] <= '9') {
            state.width *= mul;
            state.width += format[formatoff] - '0';
            mul *= 10;
        } else if (format[formatoff] == '*') {
            state.width = -1;
            return print_get_precision(state, writer, format, formatoff + 1, std::forward<Args>(args)...);
        } else if (format[formatoff] == '\0') {
            return print_error("width", state, format, formatoff);
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
            state.flags |= State::Flags::LeftJustify;
            break;
        case '+':
            state.flags |= State::Flags::Sign;
            break;
        case ' ':
            state.flags |= State::Flags::Space;
            break;
        case '#':
            state.flags |= State::Flags::Prefix;
            break;
        case '0':
            state.flags |= State::Flags::ZeroPad;
            break;
        case '\0':
            return print_error("flags", state, format, formatoff);
        default:
            return print_get_width(state, writer, format, formatoff, std::forward<Args>(args)...);
        }
    }
}

template<typename Writer, typename ...Args>
int print_helper(State& state, Writer& writer, const char* format, size_t formatoff, Args&& ...args)
{
    for (;;) {
        assert(writer.offset() <= writer.size());
        if (writer.offset() == writer.size())
            return writer.size();
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
    BufferWriter writer { buffer, size, 0 };
    return print_helper(state, writer, format, 0, std::forward<Args>(args)...);
}

template<typename ...Args>
int print(const char* format, Args&& ...args)
{
    State state;
    FileWriter writer { stdout, 0 };
    return print_helper(state, writer, format, 0, std::forward<Args>(args)...);
}

int main(int, char**)
{
    // char buf[1024];
    // const int i = snprint(buf, sizeof(buf), "hello %s\n", "ting");
    // printf("%d -> '%s'\n", i, buf);
    //const int i = snprint(buf, sizeof(buf), "hello %s\n", "hello");

    std::string tang = "tang";
    const int i = print("hello %s%s %f\n", "ting", tang, std::numeric_limits<float>::max());
    // printf("%f\n", std::numeric_limits<double>::max());
    printf("%f\n", std::numeric_limits<float>::max());
    return i;
}
