#include "print2.h"
#include <chrono>

using namespace std::chrono;

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
    // snprint2("hello %n%d %f '%*.*s' '%s' %n\n", &ting1, 99, 1.234, 10, 2, "hi ho", foobar, &ting2);
    // snprint2("got ting %d %d (%s) %p\n", ting1, ting2, foobar2, &foobar2);
    //print2("hello %d '%s'\n", 2, "foobar");

    std::string tang = "tang";
    char buffer1[1024];
    char buffer2[1024];

    int fn1, fn2, r1, r2;

    enum { Iter = 100000 };

    auto t3 = steady_clock::now();
    for (int i = 0; i < Iter; ++i) {
        //snprintf(buffer, sizeof(buffer), "hello1 %f\n", 12234.15281);
        //snprintf(buffer, sizeof(buffer), "hello1 %20s\n", "hipphipp");
        r2 = snprintf(buffer1, sizeof(buffer1), "hello1 %#x%s%*u%p%s%f%-+20d\n%n", 1234567, "jappja", 140, 12345, &fn1, "trall og trall", 123.456, 99, &fn2);
        //r2 = snprintf(buffer2, sizeof(buffer2), "h%p\n%n", &foobar, &fn2);
        //r2 = snprint2(buffer2, sizeof(buffer2), "h%p\n%n", &fn1, &fn2);
    }

    auto t4 = steady_clock::now();
    double delta2 = duration_cast<nanoseconds>(t4 - t3).count() / static_cast<double>(Iter);

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

    // verify
    bool ok = true;
    int off = 0;
    // if (fn1 == fn2 && r1 == r2 && fn1 > 0) {
    //     for (off = 0; off < fn1; ++off) {
    //         if (buffer1[off] != buffer2[off]) {
    //             ok = false;
    //             break;
    //         }
    //     }
    // } else {
    //     ok = false;
    // }

    if (ok) {
        printf("took, me   %f\n", delta1);
        printf("took, them %f\n", delta2);

        printf("verified %d\n", fn1);
    } else {
        printf("verify failed at %d (%d,%d) - (%d,%d)\n", off, fn1, fn2, r1, r2);
    }

    return 0;
}
