// Release metadata for automatic updates (pure logic, unit tested).
#pragma once

#include <string>

namespace mouse {

struct Release {
    std::string version;  // "1.2.0" (tag without the leading v)
    std::string zipUrl;   // download URL of the Mouse.zip asset
    std::string sha256;   // lowercase hex digest published by GitHub, may be empty
};

// Parses GitHub's "latest release" JSON. False if it is not a usable, published release.
bool parseRelease(const std::string& json, Release& out);

// Compares dotted versions numerically: <0 if a<b, 0 if equal, >0 if a>b. Non-numeric parts count as 0.
int compareVersions(const std::string& a, const std::string& b);

// The address of the release feed for a "owner/repo" slug.
std::string releaseFeed(const std::string& repo);

}  // namespace mouse
