#include <stdio.h>
#include <string_view>
#include <type_traits>
#include <utility>
#include <string>

#define ifc if constexpr
#define elifc else if constexpr

template<size_t Idx, typename String, typename ...Args>
constexpr void parsePercent(String string, Args&& ...args);

template<size_t Idx, typename String, typename ...Args>
constexpr void parseChar(String string, Args&& ...args)
{
    constexpr std::string_view text = string();
    static_assert(text[text.size()] == '\0');

    ifc (text[Idx] == '%') {
        parsePercent<Idx + 1>(string, std::forward<Args>(args)...);
    } elifc (Idx < text.size()) {
        parseChar<Idx + 1>(string, std::forward<Args>(args)...);
    }
    static_assert(text[Idx] != 'f', "can't have f");
}

template<size_t Idx, size_t Skip, typename FormatType, typename String, typename Arg, typename ...Args>
constexpr void parseInt(String string, Arg i, Args&& ...args)
{
    using ArgType = typename std::decay<Arg>::type;
    static_assert(std::is_integral<ArgType>::value, "Argument is not integral");
    static_assert((sizeof(ArgType) == sizeof(FormatType) && std::is_unsigned<ArgType>::value == std::is_unsigned<FormatType>::value) || sizeof(ArgType) < sizeof(FormatType),
        "Wrong int type");
    parseChar<Idx + Skip>(string, std::forward<Args>(args)...);
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

template<size_t Idx, typename String, typename ...Args>
constexpr void parsePercent(String string, Args&& ...args)
{
    constexpr std::string_view text = string();
    ifc (text[Idx] == 'd') {
        parseInt<Idx + 1, 0, int>(string, std::forward<Args>(args)...);
    } elifc (text[Idx] == 'u') {
        parseInt<Idx + 1, 0, unsigned int>(string, std::forward<Args>(args)...);
    } elifc (text[Idx] == 'l' && text[Idx + 1] == 'd') {
        parseInt<Idx + 1, 1, long>(string, std::forward<Args>(args)...);
    } elifc (text[Idx] == 'l' && text[Idx + 1] == 'u') {
        parseInt<Idx + 1, 1, unsigned long>(string, std::forward<Args>(args)...);
    } elifc (text[Idx] == 'l' && text[Idx + 1] == 'l' && text[Idx + 2] == 'd') {
        parseInt<Idx + 1, 2, long long>(string, std::forward<Args>(args)...);
    } elifc (text[Idx] == 'l' && text[Idx + 1] == 'l' && text[Idx + 2] == 'u') {
        parseInt<Idx + 1, 2, unsigned long long>(string, std::forward<Args>(args)...);
    } elifc (text[Idx] == 's') {
        parseString<Idx + 1>(string, std::forward<Args>(args)...);
    } else {
        parseChar<Idx + 1>(string);
    }
}

template<typename String, typename ...Args>
constexpr void parse(String string, Args&& ...args)
{
    //constexpr std::string_view text = string();
    parseChar<0>(string, std::forward<Args>(args)...);
}

#define NERROR(str, ...)                        \
    parse([]() { return str; }, ##__VA_ARGS__);

int main(int, char**)
{
    //auto foo = []() { return "hoo %s bafr\n"; };
    uint32_t ball = 1;
    //NERROR("ghod %llu%s%u\n", ball, "ting", 1u);
    Foobar2 f;
    std::string s;
    NERROR("ghod %llu%s%u %s\n", ball, f, 1u, std::move(s));
    //parse(foo);
}
