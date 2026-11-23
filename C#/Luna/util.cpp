#include "luna_internal.hpp"
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace luna {

static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

std::string lower(const std::string& s) {
    std::string r(s);
    for (size_t i = 0; i < r.size(); ++i) r[i] = lc(r[i]);
    return r;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && is_ws(s[a])) ++a;
    while (b > a && is_ws(s[b - 1])) --b;
    return s.substr(a, b - a);
}

bool starts_with(const std::string& s, const char* prefix) {
    size_t n = std::strlen(prefix);
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}

size_t find_ci(const std::string& hay, const std::string& needle, size_t from) {
    if (needle.empty()) return from <= hay.size() ? from : std::string::npos;
    if (hay.size() < needle.size()) return std::string::npos;
    for (size_t i = from; i + needle.size() <= hay.size(); ++i) {
        size_t k = 0;
        while (k < needle.size() && lc(hay[i + k]) == lc(needle[k])) ++k;
        if (k == needle.size()) return i;
    }
    return std::string::npos;
}

std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && is_ws(s[i])) ++i;
        size_t st = i;
        while (i < s.size() && !is_ws(s[i])) ++i;
        if (i > st) out.push_back(s.substr(st, i - st));
    }
    return out;
}

std::string escape_html(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); ++i) {
        switch (s[i]) {
            case '&': r += "&amp;"; break;
            case '<': r += "&lt;"; break;
            case '>': r += "&gt;"; break;
            case '"': r += "&quot;"; break;
            default: r += s[i];
        }
    }
    return r;
}

std::string url_decode(const std::string& s, bool plus_as_space) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '%' && i + 2 < s.size() + 1 && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
            out += (char)std::strtol(s.substr(i + 1, 2).c_str(), NULL, 16);
            i += 2;
        } else if (c == '+' && plus_as_space) out += ' ';
        else out += c;
    }
    return out;
}

std::string escape_url(const std::string& u) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(u.size());
    for (size_t i = 0; i < u.size(); ++i) {
        unsigned char c = (unsigned char)u[i];
        if (c == '\t' || c == '\r' || c == '\n') continue;      /* browsers drop these inside URLs */
        if (c <= 0x20 || c >= 0x7F || c == '"' || c == '<' || c == '>' || c == '\\' || c == '^' ||
            c == '`' || c == '{' || c == '|' || c == '}') {
            out += '%'; out += hex[c >> 4]; out += hex[c & 15];
        } else out += (char)c;
    }
    return out;
}

/* ---------------- URL join (urllib.parse.urljoin subset) ---------------- */

struct UrlParts { std::string scheme, authority, path, query; bool valid; };

static UrlParts split_url(const std::string& u) {
    UrlParts p; p.valid = false;
    size_t se = u.find("://");
    if (se == std::string::npos) return p;
    p.scheme = u.substr(0, se);
    size_t as = se + 3;
    size_t ae = u.find_first_of("/?#", as);
    if (ae == std::string::npos) ae = u.size();
    p.authority = u.substr(as, ae - as);
    size_t i = ae;
    size_t pe = u.find_first_of("?#", i);
    if (pe == std::string::npos) pe = u.size();
    p.path = u.substr(i, pe - i);
    if (pe < u.size() && u[pe] == '?') {
        size_t qe = u.find('#', pe);
        if (qe == std::string::npos) qe = u.size();
        p.query = u.substr(pe, qe - pe);
    }
    p.valid = true;
    return p;
}

static bool has_scheme(const std::string& r) {
    if (r.empty() || !((r[0] >= 'a' && r[0] <= 'z') || (r[0] >= 'A' && r[0] <= 'Z'))) return false;
    for (size_t i = 1; i < r.size(); ++i) {
        char c = r[i];
        if (c == ':') return true;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '+' || c == '-' || c == '.')) return false;
    }
    return false;
}

static std::string remove_dots(const std::string& path) {
    std::vector<std::string> segs, out;
    size_t st = 0;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            segs.push_back(path.substr(st, i - st));
            st = i + 1;
        }
    }
    bool trailing = false;
    for (size_t k = 0; k < segs.size(); ++k) {
        bool last = (k + 1 == segs.size());
        if (segs[k] == ".") { if (last) trailing = true; }
        else if (segs[k] == "..") { if (out.size() > 1) out.pop_back(); if (last) trailing = true; }
        else out.push_back(segs[k]);
    }
    if (trailing) out.push_back("");
    std::string r;
    for (size_t k = 0; k < out.size(); ++k) { if (k) r += '/'; r += out[k]; }
    if (r.empty()) r = "/";
    return r;
}

std::string urljoin(const std::string& base, const std::string& ref) {
    if (ref.empty()) return base;
    if (has_scheme(ref)) return ref;
    UrlParts b = split_url(base);
    if (!b.valid) return ref;
    if (ref.size() >= 2 && ref[0] == '/' && ref[1] == '/') return b.scheme + ":" + ref;

    size_t re = ref.find_first_of("?#");
    std::string rpath = ref.substr(0, re == std::string::npos ? ref.size() : re);
    std::string rrest = re == std::string::npos ? "" : ref.substr(re);

    std::string path;
    if (rpath.empty()) {
        path = b.path.empty() ? "/" : b.path;
        if (!rrest.empty() && rrest[0] == '#') rrest = b.query + rrest;
    } else if (rpath[0] == '/') {
        path = rpath;
    } else {
        size_t ls = b.path.rfind('/');
        std::string dir = ls == std::string::npos ? "/" : b.path.substr(0, ls + 1);
        path = dir + rpath;
    }
    return b.scheme + "://" + b.authority + remove_dots(path) + rrest;
}

/* ---------------- UTF-8 ---------------- */

std::string sanitize_utf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0, n = s.size();
    if (n >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) i = 3;
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { out += (char)c; ++i; continue; }
        int len = 0; unsigned cp = 0;
        if (c >= 0xC2 && c <= 0xDF) { len = 2; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
        else if (c >= 0xF0 && c <= 0xF4) { len = 4; cp = c & 0x07; }
        bool ok = len > 0 && i + (size_t)len <= n;
        if (ok) {
            for (int k = 1; k < len; ++k) {
                unsigned char cc = (unsigned char)s[i + k];
                if ((cc & 0xC0) != 0x80) { ok = false; break; }
                cp = (cp << 6) | (cc & 0x3F);
            }
        }
        if (ok) {
            if (len == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) ok = false;
            if (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ok = false;
        }
        if (ok) { out.append(s, i, (size_t)len); i += (size_t)len; }
        else { out += "\xEF\xBF\xBD"; ++i; }
    }
    return out;
}

static void append_utf8(std::string& o, unsigned cp) {
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
}

/* ---------------- HTML entities ----------------
 * Subset of Python's html.unescape: numeric references (with the Windows-1252
 * remap for 0x80-0x9F) plus the common named entities, Latin-1 included. */

static const std::unordered_map<std::string, unsigned>& entity_table() {
    static std::unordered_map<std::string, unsigned> t;
    if (!t.empty()) return t;
    struct E { const char* n; unsigned c; };
    static const E list[] = {
        {"amp",38},{"lt",60},{"gt",62},{"quot",34},{"apos",39},{"nbsp",160},{"iexcl",161},
        {"cent",162},{"pound",163},{"curren",164},{"yen",165},{"brvbar",166},{"sect",167},
        {"uml",168},{"copy",169},{"ordf",170},{"laquo",171},{"not",172},{"shy",173},{"reg",174},
        {"macr",175},{"deg",176},{"plusmn",177},{"sup2",178},{"sup3",179},{"acute",180},
        {"micro",181},{"para",182},{"middot",183},{"cedil",184},{"sup1",185},{"ordm",186},
        {"raquo",187},{"frac14",188},{"frac12",189},{"frac34",190},{"iquest",191},
        {"ensp",8194},{"emsp",8195},{"thinsp",8201},{"zwnj",8204},{"zwj",8205},{"lrm",8206},{"rlm",8207},
        {"ndash",8211},{"mdash",8212},{"lsquo",8216},{"rsquo",8217},{"sbquo",8218},{"ldquo",8220},
        {"rdquo",8221},{"bdquo",8222},{"dagger",8224},{"Dagger",8225},{"bull",8226},{"hellip",8230},
        {"permil",8240},{"prime",8242},{"Prime",8243},{"lsaquo",8249},{"rsaquo",8250},{"euro",8364},
        {"trade",8482},{"larr",8592},{"uarr",8593},{"rarr",8594},{"darr",8595},{"harr",8596},
        {"minus",8722},{"infin",8734},{"ne",8800},{"le",8804},{"ge",8805},{"hearts",9829}
    };
    for (size_t i = 0; i < sizeof(list) / sizeof(list[0]); ++i) t[list[i].n] = list[i].c;
    static const char* latin1[] = {
        "Agrave","Aacute","Acirc","Atilde","Auml","Aring","AElig","Ccedil","Egrave","Eacute","Ecirc","Euml",
        "Igrave","Iacute","Icirc","Iuml","ETH","Ntilde","Ograve","Oacute","Ocirc","Otilde","Ouml","times",
        "Oslash","Ugrave","Uacute","Ucirc","Uuml","Yacute","THORN","szlig","agrave","aacute","acirc","atilde",
        "auml","aring","aelig","ccedil","egrave","eacute","ecirc","euml","igrave","iacute","icirc","iuml",
        "eth","ntilde","ograve","oacute","ocirc","otilde","ouml","divide","oslash","ugrave","uacute","ucirc",
        "uuml","yacute","thorn","yuml"
    };
    for (unsigned i = 0; i < 64; ++i) t[latin1[i]] = 192 + i;
    return t;
}

static const unsigned cp1252[32] = {
    0x20AC,0x81,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,0x02C6,0x2030,0x0160,0x2039,0x0152,0x8D,0x017D,0x8F,
    0x90,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,0x02DC,0x2122,0x0161,0x203A,0x0153,0x9D,0x017E,0x0178
};

std::string decode_entities(const std::string& s) {
    if (s.find('&') == std::string::npos) return s;
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] != '&') { out += s[i++]; continue; }
        size_t semi = s.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 32) { out += '&'; ++i; continue; }
        std::string name = s.substr(i + 1, semi - i - 1);
        unsigned cp = 0; bool ok = false;
        if (!name.empty() && name[0] == '#') {
            bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
            size_t k = hex ? 2 : 1;
            unsigned long v = 0; bool any = false, bad = false;
            for (; k < name.size(); ++k) {
                char c = name[k]; int d;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (hex && c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else if (hex && c >= 'A' && c <= 'F') d = c - 'A' + 10;
                else { bad = true; break; }
                any = true;
                v = v * (hex ? 16 : 10) + (unsigned long)d;
                if (v > 0x10FFFF) v = 0x110000;
            }
            if (any && !bad) {
                ok = true;
                if (v == 0 || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) cp = 0xFFFD;
                else if (v >= 0x80 && v <= 0x9F) cp = cp1252[v - 0x80];
                else cp = (unsigned)v;
            }
        } else {
            const std::unordered_map<std::string, unsigned>& t = entity_table();
            std::unordered_map<std::string, unsigned>::const_iterator it = t.find(name);
            if (it != t.end()) { cp = it->second; ok = true; }
        }
        if (ok) { append_utf8(out, cp); i = semi + 1; }
        else { out += '&'; ++i; }
    }
    return out;
}

} // namespace luna
