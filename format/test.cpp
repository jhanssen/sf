#include <stdio.h>
#include <string_view>
#include <type_traits>
#include <utility>
#include <string>

#define ifc if constexpr
#define elifc else if constexpr

template<typename T> struct dependent_false : std::false_type
{
};

template<size_t Idx, typename String, typename ...Args>
constexpr void parsePercentFlags(String string, Args&& ...args);

template<size_t Idx, typename String, typename ...Args>
constexpr void parseChar(String string, Args&& ...args)
{
    constexpr std::string_view text = string();
    ifc (text[Idx] == '%') {
        parsePercentFlags<Idx + 1>(string, std::forward<Args>(args)...);
    } elifc (Idx < text.size()) {
        parseChar<Idx + 1>(string, std::forward<Args>(args)...);
    } elifc (sizeof...(args) > 0) {
        static_assert(dependent_false<String>::value, "Extraneous arguments passed");
    }
    static_assert(text[Idx] != 'f', "can't have f");
}

template<size_t Idx, typename FormatType, typename String, typename Arg, typename ...Args>
constexpr void parseInt(String string, Arg i, Args&& ...args)
{
    using ArgType = typename std::remove_cv<typename std::remove_reference<Arg>::type>::type;
    static_assert(std::is_integral<ArgType>::value, "Argument is not integral");
    static_assert(sizeof(ArgType) <= sizeof(FormatType), "Wrong int type");
    parseChar<Idx>(string, std::forward<Args>(args)...);
}

template<size_t Idx, typename FormatType, typename String, typename Arg, typename ...Args>
constexpr void parseExact(String string, Arg i, Args&& ...args)
{
    using ArgType = typename std::remove_cv<typename std::remove_reference<Arg>::type>::type;
    static_assert(std::is_same<ArgType, FormatType>::value, "Invalid argument type");
    parseChar<Idx>(string, std::forward<Args>(args)...);
}

template<size_t Idx, typename String, typename Arg, typename ...Args>
constexpr void parseFloat(String string, Arg f, Args&& ...args)
{
    using ArgType = typename std::remove_cv<typename std::remove_reference<Arg>::type>::type;
    static_assert(std::is_arithmetic<ArgType>::value, "Argument is not arithmetic");
    parseChar<Idx>(string, std::forward<Args>(args)...);
}

template<size_t Idx, typename String, typename Arg, typename ...Args>
constexpr void parsePointer(String string, Arg f, Args&& ...args)
{
    static_assert(std::is_pointer<Arg>::value, "Argument is not a pointer");
    parseChar<Idx>(string, std::forward<Args>(args)...);
}

template<class T>
struct is_c_string : std::integral_constant<
    bool,
    std::is_same<const char*, typename std::decay<T>::type>::value ||
    std::is_same<char*, typename std::decay<T>::type>::value>
{
};

template<class T>
struct is_string : std::integral_constant<
    bool,
    is_c_string<T>::value ||
    std::is_same<std::string, typename std::decay<T>::type>::value>
{
};

struct Foobar1
{
};

static std::string to_string(const Foobar1&)
{
}

struct Foobar2
{
};

static std::string to_string(const Foobar2&)
{
}
struct FoobarNo
{
};

template<typename, typename = void>
struct has_global_to_string : std::false_type
{
};

template<typename T>
struct has_global_to_string<T, std::void_t<decltype(to_string(std::declval<T>()))> > : std::true_type
{
};

template<size_t Idx, typename String, typename Arg, typename ...Args>
constexpr void parseString(String string, Arg s, Args&& ...args)
{
    static_assert(is_string<Arg>::value || has_global_to_string<Arg>::value, "Needs to be a string or have a global to_string");
    parseChar<Idx>(string, std::forward<Args>(args)...);
}

enum class LengthType
{
    None, hh, h, ll, l, j, z, t, L
};

template<typename T, LengthType Length>
struct TypeType;

template<>
struct TypeType<int, LengthType::None>
{
    typedef int type;
};

template<>
struct TypeType<int, LengthType::hh>
{
    typedef signed char type;
};

template<>
struct TypeType<int, LengthType::h>
{
    typedef short int type;
};

template<>
struct TypeType<int, LengthType::ll>
{
    typedef long long int type;
};

template<>
struct TypeType<int, LengthType::l>
{
    typedef long int type;
};

template<>
struct TypeType<int, LengthType::j>
{
    typedef intmax_t type;
};

template<>
struct TypeType<int, LengthType::z>
{
    typedef ssize_t type;
};

template<>
struct TypeType<int, LengthType::t>
{
    typedef ptrdiff_t type;
};

template<>
struct TypeType<unsigned int, LengthType::None>
{
    typedef unsigned int type;
};

template<>
struct TypeType<unsigned int, LengthType::hh>
{
    typedef unsigned char type;
};

template<>
struct TypeType<unsigned int, LengthType::h>
{
    typedef unsigned short int type;
};

template<>
struct TypeType<unsigned int, LengthType::ll>
{
    typedef unsigned long long int type;
};

template<>
struct TypeType<unsigned int, LengthType::l>
{
    typedef unsigned long int type;
};

template<>
struct TypeType<unsigned int, LengthType::j>
{
    typedef uintmax_t type;
};

template<>
struct TypeType<unsigned int, LengthType::z>
{
    typedef size_t type;
};

template<>
struct TypeType<unsigned int, LengthType::t>
{
    typedef ptrdiff_t type;
};

template<>
struct TypeType<signed char, LengthType::None>
{
    typedef int type;
};

template<>
struct TypeType<signed char, LengthType::l>
{
    typedef wint_t type;
};

template<size_t Idx, size_t Stars, LengthType Length, typename String, typename ...Args>
constexpr void parsePercentSpecifier(String string, Args&& ...args);

template<size_t Idx, size_t Stars, LengthType Length, typename String, typename Arg, typename ...Args>
constexpr void parsePercentStars(String string, Arg i, Args&& ...args)
{
    using ArgType = typename std::remove_cv<typename std::remove_reference<Arg>::type>::type;
    static_assert(std::is_same<ArgType, int>::value, "Star argument must be int");
    static_assert(Stars > 0 && Stars <= 2, "Invalid star count");
    ifc (Stars > 1) {
        parsePercentStars<Idx, Stars - 1, Length>(string, std::forward<Args>(args)...);
    } else {
        parsePercentSpecifier<Idx, Stars - 1, Length>(string, std::forward<Args>(args)...);
    }
}

template<size_t Idx, size_t Stars, LengthType Length, typename String, typename ...Args>
constexpr void parsePercentSpecifier(String string, Args&& ...args)
{
    ifc (Stars > 0) {
        parsePercentStars<Idx, Stars, Length>(string, std::forward<Args>(args)...);
    } else {
        constexpr std::string_view text = string();
        ifc (text[Idx] == 'd' || text[Idx] == 'i') {
            ifc (Length == LengthType::z) {
                parseExact<Idx + 1, ssize_t>(string, std::forward<Args>(args)...);
            } else {
                parseInt<Idx + 1, typename TypeType<int, Length>::type>(string, std::forward<Args>(args)...);
            }
        } elifc (text[Idx] == 'u' || text[Idx] == 'o' || text[Idx] == 'x' || text[Idx] == 'X') {
            ifc (Length == LengthType::z) {
                parseExact<Idx + 1, size_t>(string, std::forward<Args>(args)...);
            } else {
                parseInt<Idx + 1, typename TypeType<unsigned int, Length>::type>(string, std::forward<Args>(args)...);
            }
        } elifc (text[Idx] == 's') {
            ifc (Length == LengthType::None) {
                parseString<Idx + 1>(string, std::forward<Args>(args)...);
            } else {
                static_assert(dependent_false<String>::value, "Invalid string length");
            }
        } elifc (text[Idx] == 'p') {
            ifc (Length == LengthType::None) {
                parsePointer<Idx + 1>(string, std::forward<Args>(args)...);
            } else {
                static_assert(dependent_false<String>::value, "Invalid pointer length");
            }
        } elifc (text[Idx] == 'c') {
            parseInt<Idx + 1, typename TypeType<signed char, Length>::type>(string, std::forward<Args>(args)...);
        } elifc (text[Idx] == 'n') {
            parseExact<Idx + 1, typename std::add_pointer<typename TypeType<int, Length>::type>::type>(string, std::forward<Args>(args)...);
        } elifc (text[Idx] == 'f' || text[Idx] == 'F' || text[Idx] == 'e' || text[Idx] == 'E' || text[Idx] == 'g' || text[Idx] == 'G' || text[Idx] == 'a' || text[Idx] == 'A') {
            ifc (Length == LengthType::None || Length == LengthType::L) {
                parseFloat<Idx + 1>(string, std::forward<Args>(args)...);
            } else {
                static_assert(dependent_false<String>::value, "Invalid double length");
            }
        } else {
            static_assert(dependent_false<String>::value, "Invalid format specifier");
        }
    }
}

template<size_t Idx, size_t Stars, typename String, typename ...Args>
constexpr void parsePercentLength(String string, Args&& ...args)
{
    constexpr std::string_view text = string();
    ifc (text[Idx] == 'h' && text[Idx + 1] == 'h') {
        parsePercentSpecifier<Idx + 2, Stars, LengthType::hh>(string, std::forward<Args>(args)...);
    } elifc (text[Idx] == 'h') {
        parsePercentSpecifier<Idx + 1, Stars, LengthType::h>(string, std::forward<Args>(args)...);
    } elifc (text[Idx] == 'l' && text[Idx + 1] == 'l') {
        parsePercentSpecifier<Idx + 2, Stars, LengthType::ll>(string, std::forward<Args>(args)...);
    } elifc (text[Idx] == 'l') {
        parsePercentSpecifier<Idx + 1, Stars, LengthType::l>(string, std::forward<Args>(args)...);
    } elifc (text[Idx] == 'j') {
        parsePercentSpecifier<Idx + 1, Stars, LengthType::j>(string, std::forward<Args>(args)...);
    } elifc (text[Idx] == 'z') {
        parsePercentSpecifier<Idx + 1, Stars, LengthType::z>(string, std::forward<Args>(args)...);
    } elifc (text[Idx] == 't') {
        parsePercentSpecifier<Idx + 1, Stars, LengthType::t>(string, std::forward<Args>(args)...);
    } elifc (text[Idx] == 'L') {
        parsePercentSpecifier<Idx + 1, Stars, LengthType::L>(string, std::forward<Args>(args)...);
    } else {
        parsePercentSpecifier<Idx, Stars, LengthType::None>(string, std::forward<Args>(args)...);
    }
}

template<size_t Idx, size_t Stars, typename String, typename ...Args>
constexpr void parsePercentPrecision(String string, Args&& ...args)
{
    constexpr std::string_view text = string();
    ifc (text[Idx] == '*') {
        parsePercentLength<Idx + 1, Stars + 1>(string, std::forward<Args>(args)...);
    } elifc (text[Idx] >= '0' && text[Idx] <= '9') {
        parsePercentPrecision<Idx + 1, Stars>(string, std::forward<Args>(args)...);
    } else {
        parsePercentLength<Idx + 1, Stars>(string, std::forward<Args>(args)...);
    }
}

template<size_t Idx, size_t Stars, typename String, typename ...Args>
constexpr void parsePercentWidth(String string, Args&& ...args)
{
    constexpr std::string_view text = string();
    ifc (text[Idx] >= '0' && text[Idx] <= '9') {
        parsePercentWidth<Idx + 1, Stars>(string, std::forward<Args>(args)...);
    } elifc (text[Idx] == '*') {
        ifc (Stars == 0) {
            parsePercentWidth<Idx + 1, Stars + 1>(string, std::forward<Args>(args)...);
        } else {
            static_assert(dependent_false<String>::value, "Too many stars in width");
        }
    } elifc (text[Idx] == '.') {
        parsePercentPrecision<Idx + 1, Stars>(string, std::forward<Args>(args)...);
    } else {
        parsePercentLength<Idx, Stars>(string, std::forward<Args>(args)...);
    }
}

template<size_t Idx, typename String, typename ...Args>
constexpr void parsePercentFlags(String string, Args&& ...args)
{
    constexpr std::string_view text = string();
    ifc (text[Idx] == '-' || text[Idx] == '+' || text[Idx] == ' ' || text[Idx] == '#' || text[Idx] == '0') {
        parsePercentFlags<Idx + 1>(string, std::forward<Args>(args)...);
    } else {
        parsePercentWidth<Idx, 0>(string, std::forward<Args>(args)...);
    }
}

template<typename String, typename ...Args>
constexpr void parse(String string, Args&& ...args)
{
    constexpr std::string_view text = string();
    static_assert(text[text.size()] == '\0');
    parseChar<0>(string, std::forward<Args>(args)...);
}

#define NERROR(str, ...)                        \
    parse([]() { return str; }, ##__VA_ARGS__);

int main(int, char**)
{
    //auto foo = []() { return "hoo %s bafr\n"; };
    uint32_t ball = 1;
    uint64_t ball64 = 2;
    signed char i;
    //NERROR("ghod %llu%s%u\n", ball, "ting", 1u);
    Foobar2 f;
    std::string s;
    size_t sz;
    //NERROR("ghod %llu%s%u %s %u %zu %20s\n", ball, f, 1u, std::move(s), 1, sz, f);
    //NERROR("hey %*.*s %f %n\n", 1, 1, "foo", 1, ball64);
    NERROR("hey %*.*s %hhn%p\n", 1, 1, f, &i, &i);
    //parse(foo);
}
