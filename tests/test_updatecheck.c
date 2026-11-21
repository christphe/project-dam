/* Update check: version parsing, tag/asset/body extraction (port/include/updatecheck_parse.h). */
#include "ge_test.h"
#include "updatecheck_parse.h"

static void versions(void)
{
    static const struct { const char *tag, *running; int newer; } t[] = {
        { "project-dam-v1.2.0", "1.1.9",   1 },
        { "project-dam-v1.2.0", "1.2.0",   0 },
        { "project-dam-v1.2.0", "1.10.0",  0 },
        { "project-dam-v1.10.0", "1.9.9",  1 },   /* numeric, not lexical */
        { "v2",                "1.9.9",    1 },   /* missing parts are 0 */
        { "V0.7.1-rc1",        "0.7.0",    1 },   /* suffix ignored */
        { "project-dam-v1.2.0", "dev-abc", 0 },   /* unparsable running version: never newer */
        { "nightly",           "1.0.0",    0 },   /* unparsable tag */
        { "",                  "1.0.0",    0 },
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
        GE_CHECK_EQ_INT(versionNewer(t[i].tag, t[i].running), t[i].newer);

    int v[3];
    GE_CHECK(parseVer("project-dam-v3.4.5", v));
    GE_CHECK(v[0] == 3 && v[1] == 4 && v[2] == 5);
}

static void tags(void)
{
    char out[32];
    static const struct { const char *json; const char *want; } ok[] = {
        { "{\"tag_name\": \"project-dam-v0.9.1\", \"x\": 1}", "project-dam-v0.9.1" },
        { "{\"tag_name\":\"v1\"}", "v1" },
        { "{\"tag_name\" \n :\t \"v2.0\"}", "v2.0" },
    };
    for (unsigned i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        GE_CHECK(extractTag(ok[i].json, out, sizeof out));
        GE_CHECK_STR(out, ok[i].want);
    }
    static const char *bad[] = {
        "{}", "{\"tag_name\": 5}", "{\"tag_name\": \"\"}", "{\"tag_name\": \"v1;rm\"}",
        "{\"tag_name\": \"v1", "{\"tag_name\": \"\x1b[31mv1\"}",
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++)
        GE_CHECK(!extractTag(bad[i], out, sizeof out));
    char small[4];   /* too small for the value: truncated, then no closing quote */
    GE_CHECK(!extractTag("{\"tag_name\": \"v1.2.3\"}", small, sizeof small));
}

static void assets(void)
{
    GE_CHECK(hasPcAsset("{\"assets\":[{\"browser_download_url\":\"https://x/y/project-dam-linux-x86_64.tar.gz\"}]}"));
    GE_CHECK(hasPcAsset("{\"browser_download_url\": \"https://x/y/project-dam-windows.zip\"}"));
    GE_CHECK(!hasPcAsset("{\"browser_download_url\":\"https://x/y/project-dam.nro\"}"));
    GE_CHECK(!hasPcAsset("{\"browser_download_url\":\"https://x/linux-dir/project-dam.nro\"}"));   /* the directory does not count */
    GE_CHECK(!hasPcAsset("{}"));

    char url[256], sha[65];
    static const char sha64[] = "0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789abcdef";
    char body[1024];
    snprintf(body, sizeof body,
        "{\"assets\":[{\"name\":\"other.zip\",\"digest\":\"sha256:%s\",\"browser_download_url\":\"https://x/other.zip\"},"
        "{\"name\":\"project-dam.nro\",\"digest\":\"sha256:%s\",\"browser_download_url\":\"https://x/project-dam.nro\"}]}",
        sha64, sha64);
    GE_CHECK(findAsset(body, "project-dam.nro", url, sizeof url, sha, sizeof sha));
    GE_CHECK_STR(url, "https://x/project-dam.nro");
    GE_CHECK_STR(sha, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");   /* lower-cased */
    GE_CHECK(!findAsset(body, "missing.nro", url, sizeof url, sha, sizeof sha));
    GE_CHECK(!findAsset(body, "project-dam.nro", url, sizeof url, sha, 64));   /* sha buffer too small */
    GE_CHECK(!findAsset("{\"name\":\"project-dam.nro\",\"digest\":\"sha1:abc\",\"browser_download_url\":\"u\"}",
                        "project-dam.nro", url, sizeof url, sha, sizeof sha));
}

static void notes(void)
{
    char out[128];
    GE_CHECK(extractBody("{\"body\": \"Hello\\nWorld \\\"q\\\"\\tT\\r\\n\\u2764 end\"}", out, sizeof out));
    GE_CHECK_STR(out, "Hello\nWorld \"q\" T\n end");
    GE_CHECK(!extractBody("{\"body\": \"\"}", out, sizeof out));
    GE_CHECK(!extractBody("{}", out, sizeof out));
    GE_CHECK(extractBody("{\"body\": \"abcdefgh\"}", out, 5));   /* truncated to fit */
    GE_CHECK_STR(out, "abcd");
}

int main(void)
{
    GE_TEST_BEGIN("update check parsing");
    versions();
    tags();
    assets();
    notes();
    return GE_TEST_END();
}
