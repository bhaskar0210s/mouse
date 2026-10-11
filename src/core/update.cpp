#include "update.hpp"

#include <cctype>
#include <cstdlib>
#include <map>
#include <memory>
#include <vector>

namespace mouse {
namespace {

// A small JSON reader: just enough structure to read GitHub's release object safely.
struct Json {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> items;
    std::map<std::string, Json> fields;

    const Json* get(const std::string& k) const {
        auto it = fields.find(k);
        return it == fields.end() ? nullptr : &it->second;
    }
    std::string str(const std::string& k) const {
        const Json* v = get(k);
        return v && v->kind == String ? v->s : "";
    }
};

class Reader {
public:
    explicit Reader(const std::string& t) : t_(t) {}
    bool parse(Json& out) {
        if (!value(out, 0)) return false;
        ws();
        return i_ == t_.size();
    }

private:
    void ws() {
        while (i_ < t_.size() && std::isspace(static_cast<unsigned char>(t_[i_]))) i_++;
    }
    bool lit(const char* w) {
        size_t n = std::char_traits<char>::length(w);
        if (t_.compare(i_, n, w) != 0) return false;
        i_ += n;
        return true;
    }
    bool value(Json& v, int depth) {
        if (depth > 64) return false;
        ws();
        if (i_ >= t_.size()) return false;
        char c = t_[i_];
        if (c == '{') return object(v, depth);
        if (c == '[') return array(v, depth);
        if (c == '"') {
            v.kind = Json::String;
            return string(v.s);
        }
        if (lit("true")) {
            v.kind = Json::Bool;
            v.b = true;
            return true;
        }
        if (lit("false")) {
            v.kind = Json::Bool;
            return true;
        }
        if (lit("null")) return true;
        return number(v);
    }
    bool number(Json& v) {
        size_t start = i_;
        if (i_ < t_.size() && (t_[i_] == '-' || t_[i_] == '+')) i_++;
        while (i_ < t_.size() && (std::isdigit(static_cast<unsigned char>(t_[i_])) || t_[i_] == '.' || t_[i_] == 'e' ||
                                  t_[i_] == 'E' || t_[i_] == '-' || t_[i_] == '+'))
            i_++;
        if (i_ == start) return false;
        v.kind = Json::Number;
        v.n = std::strtod(t_.substr(start, i_ - start).c_str(), nullptr);
        return true;
    }
    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) {
            o += static_cast<char>(cp);
        } else if (cp < 0x800) {
            o += static_cast<char>(0xC0 | (cp >> 6));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            o += static_cast<char>(0xE0 | (cp >> 12));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool string(std::string& o) {
        if (t_[i_] != '"') return false;
        i_++;
        while (i_ < t_.size()) {
            char c = t_[i_++];
            if (c == '"') return true;
            if (c != '\\') {
                o += c;
                continue;
            }
            if (i_ >= t_.size()) return false;
            char e = t_[i_++];
            switch (e) {
                case '"': o += '"'; break;
                case '\\': o += '\\'; break;
                case '/': o += '/'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'n': o += '\n'; break;
                case 'r': o += '\r'; break;
                case 't': o += '\t'; break;
                case 'u': {
                    if (i_ + 4 > t_.size()) return false;
                    unsigned cp = static_cast<unsigned>(std::strtoul(t_.substr(i_, 4).c_str(), nullptr, 16));
                    i_ += 4;
                    utf8(o, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool array(Json& v, int depth) {
        v.kind = Json::Array;
        i_++;
        ws();
        if (i_ < t_.size() && t_[i_] == ']') {
            i_++;
            return true;
        }
        for (;;) {
            Json item;
            if (!value(item, depth + 1)) return false;
            v.items.push_back(std::move(item));
            ws();
            if (i_ < t_.size() && t_[i_] == ',') {
                i_++;
                continue;
            }
            if (i_ < t_.size() && t_[i_] == ']') {
                i_++;
                return true;
            }
            return false;
        }
    }
    bool object(Json& v, int depth) {
        v.kind = Json::Object;
        i_++;
        ws();
        if (i_ < t_.size() && t_[i_] == '}') {
            i_++;
            return true;
        }
        for (;;) {
            ws();
            std::string key;
            if (i_ >= t_.size() || !string(key)) return false;
            ws();
            if (i_ >= t_.size() || t_[i_] != ':') return false;
            i_++;
            Json val;
            if (!value(val, depth + 1)) return false;
            v.fields[key] = std::move(val);
            ws();
            if (i_ < t_.size() && t_[i_] == ',') {
                i_++;
                continue;
            }
            if (i_ < t_.size() && t_[i_] == '}') {
                i_++;
                return true;
            }
            return false;
        }
    }
    const std::string& t_;
    size_t i_ = 0;
};

bool isHex(const std::string& s) {
    for (char c : s)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return !s.empty();
}

}  // namespace

bool parseRelease(const std::string& json, Release& out) {
    if (json.size() > (4 << 20)) return false;
    Json root;
    if (!Reader(json).parse(root) || root.kind != Json::Object) return false;
    if (const Json* d = root.get("draft"); d && d->kind == Json::Bool && d->b) return false;
    if (const Json* p = root.get("prerelease"); p && p->kind == Json::Bool && p->b) return false;
    std::string tag = root.str("tag_name");
    if (tag.size() > 1 && (tag[0] == 'v' || tag[0] == 'V')) tag = tag.substr(1);
    if (tag.empty() || !std::isdigit(static_cast<unsigned char>(tag[0]))) return false;
    const Json* assets = root.get("assets");
    if (!assets || assets->kind != Json::Array) return false;
    for (const Json& a : assets->items) {
        if (a.kind != Json::Object || a.str("name") != "Mouse.zip") continue;
        std::string url = a.str("browser_download_url");
        if (url.rfind("https://github.com/", 0) != 0) return false;  // only ever download from GitHub
        out.version = tag;
        out.zipUrl = url;
        out.sha256.clear();
        std::string digest = a.str("digest");
        if (digest.rfind("sha256:", 0) == 0 && digest.size() == 7 + 64 && isHex(digest.substr(7))) {
            out.sha256 = digest.substr(7);
            for (auto& ch : out.sha256) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return true;
    }
    return false;
}

int compareVersions(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        long x = 0, y = 0;
        while (i < a.size() && a[i] != '.') {
            x = std::isdigit(static_cast<unsigned char>(a[i])) && x < 100000000 ? x * 10 + (a[i] - '0') : x;
            i++;
        }
        while (j < b.size() && b[j] != '.') {
            y = std::isdigit(static_cast<unsigned char>(b[j])) && y < 100000000 ? y * 10 + (b[j] - '0') : y;
            j++;
        }
        if (x != y) return x < y ? -1 : 1;
        if (i < a.size()) i++;
        if (j < b.size()) j++;
    }
    return 0;
}

std::string releaseFeed(const std::string& repo) { return "https://api.github.com/repos/" + repo + "/releases/latest"; }

}  // namespace mouse
