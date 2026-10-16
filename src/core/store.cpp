#include "store.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <notify.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

namespace mouse {
namespace {

std::string homeDir() {
    if (const char* h = std::getenv("HOME"); h && *h) return h;
    if (const passwd* pw = getpwuid(getuid()); pw && pw->pw_dir) return pw->pw_dir;
    return "/tmp";
}

void makeDirs(const std::string& path) {
    std::string cur;
    for (size_t i = 0; i < path.size(); i++) {
        cur += path[i];
        if ((path[i] == '/' && i > 0) || i + 1 == path.size()) mkdir(cur.c_str(), 0755);
    }
}

std::string field(const std::string& text, const std::string& key) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() : nl + 1;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        while (!k.empty() && k.back() == ' ') k.pop_back();
        while (!v.empty() && v.front() == ' ') v.erase(v.begin());
        if (k == key) return v;
    }
    return "";
}

int num(const std::string& v, int fallback) {
    if (v.empty()) return fallback;
    char* end = nullptr;
    long n = std::strtol(v.c_str(), &end, 10);
    return (end && *end == '\0') ? static_cast<int>(n) : fallback;
}

}  // namespace

std::string dataDir() {
    if (const char* d = std::getenv("MOUSE_DIR"); d && *d) return d;
    return homeDir() + "/Library/Application Support/Mouse";
}

std::string configPath() { return dataDir() + "/config"; }
std::string statePath() { return dataDir() + "/state"; }

bool readFile(const std::string& path, std::string& out) {
    out.clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[4096];
    for (;;) {
        size_t n = std::fread(buf, 1, sizeof buf, f);
        out.append(buf, n);
        // A short read is the end of the file (or an error); settings files are tiny, so stop at 1 MB.
        if (n < sizeof buf || out.size() > (1 << 20)) break;
    }
    std::fclose(f);
    return true;
}

bool writeFileAtomic(const std::string& path, const std::string& data) {
    size_t slash = path.rfind('/');
    if (slash != std::string::npos) makeDirs(path.substr(0, slash));
    std::string tmp = path + ".tmp." + std::to_string(getpid());
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    const char* p = data.data();
    size_t left = data.size();
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR) continue;
            close(fd);
            unlink(tmp.c_str());
            return false;
        }
        p += w;
        left -= static_cast<size_t>(w);
    }
    if (close(fd) != 0 || rename(tmp.c_str(), path.c_str()) != 0) {
        unlink(tmp.c_str());
        return false;
    }
    return true;
}

Config loadConfig() {
    std::string text;
    if (!readFile(configPath(), text)) return Config{};
    return parseConfig(text);
}

bool saveConfig(const Config& c) {
    bool ok = writeFileAtomic(configPath(), serializeConfig(c));
    postNotification(kConfigChanged);
    return ok;
}

State parseState(const std::string& t) {
    State s;
    s.running = field(t, "running") == "1";
    s.connected = field(t, "connected") == "1";
    s.name = field(t, "name");
    s.battery = num(field(t, "battery"), -1);
    s.charging = field(t, "charging") == "1";
    s.dpiMin = num(field(t, "dpi.min"), s.dpiMin);
    s.dpiMax = num(field(t, "dpi.max"), s.dpiMax);
    s.dpiStep = num(field(t, "dpi.step"), s.dpiStep);
    s.trusted = field(t, "trusted") == "1";
    s.inputAccess = field(t, "input") == "1";
    s.version = field(t, "version");
    s.update = field(t, "update");
    return s;
}

std::string serializeState(const State& s) {
    std::string o;
    auto line = [&o](const char* k, const std::string& v) { o += std::string(k) + " = " + v + "\n"; };
    line("running", s.running ? "1" : "0");
    line("connected", s.connected ? "1" : "0");
    line("name", s.name);
    line("battery", std::to_string(s.battery));
    line("charging", s.charging ? "1" : "0");
    line("dpi.min", std::to_string(s.dpiMin));
    line("dpi.max", std::to_string(s.dpiMax));
    line("dpi.step", std::to_string(s.dpiStep));
    line("trusted", s.trusted ? "1" : "0");
    line("input", s.inputAccess ? "1" : "0");
    line("version", s.version);
    line("update", s.update);
    return o;
}

State loadState() {
    std::string text;
    if (!readFile(statePath(), text)) return State{};
    return parseState(text);
}

bool saveState(const State& s) {
    bool ok = writeFileAtomic(statePath(), serializeState(s));
    postNotification(kStateChanged);
    return ok;
}

void postNotification(const char* name) { notify_post(name); }

}  // namespace mouse
