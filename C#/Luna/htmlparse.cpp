/* Luna HTML tokenizer / element builder (port of PyHTML_Canvas.parse_and_format) */
#include "luna_internal.hpp"
#include <cctype>
#include <cstdlib>
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

/* href/src values: trimmed and entity-decoded, so "?a=1&amp;b=2" becomes "?a=1&b=2" */
static std::string attr_url(const std::string* v) {
    return v ? decode_entities(trim(*v)) : std::string();
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

/* inline formatting tags (open or close, with or without the slash): they do not end a run of text */
static bool is_inline_tag(const std::string& name) {
    std::string n = (!name.empty() && name[0] == '/') ? name.substr(1) : name;
    static const char* tags[] = { "a", "b", "strong", "i", "em", "span", "small", "u", "s", "strike", "code", "tt",
        "kbd", "samp", "var", "sup", "sub", "font", "abbr", "acronym", "cite", "dfn", "mark", "q", "big", "label",
        "bdi", "bdo", "ins", "del", "time", "data", "wbr", "button", "input" };
    for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); ++i) if (n == tags[i]) return true;
    return false;
}

struct ListCtx { bool ordered; int counter; };

/* inner text of a closing tag token, name given with the slash: "/button" */
static bool is_close_tag(const std::string& inner, const char* name) {
    size_t n = std::strlen(name);
    if (inner.size() < n || find_ci(inner, name) != 0) return false;
    return inner.size() == n || sp(inner[n]);
}

static PropMap attr_map(const std::vector<Attr>& a) {
    PropMap m;
    for (size_t i = 0; i < a.size(); ++i) m.push_back(std::make_pair(a[i].name, decode_entities(trim(a[i].value))));
    return m;
}

/* inner text of a "</video>" tag token */
static bool is_video_close(const std::string& inner) {
    if (inner.size() < 6 || find_ci(inner, "/video") != 0) return false;
    return inner.size() == 6 || sp(inner[6]);
}

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
    bool in_video = false;   /* inside <video>...</video>: the fallback content is not drawn, NyxPlayer plays the video */

    std::vector<ListCtx> lists;                 /* open <ul>/<ol>, innermost last */
    bool li_pending = false;                    /* the next element is the first of a list item */
    int li_marker = 0, li_number = 0;
    std::string block_tag = "p";                /* tag of the surrounding block, for text after </a> and </span> */
    bool run_open = false;                      /* a text element was already emitted and only inline tags came since */

    int table_depth = 0;                        /* open <table> elements */
    bool in_th = false;                         /* inside <th>: bold */
    bool in_button = false;                     /* inside <button>...</button>: the content becomes the button label */
    Element btn;
    std::string btn_text;

    /* every drawable element goes through here: list info, and the new-line / same-line decision */
    struct Emit {
        std::vector<Element>& out; std::vector<ListCtx>& lists; bool& li_pending; int& li_marker; int& li_number; bool& run_open;
        void operator()(Element& el) {
            el.list_depth = (int)lists.size();
            if (li_pending) {
                el.list_marker = li_marker; el.list_number = li_number; el.layout |= kLayoutNewline; li_pending = false;
            } else if ((el.is_text || el.tag == "button") && run_open) {
                if (el.is_text) el.layout |= kLayoutInline;      /* "text <b>bold</b> more" stays on one line */
                /* the element before it must not end the line either, even when its tag is a block like <p> */
                if (!out.empty() && (out.back().is_text || out.back().tag == "button") && !(out.back().layout & kLayoutInline))
                    out.back().layout |= kLayoutContinues;
            }
            if (el.is_text || el.tag == "button") run_open = true;
            out.push_back(el);
        }
    } emit = { elements, lists, li_pending, li_marker, li_number, run_open };

    for (size_t ti = 0; ti < toks.size(); ++ti) {
        if (!toks[ti].is_tag) {
            if (in_video) continue;
            if (in_button) {            /* label of the button: all its text joined */
                std::string bt = trim(toks[ti].s);
                if (!bt.empty()) { if (!btn_text.empty()) btn_text += ' '; btn_text += decode_entities(bt); }
                continue;
            }
            std::string t = trim(toks[ti].s);
            if (t.empty()) continue;
            Element el;
            el.tag = curr_tag; el.text = decode_entities(t); el.url = curr_url;
            el.bold = is_bold || in_th; el.italic = is_italic; el.is_text = true;
            el.css = inline_css; el.id = curr_id; el.classes = curr_classes;
            emit(el);
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

        if (in_video) {
            if (tag_name == "/video") in_video = false;      /* then handled below like any closing tag */
            else {
                /* fallback text, images and links inside <video> are skipped; only <source> matters */
                if (tag_name == "source") {
                    std::string src = attr_url(get_attr(attrs, "src"));
                    if (last_video >= 0 && elements[(size_t)last_video].url.empty() && !src.empty())
                        elements[(size_t)last_video].url = urljoin(base_url, src);
                }
                continue;
            }
        }

        if (in_button) {
            if (tag_name != "/button") continue;           /* formatting, images and links inside a button are skipped */
            btn.text = btn_text;
            in_button = false;
            emit(btn);
        }

        if (!is_inline_tag(tag_name)) run_open = false;    /* a block-level tag ends the current run of text */

        const std::string* a_id = get_attr(attrs, "id");
        const std::string* a_class = get_attr(attrs, "class");
        const std::string* a_style = get_attr(attrs, "style");
        curr_id = a_id ? *a_id : std::string();
        curr_classes = a_class ? split_ws(*a_class) : std::vector<std::string>();
        inline_css = a_style ? Aurelius::parse_inline(*a_style) : PropMap();

        /* table structure markers carry their attributes; the shell builds the grid from the sequence */
        struct Marker {
            std::vector<Element>& out; int depth;
            void operator()(const std::string& name, const PropMap& css, const std::string& id,
                            const std::vector<std::string>& classes, const PropMap& attrs) {
                Element el; el.tag = name; el.css = css; el.id = id; el.classes = classes; el.attrs = attrs;
                el.list_depth = depth;
                out.push_back(el);
            }
        } marker = { elements, (int)lists.size() };

        if (tag_name == "b" || tag_name == "strong") is_bold = true;
        else if (tag_name == "i" || tag_name == "em") is_italic = true;
        else if (tag_name == "/b" || tag_name == "/strong") is_bold = false;
        else if (tag_name == "/i" || tag_name == "/em") is_italic = false;
        else if (tag_name == "hr") {
            Element el; el.tag = "hr"; el.css = inline_css; el.id = curr_id; el.classes = curr_classes;
            emit(el);
        }
        else if (tag_name == "br") {
            Element el; el.tag = "br";
            elements.push_back(el);
        }
        else if (tag_name == "img") {
            std::string src = attr_url(get_attr(attrs, "src"));
            if (!src.empty()) {
                Element el; el.tag = "img"; el.url = urljoin(base_url, src); el.css = inline_css;
                el.id = curr_id; el.classes = curr_classes; el.link = curr_url;
                const std::string* alt = get_attr(attrs, "alt");
                if (alt) el.text = decode_entities(*alt);
                el.width = parse_dim(get_attr(attrs, "width"));
                el.height = parse_dim(get_attr(attrs, "height"));
                emit(el);
            }
        }
        else if (tag_name == "video") {
            /* src may come from a <source> child; elements without any source are dropped at the end */
            Element el; el.tag = "video"; el.css = inline_css; el.id = curr_id; el.classes = curr_classes;
            std::string src = attr_url(get_attr(attrs, "src"));
            if (!src.empty()) el.url = urljoin(base_url, src);
            el.width = parse_dim(get_attr(attrs, "width"));
            el.height = parse_dim(get_attr(attrs, "height"));
            emit(el);
            last_video = (int)elements.size() - 1;

            /* swallow the fallback content, but only when the tag really is closed later (never eat the rest of a page) */
            bool self_closed = !inner.empty() && inner[inner.size() - 1] == '/';
            if (!self_closed) {
                for (size_t k = ti + 1; k < toks.size(); ++k)
                    if (toks[k].is_tag && is_video_close(trim(toks[k].s))) { in_video = true; break; }
            }
        }
        else if (tag_name == "source") {
            std::string src = attr_url(get_attr(attrs, "src"));
            if (last_video >= 0 && elements[(size_t)last_video].url.empty() && !src.empty())
                elements[(size_t)last_video].url = urljoin(base_url, src);
        }
        else if (tag_name == "a") {
            curr_tag = "a";
            curr_url = attr_url(get_attr(attrs, "href"));
        }
        else if (tag_name == "table") {
            ++table_depth;
            marker("table", inline_css, curr_id, curr_classes, attr_map(attrs));
        }
        else if (table_depth > 0 && (tag_name == "tr" || tag_name == "caption")) {
            marker(tag_name, inline_css, curr_id, curr_classes, attr_map(attrs));
        }
        else if (table_depth > 0 && (tag_name == "td" || tag_name == "th")) {
            in_th = (tag_name == "th");
            marker(tag_name, inline_css, curr_id, curr_classes, attr_map(attrs));
        }
        else if (tag_name == "button") {
            btn = Element();
            btn.tag = "button"; btn.css = inline_css; btn.id = curr_id; btn.classes = curr_classes;
            btn.url = curr_url; btn.attrs = attr_map(attrs);
            if (get_attr(attrs, "disabled")) btn.layout |= kLayoutDisabled;
            btn_text.clear();
            bool self_closed = !inner.empty() && inner[inner.size() - 1] == '/';
            if (!self_closed) {
                for (size_t k = ti + 1; k < toks.size(); ++k)
                    if (toks[k].is_tag && is_close_tag(trim(toks[k].s), "/button")) { in_button = true; break; }
            }
            if (!in_button) emit(btn);      /* never closed: an empty button, the rest of the page stays visible */
        }
        else if (tag_name == "input") {
            const std::string* ty = get_attr(attrs, "type");
            std::string type = ty ? lower(trim(*ty)) : std::string("text");
            if (type == "button" || type == "submit" || type == "reset") {
                Element el;
                el.tag = "button"; el.css = inline_css; el.id = curr_id; el.classes = curr_classes;
                el.url = curr_url; el.attrs = attr_map(attrs);
                const std::string* v = get_attr(attrs, "value");
                el.text = v ? decode_entities(trim(*v)) : (type == "submit" ? std::string("Submit") : type == "reset" ? std::string("Reset") : std::string());
                if (get_attr(attrs, "disabled")) el.layout |= kLayoutDisabled;
                emit(el);
            }
        }
        else if (tag_name == "ul" || tag_name == "ol") {
            ListCtx L; L.ordered = (tag_name == "ol"); L.counter = 0;
            if (L.ordered) {
                const std::string* st = get_attr(attrs, "start");
                if (st) L.counter = std::atoi(st->c_str()) - 1;
            }
            lists.push_back(L);
            li_pending = false;
        }
        else if (tag_name == "li") {
            curr_tag = "li"; block_tag = "li";
            li_pending = true; li_marker = 0; li_number = 0;
            if (!lists.empty()) {
                ListCtx& L = lists.back();
                if (L.ordered) {
                    const std::string* v = get_attr(attrs, "value");
                    if (v) L.counter = std::atoi(v->c_str()) - 1;
                    ++L.counter;
                    li_marker = 4; li_number = L.counter;
                } else {
                    size_t d = lists.size();
                    li_marker = d == 1 ? 1 : (d == 2 ? 2 : 3);     /* disc, circle, square by depth */
                }
            }
        }
        else if (is_text_tag(tag_name)) {
            curr_tag = tag_name;
            if (tag_name != "span" && tag_name != "small") block_tag = tag_name;
        }
        else if (!tag_name.empty() && tag_name[0] == '/') {
            if (tag_name == "/a") curr_url.clear();
            if (tag_name == "/video") last_video = -1;
            if ((tag_name == "/table" || tag_name == "/caption") && table_depth > 0) {
                if (tag_name == "/table") { --table_depth; in_th = false; }
                Element el; el.tag = tag_name; el.list_depth = (int)lists.size();
                elements.push_back(el);
            }
            if (tag_name == "/td" || tag_name == "/th" || tag_name == "/tr") in_th = false;
            if ((tag_name == "/ul" || tag_name == "/ol") && !lists.empty()) lists.pop_back();
            if (tag_name == "/li" || tag_name == "/ul" || tag_name == "/ol") li_pending = false;
            if (tag_name == "/a" || tag_name == "/span") curr_tag = block_tag;   /* back to the font of the surrounding block */
            else if (!is_inline_tag(tag_name)) { curr_tag = "p"; block_tag = "p"; }
            curr_id.clear();
            curr_classes.clear();
        }
    }

    /* close tables that were never closed */
    while (table_depth-- > 0) {
        Element el; el.tag = "/table";
        elements.push_back(el);
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
