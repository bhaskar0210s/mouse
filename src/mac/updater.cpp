// `Mouse --update`: checks GitHub for a newer release and installs it in place.
//
// Runs as a short-lived child of the agent so the networking code never lives in the agent.
// A download is installed only if it is a newer version of the same app signed with the
// same certificate as the running copy.
#include "updater.hpp"

#include <CommonCrypto/CommonDigest.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <dirent.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/store.hpp"
#include "core/update.hpp"

extern char** environ;

namespace mouse::mac {
namespace {

constexpr const char* kRepo = "bhaskar0210s/mouse";

int run(const std::vector<std::string>& args, std::string* out) {
    int fds[2] = {-1, -1};
    if (out && pipe(fds) != 0) return -1;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    if (out) {
        posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
        posix_spawn_file_actions_addclose(&fa, fds[0]);
        posix_spawn_file_actions_addclose(&fa, fds[1]);
    } else {
        posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    }
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    std::vector<char*> argv;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    pid_t pid = 0;
    int rc = posix_spawn(&pid, argv[0], &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (out) close(fds[1]);
    if (rc != 0) {
        if (out) close(fds[0]);
        return -1;
    }
    if (out) {
        char buf[16384];
        ssize_t n;
        while ((n = read(fds[0], buf, sizeof buf)) > 0)
            if (out->size() < (8u << 20)) out->append(buf, static_cast<size_t>(n));
        close(fds[0]);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::string sha256(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return "";
    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);
    unsigned char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) CC_SHA256_Update(&ctx, buf, static_cast<CC_LONG>(n));
    std::fclose(f);
    unsigned char md[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(md, &ctx);
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (unsigned char c : md) {
        s += hex[c >> 4];
        s += hex[c & 15];
    }
    return s;
}

std::string cfString(CFStringRef s) {
    if (!s) return "";
    char buf[1024] = {};
    CFStringGetCString(s, buf, sizeof buf, kCFStringEncodingUTF8);
    return buf;
}

std::string bundleValue(const std::string& appPath, CFStringRef key) {
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(appPath.c_str()),
                                                           static_cast<CFIndex>(appPath.size()), true);
    if (!url) return "";
    CFBundleRef b = CFBundleCreate(kCFAllocatorDefault, url);
    CFRelease(url);
    if (!b) return "";
    auto v = static_cast<CFStringRef>(CFBundleGetValueForInfoDictionaryKey(b, key));
    std::string s = v && CFGetTypeID(v) == CFStringGetTypeID() ? cfString(v) : "";
    CFRelease(b);
    return s;
}

// The certificate hash in our own designated requirement, empty for ad-hoc (development) builds.
std::string ourCertificateLeaf() {
    SecCodeRef self = nullptr;
    SecStaticCodeRef selfStatic = nullptr;
    SecRequirementRef req = nullptr;
    CFStringRef text = nullptr;
    std::string leaf;
    if (SecCodeCopySelf(kSecCSDefaultFlags, &self) == errSecSuccess &&
        SecCodeCopyStaticCode(self, kSecCSDefaultFlags, &selfStatic) == errSecSuccess &&
        SecCodeCopyDesignatedRequirement(selfStatic, kSecCSDefaultFlags, &req) == errSecSuccess &&
        SecRequirementCopyString(req, kSecCSDefaultFlags, &text) == errSecSuccess) {
        std::string r = cfString(text);
        size_t p = r.find("certificate leaf = H\"");
        if (p != std::string::npos) {
            p += std::strlen("certificate leaf = H\"");
            size_t e = r.find('"', p);
            if (e != std::string::npos && e - p == 40) leaf = r.substr(p, e - p);
        }
    }
    if (text) CFRelease(text);
    if (req) CFRelease(req);
    if (selfStatic) CFRelease(selfStatic);
    if (self) CFRelease(self);
    return leaf;
}

// True if the code at `path` is intact and signed by `leaf` with the given identifier.
bool signedBy(const std::string& path, const std::string& identifier, const std::string& leaf) {
    std::string text = "identifier \"" + identifier + "\" and certificate leaf = H\"" + leaf + "\"";
    CFStringRef reqText = CFStringCreateWithCString(kCFAllocatorDefault, text.c_str(), kCFStringEncodingUTF8);
    SecRequirementRef req = nullptr;
    SecStaticCodeRef code = nullptr;
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(path.c_str()),
                                                           static_cast<CFIndex>(path.size()), true);
    bool ok = reqText && url && SecRequirementCreateWithString(reqText, kSecCSDefaultFlags, &req) == errSecSuccess &&
              SecStaticCodeCreateWithPath(url, kSecCSDefaultFlags, &code) == errSecSuccess &&
              SecStaticCodeCheckValidity(code, kSecCSStrictValidate | kSecCSCheckAllArchitectures | kSecCSCheckNestedCode,
                                         req) == errSecSuccess;
    if (code) CFRelease(code);
    if (req) CFRelease(req);
    if (url) CFRelease(url);
    if (reqText) CFRelease(reqText);
    return ok;
}

// A downloaded Mouse.app is accepted only if both it and its agent carry our certificate.
bool signedLikeUs(const std::string& app) {
    std::string leaf = ourCertificateLeaf();
    if (leaf.empty()) return false;  // development builds never update themselves
    return signedBy(app, "app.mouse.Mouse", leaf) &&
           signedBy(app + "/Contents/Helpers/MouseAgent.app", "app.mouse.agent", leaf);
}

void note(const std::string& text) { writeFileAtomic(dataDir() + "/update-note", text); }

}  // namespace

std::string currentBundlePath() {
    CFBundleRef b = CFBundleGetMainBundle();
    if (!b) return "";
    CFURLRef url = CFBundleCopyBundleURL(b);
    if (!url) return "";
    char buf[PATH_MAX] = {};
    bool ok = CFURLGetFileSystemRepresentation(url, true, reinterpret_cast<UInt8*>(buf), sizeof buf);
    CFRelease(url);
    std::string p = ok ? buf : "";
    // The agent lives in Mouse.app/Contents/Helpers/MouseAgent.app: updates replace the outer app.
    const std::string helper = "/Contents/Helpers/MouseAgent.app";
    if (p.size() > helper.size() && p.compare(p.size() - helper.size(), helper.size(), helper) == 0)
        p.erase(p.size() - helper.size());
    return p.size() > 4 && p.compare(p.size() - 4, 4, ".app") == 0 ? p : "";
}

bool installedForUpdates() {
    std::string p = currentBundlePath();
    if (p.empty()) return false;
    const char* home = getenv("HOME");
    return p.rfind("/Applications/", 0) == 0 || (home && p.rfind(std::string(home) + "/Applications/", 0) == 0);
}

int runUpdate() {
    std::string app = currentBundlePath();
    if (app.empty() || !installedForUpdates()) return 0;
    std::string parent = app.substr(0, app.rfind('/'));
    if (access(parent.c_str(), W_OK) != 0) return 1;

    std::string json;
    if (run({"/usr/bin/curl", "-fsSL", "--proto", "=https", "--proto-redir", "=https", "--max-time", "30", "-H",
             "Accept: application/vnd.github+json", "-H", "User-Agent: Mouse-updater", releaseFeed(kRepo)},
            &json) != 0)
        return 1;
    Release rel;
    if (!parseRelease(json, rel)) return 1;
    if (compareVersions(rel.version, MOUSE_VERSION) <= 0) return 0;

    // Leftovers of an interrupted earlier update (hidden folders next to the app) go first.
    if (DIR* d = opendir(parent.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n.rfind(".Mouse-update-", 0) == 0 || n.rfind(".Mouse-old-", 0) == 0) run({"/bin/rm", "-rf", parent + "/" + n}, nullptr);
        }
        closedir(d);
    }
    std::string stage = parent + "/.Mouse-update-" + std::to_string(getpid());
    run({"/bin/rm", "-rf", stage}, nullptr);
    if (mkdir(stage.c_str(), 0755) != 0) return 1;
    auto fail = [&]() {
        run({"/bin/rm", "-rf", stage}, nullptr);
        return 1;
    };
    std::string zip = stage + "/Mouse.zip";
    if (run({"/usr/bin/curl", "-fsSL", "--proto", "=https", "--proto-redir", "=https", "--max-time", "600", "-o", zip,
             rel.zipUrl},
            nullptr) != 0)
        return fail();
    if (!rel.sha256.empty() && sha256(zip) != rel.sha256) return fail();
    if (run({"/usr/bin/ditto", "-x", "-k", zip, stage + "/x"}, nullptr) != 0) return fail();
    std::string fresh = stage + "/x/Mouse.app";
    if (bundleValue(fresh, CFSTR("CFBundleIdentifier")) != "app.mouse.Mouse") return fail();
    if (compareVersions(bundleValue(fresh, CFSTR("CFBundleShortVersionString")), MOUSE_VERSION) <= 0) return fail();
    if (!signedLikeUs(fresh)) return fail();

    std::string old = parent + "/.Mouse-old-" + std::to_string(getpid()) + ".app";
    if (rename(app.c_str(), old.c_str()) != 0) return fail();
    if (rename(fresh.c_str(), app.c_str()) != 0) {
        rename(old.c_str(), app.c_str());
        return fail();
    }
    run({"/bin/rm", "-rf", old}, nullptr);
    run({"/bin/rm", "-rf", stage}, nullptr);
    note("Updated to " + rel.version);
    return kUpdateInstalled;
}

}  // namespace mouse::mac
