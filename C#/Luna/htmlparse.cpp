/* Luna HTML tokenizer / element builder (port of PyHTML_Canvas.parse_and_format) */
#include "luna_internal.hpp"
#include <cctype>
#include <cstring>

namespace luna {

struct Attr { std::string name, value; };

static bool sp(char c) { return std::isspace((unsigned char)c) != 0; }
static char lc1(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static std::vector<Attr> parse_attrs(const std::string& s) {
    std::vector<Attr> out;
    size_t i = 0, n = s.size();
    while (i < n) {
        while (i < n && (sp(s[i]) || s[i] == '/')) ++i;
        if (i >= n) break;
        size_t ns = i;
        while (i < n && !sp(s[i]) && s[i] != '=' && s[i] != '/') ++i;
        Attr a;
        a.name = lower(s.substr(ns, i - ns));
        while (i < n && sp(s[i])) ++i;
        if (i < n && s[i] == '=') {
            ++i;
            while (i < n && sp(s[i])) ++i;
            if (i < n && (s[i] == '"' || s[i] == '\'')) {
                char q = s[i++];
                size_t vs = i;
                while (i < n && s[i] != q) ++i;
                a.value = s.substr(vs, i - vs);
                if (i < n) ++i;
            } else {
                size_t vs = i;
                while (i < n && !sp(s[i])) ++i;
                a.value = s.substr(vs, i - vs);
            }
        }
        if (!a.name.empty()) out.push_back(a);
    }
    return out;
}

/* "200", "200px" -> 200; "50%" or garbage -> 0 */
static int parse_dim(const std::string* v) {
    if (!v) return 0;
    std::string s = trim(*v);
    size_t i = 0; long n = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') { n = n * 10 + (s[i] - '0'); if (n > 100000) n = 100000; ++i; }
    if (i == 0) return 0;
    if (i < s.size() && s[i] == '%') return 0;
    return (int)n;
}

static const std::string* get_attr(const std::vector<Attr>& a, const char* name) {
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].name == name) return &a[i].value;
    return NULL;
}

/* "<name" followed by whitespace, '>' or '/' at position i (h[i] == '<') */
static bool tag_open_at(const std::string& h, size_t i, const char* name) {
    size_t L = std::strlen(name);
    if (i + 1 + L >= h.size()) return false;
    for (size_t k = 0; k < L; ++k)
        if (lc1(h[i + 1 + k]) != name[k]) return false;
    char c = h[i + 1 + L];
    return c == '>' || c == '/' || sp(c);
}

/* position of "</name" that is a real closing tag, or npos */
static size_t find_close(const std::string& h, const char* name, size_t from) {
    std::string close = std::string("</") + name;
    size_t e = find_ci(h, close, from);
    while (e != std::string::npos) {
        size_t after = e + close.size();
        if (after < h.size() && (h[after] == '>' || sp(h[after]))) return e;
        e = find_ci(h, close, e + 1);
    }
    return std::string::npos;
}

/* <style ...>(.*?)</style> */
static void collect_styles(const std::string& h, Aurelius& aur) {
    size_t pos = 0;
    while (pos < h.size()) {
        size_t s = h.find('<', pos);
        if (s == std::string::npos) break;
        if (!tag_open_at(h, s, "style")) { pos = s + 1; continue; }
        size_t gt = h.find('>', s);
        if (gt == std::string::npos) break;
        size_t e = find_close(h, "style", gt + 1);
        if (e == std::string::npos) { pos = gt + 1; continue; }
        aur.parse(h.substr(gt + 1, e - gt - 1));
        size_t eg = h.find('>', e);
        pos = eg == std::string::npos ? h.size() : eg + 1;
    }
}

/* Drop <script>, <style> and <head> blocks. Unlike the Python regex, the tag
 * name must match exactly, so <header> is not mistaken for <head>. */
static std::string strip_blocks(const std::string& h) {
    static const char* names[] = { "script", "style", "head" };
    std::string out;
    out.reserve(h.size());
    size_t i = 0, n = h.size();
    while (i < n) {
        if (h[i] == '<') {
            bool skipped = false;
            for (int k = 0; k < 3 && !skipped; ++k) {
                if (!tag_open_at(h, i, names[k])) continue;
                size_t gt = h.find('>', i);
                if (gt == std::string::npos) break;
                size_t e = find_close(h, names[k], gt + 1);
                if (e == std::string::npos) break;
                size_t eg = h.find('>', e);
                if (eg == std::string::npos) break;
                i = eg + 1;
                skipped = true;
            }
            if (skipped) continue;
        }
        out += h[i++];
    }
    return out;
}

struct Token { bool is_tag; std::string s; };

static size_t find_tag_end(const std::string& h, size_t from, bool quotes) {
    char q = 0;
    for (size_t j = from; j < h.size(); ++j) {
        char c = h[j];
        if (quotes && q) { if (c == q) q = 0; }
        else if (quotes && (c == '"' || c == '\'')) q = c;
        else if (c == '>') return j;
    }
    return std::string::npos;
}

static void tokenize(const std::string& h, std::vector<Token>& toks) {
    std::string text;
    size_t i = 0, n = h.size();
    while (i < n) {
        if (h[i] == '<') {
            if (h.compare(i, 4, "<!--") == 0) {
                size_t e = h.find("-->", i + 4);
                if (!text.empty()) { Token t; t.is_tag = false; t.s = text; toks.push_back(t); text.clear(); }
                i = (e == std::string::npos) ? n : e + 3;
                continue;
            }
            if (i + 1 < n && (std::isalpha((unsigned char)h[i + 1]) || h[i + 1] == '/' ||
                              h[i + 1] == '!' || h[i + 1] == '?')) {
                size_t j = find_tag_end(h, i + 1, true);
                if (j == std::string::npos) j = find_tag_end(h, i + 1, false);
                if (j != std::string::npos) {
                    if (!text.empty()) { Token t; t.is_tag = false; t.s = text; toks.push_back(t); text.clear(); }
                    Token t; t.is_tag = true; t.s = h.substr(i + 1, j - i - 1);
                    toks.push_back(t);
                    i = j + 1;
                    continue;
                }
            }
        }
        text += h[i++];
    }
    if (!text.empty()) { Token t; t.is_tag = false; t.s = text; toks.push_back(t); }
}

static bool is_text_tag(const std::string& t) {
    return t == "h1" || t == "h2" || t == "h3" || t == "h4" || t == "h5" || t == "h6" ||
           t == "address" || t == "p" || t == "li" || t == "small" || t == "span";
}

void parse_and_format(const std::string& raw_html, const std::string& base_url,
                      Aurelius& aur, std::vector<Element>& elements, std::string& bg_color) {
    elements.clear();
    aur.reset();

    collect_styles(raw_html, aur);

    std::string bg = aur.get_prop("body", std::vector<std::string>(), "", "background-color");
    bg_color = (bg != "undefined") ? bg : "#ffffff";

    std::string clean = strip_blocks(raw_html);
    std::vector<Token> toks;
    tokenize(clean, toks);

    std::string curr_tag = "p", curr_url, curr_id;
    std::vector<std::string> curr_classes;
    bool is_bold = false, is_italic = false;
    PropMap inline_css;
    int last_video = -1;

    for (size_t ti = 0; ti < toks.size(); ++ti) {
        if (!toks[ti].is_tag) {
            std::string t = trim(toks[ti].s);
            if (t.empty()) continue;
            Element el;
            el.tag = curr_tag; el.text = decode_entities(t); el.url = curr_url;
            el.bold = is_bold; el.italic = is_italic; el.is_text = true;
            el.css = inline_css; el.id = curr_id; el.classes = curr_classes;
            elements.push_back(el);
            continue;
        }

        std::string inner = trim(toks[ti].s);
        size_t ws = 0;
        while (ws < inner.size() && !sp(inner[ws])) ++ws;
        std::string tag_name = lower(inner.substr(0, ws));
        /* <br/> style self-closing: not handled by the Python version */
        if (tag_name.size() > 1 && tag_name[0] != '/' && tag_name[tag_name.size() - 1] == '/')
            tag_name.erase(tag_name.size() - 1);
        std::vector<Attr> attrs = parse_attrs(inner.substr(ws));

        const std::string* a_id = get_attr(attrs, "id");
        const std::string* a_class = get_attr(attrs, "class");
        const std::string* a_style = get_attr(attrs, "style");
        curr_id = a_id ? *a_id : std::string();
        curr_classes = a_class ? split_ws(*a_class) : std::vector<std::string>();
        inline_css = a_style ? Aurelius::parse_inline(*a_style) : PropMap();

        if (tag_name == "b" || tag_name == "strong") is_bold = true;
        else if (tag_name == "i" || tag_name == "em") is_italic = true;
        else if (tag_name == "/b" || tag_name == "/strong") is_bold = false;
        else if (tag_name == "/i" || tag_name == "/em") is_italic = false;
        else if (tag_name == "hr") {
            Element el; el.tag = "hr"; el.css = inline_css; el.id = curr_id; el.classes = curr_classes;
            elements.push_back(el);
        }
        else if (tag_name == "br") {
            Element el; el.tag = "br";
            elements.push_back(el);
        }
        else if (tag_name == "img") {
            const std::string* src = get_attr(attrs, "src");
            if (src && !src->empty()) {
                Element el; el.tag = "img"; el.url = urljoin(base_url, *src); el.css = inline_css;
                el.id = curr_id; el.classes = curr_classes; el.link = curr_url;
                const std::string* alt = get_attr(attrs, "alt");
                if (alt) el.text = decode_entities(*alt);
                el.width = parse_dim(get_attr(attrs, "width"));
                el.height = parse_dim(get_attr(attrs, "height"));
                elements.push_back(el);
            }
        }
        else if (tag_name == "video") {
            /* src may come from a <source> child; elements without any source are dropped at the end */
            Element el; el.tag = "video"; el.css = inline_css; el.id = curr_id; el.classes = curr_classes;
            const std::string* src = get_attr(attrs, "src");
            if (src && !src->empty()) el.url = urljoin(base_url, *src);
            el.width = parse_dim(get_attr(attrs, "width"));
            el.height = parse_dim(get_attr(attrs, "height"));
            elements.push_back(el);
            last_video = (int)elements.size() - 1;
        }
        else if (tag_name == "source") {
            const std::string* src = get_attr(attrs, "src");
            if (last_video >= 0 && elements[(size_t)last_video].url.empty() && src && !src->empty())
                elements[(size_t)last_video].url = urljoin(base_url, *src);
        }
        else if (tag_name == "a") {
            curr_tag = "a";
            const std::string* href = get_attr(attrs, "href");
            curr_url = (href && !href->empty()) ? *href : std::string();
        }
        else if (is_text_tag(tag_name)) curr_tag = tag_name;
        else if (!tag_name.empty() && tag_name[0] == '/') {
            if (tag_name == "/a") curr_url.clear();
            if (tag_name == "/video") last_video = -1;
            /* only reset to 'p' if we are not mid-inline sequence */
            if (tag_name != "/span") curr_tag = "p";
            curr_id.clear();
            curr_classes.clear();
        }
    }

    /* drop videos that never got a source */
    size_t w = 0;
    for (size_t r = 0; r < elements.size(); ++r) {
        if (elements[r].tag == "video" && elements[r].url.empty()) continue;
        if (w != r) elements[w] = elements[r];
        ++w;
    }
    elements.resize(w);
}

} // namespace luna
