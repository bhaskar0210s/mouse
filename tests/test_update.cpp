// Release feed parsing and version ordering for automatic updates.
#include "core/update.hpp"
#include "test.hpp"

using namespace mouse;

namespace {
const char* kFeed = R"({
  "url": "https://api.github.com/repos/o/r/releases/1",
  "tag_name": "v1.2.10",
  "draft": false,
  "prerelease": false,
  "name": "Mouse 1.2.10 — \"quotes\" \\ slash",
  "assets": [
    {"name": "Other.zip", "browser_download_url": "https://github.com/o/r/releases/download/v1.2.10/Other.zip"},
    {"name": "Mouse.zip", "size": 512000, "digest": "sha256:ABCDEF0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789",
     "browser_download_url": "https://github.com/o/r/releases/download/v1.2.10/Mouse.zip"}
  ],
  "body": "notes"
})";
}

TEST(release_feed_parses) {
    Release r;
    CHECK(parseRelease(kFeed, r));
    CHECK_EQ(r.version, std::string("1.2.10"));
    CHECK_EQ(r.zipUrl, std::string("https://github.com/o/r/releases/download/v1.2.10/Mouse.zip"));
    CHECK_EQ(r.sha256, std::string("abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"));
    CHECK_EQ(releaseFeed("o/r"), std::string("https://api.github.com/repos/o/r/releases/latest"));
}

TEST(release_feed_rejects_bad_input) {
    Release r;
    std::string feed = kFeed;
    CHECK(!parseRelease("", r));
    CHECK(!parseRelease("{", r));
    CHECK(!parseRelease("[1,2,3]", r));
    CHECK(!parseRelease(R"({"tag_name":"v1.0.0","assets":[]})", r));                       // no Mouse.zip
    CHECK(!parseRelease(R"({"tag_name":"v1.0.0","draft":true,"assets":[]})", r));
    CHECK(!parseRelease(R"({"tag_name":"nightly","assets":[{"name":"Mouse.zip","browser_download_url":"https://github.com/x"}]})", r));
    // never download from anywhere but GitHub
    CHECK(!parseRelease(R"({"tag_name":"v9.0.0","assets":[{"name":"Mouse.zip","browser_download_url":"https://evil.example/Mouse.zip"}]})", r));
    CHECK(!parseRelease(R"({"tag_name":"v9.0.0","assets":[{"name":"Mouse.zip","browser_download_url":"http://github.com/x"}]})", r));
    // prerelease ignored
    std::string pre = feed;
    pre.replace(pre.find("\"prerelease\": false"), 19, "\"prerelease\": true ");
    CHECK(!parseRelease(pre, r));
    // a malformed digest is ignored, not trusted
    std::string bad = feed;
    bad.replace(bad.find("sha256:ABCDEF"), 13, "sha256:XYZ");
    CHECK(parseRelease(bad, r));
    CHECK(r.sha256.empty());
    // truncated documents and deep nesting fail cleanly
    for (size_t n = 0; n < feed.size(); n += 7) CHECK(!parseRelease(feed.substr(0, n), r));
    CHECK(!parseRelease(std::string(5000, '[') + std::string(5000, ']'), r));
}

TEST(version_ordering) {
    CHECK(compareVersions("1.2.10", "1.2.9") > 0);
    CHECK(compareVersions("1.2.9", "1.2.10") < 0);
    CHECK(compareVersions("1.0", "1.0.0") == 0);
    CHECK(compareVersions("2.0.0", "1.99.99") > 0);
    CHECK(compareVersions("1.0.0", "1.0.0") == 0);
    CHECK(compareVersions("", "0.0.1") < 0);
    CHECK(compareVersions("1.0.0-beta", "1.0.0") == 0);
    CHECK(compareVersions("99999999999.0", "1.0") > 0);  // no overflow
}
