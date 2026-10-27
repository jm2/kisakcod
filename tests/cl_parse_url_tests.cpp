// cl_parse_url_tests.cpp: CL_SanitizeDownloadUrl masks the userinfo of every
// embedded authority while leaving the rest of the URL intact (#206 C2 item 5).
#include <client_mp/client_mp.h>

#include <cstdio>
#include <cstring>

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

void CheckSanitize(const char *source, const char *expected,
    const std::size_t capacity = 1024)
{
    char out[1024];
    std::memset(out, 0x5A, sizeof(out));
    CL_SanitizeDownloadUrl(source, out, capacity);

    const bool terminated = std::memchr(out, '\0', capacity) != nullptr;
    CHECK(terminated);
    if (terminated)
    {
        const bool matches = std::strcmp(out, expected) == 0;
        CHECK(matches);
        if (!matches)
            std::fprintf(stderr, "  in:   %s\n  out:  %s\n  want: %s\n",
                source, out, expected);
    }
    for (std::size_t index = capacity; index < sizeof(out); ++index)
        CHECK(out[index] == static_cast<char>(0x5A));
}
} // namespace

int main()
{
    CheckSanitize("plain", "plain");
    CheckSanitize("http://host/path", "http://host/path");
    CheckSanitize("http://user:pass@host/path", "http://*:*host/path");
    CheckSanitize("http://user@host/path", "http://*:*host/path");
    CheckSanitize("http://user:pass@host", "http://*:*host");
    CheckSanitize("http://user:pass@host/path?next=https://bob:pw@other/x",
        "http://*:*host/path?next=https://*:*other/x");
    CheckSanitize("http://host/a?u=ftp://u:p@h2/b#frag",
        "http://host/a?u=ftp://*:*h2/b#frag");
    CheckSanitize("http://u1:p1@h1/a?u=http://u2:p2@h2/b&v=https://u3:p3@h3/c",
        "http://*:*h1/a?u=http://*:*h2/b&v=https://*:*h3/c");
    CheckSanitize("http://first:pw@host/a@b", "http://*:*host/a@b");
    CheckSanitize("http://host/a?x=http://user@h2", "http://host/a?x=http://*:*h2");
    CheckSanitize("http://u:p@h1/a?x=http://h2/b", "http://*:*h1/a?x=http://h2/b");
    CheckSanitize("http://h1/a?x=http://u:p@h2/b&y=https://h3/c",
        "http://h1/a?x=http://*:*h2/b&y=https://h3/c");
    CheckSanitize("http://host://user:pass@other/x", "http://host://*:*other/x");

    char bounded[16];
    std::memset(bounded, 0x5A, sizeof(bounded));
    CL_SanitizeDownloadUrl("http://user:pass@host/path?next=https://u:p@h2/x",
        bounded, 12);
    CHECK(std::memchr(bounded, '\0', 12) != nullptr);
    CHECK(bounded[11] == '\0');
    for (std::size_t index = 12; index < sizeof(bounded); ++index)
        CHECK(bounded[index] == static_cast<char>(0x5A));

    if (g_failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("cl_parse URL masking: ok");
    return 0;
}
