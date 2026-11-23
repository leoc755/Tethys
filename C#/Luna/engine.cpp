/* Luna engine + page logic and the C API (port of CelesteEngine and BrowserTab) */
#include "luna.h"
#include "luna_internal.hpp"
#include <cstdlib>
#include <cstring>
#include <new>

using namespace luna;

struct LunaEngine {
    std::string version, shell_name, shell_ver, cssver, home_url, ua, scratch;
    std::vector<std::pair<std::string, std::string> > bookmarks;
    struct Img { bool ok; std::vector<unsigned char> data; };
    std::unordered_map<std::string, Img> image_cache;

    LunaEngine() {
        version = LUNA_ENGINE_VERSION;
        shell_name = "Callisto";
        shell_ver = LUNA_SHELL_VERSION;
        cssver = LUNA_CSS_VERSION;
        home_url = "https://nxhp.pages.dev/";
        bookmarks.push_back(std::make_pair("XAMPP", "http://192.168.12.154/"));
        bookmarks.push_back(std::make_pair("Acid1", "https://www.w3.org/Style/CSS/Test/Acid1/test.html"));
        bookmarks.push_back(std::make_pair("Acid2", "https://acid2.acidtests.org/"));
        bookmarks.push_back(std::make_pair("Acid3", "http://acid3.acidtests.org/"));
    }

    std::string get_ua() const {
        return "Mozilla/5.0 (" + os_info() + ") Nyxora/1.14 Nex/27.1.2 " + shell_name + "/" + shell_ver +
               " (Luna, rv:" + version + ")";
    }

    static bool is_redirect(int s) { return s == 301 || s == 302 || s == 303 || s == 307 || s == 308; }

    /* Luna 1.16 core: manual redirects, security warning signal */
    std::string nexia_request(const std::string& start_url, int redirects_left,
                              bool ignore_security, std::string& final_url) {
        std::string url = start_url;
        const std::string ua_str = get_ua();
        for (;;) {
            final_url = url;
            if (redirects_left < 0) return "<h1>Engine Error</h1><p>Too many redirects.</p>";
            HttpResult r = http_get(url, ua_str, ignore_security, 5000, 64u * 1024u * 1024u);
            if (r.security_error) return "LUNA_SECURITY_ERROR";
            if (!r.ok) return "<h1>Engine Alert</h1><p>" + escape_html(r.error) + "</p>";
            if (is_redirect(r.status) && !r.location.empty()) {
                url = urljoin(url, r.location);
                --redirects_left;
                continue;
            }
            return sanitize_utf8(r.body);
        }
    }
};

struct LunaPage {
    LunaEngine* eng;
    std::vector<std::string> history, forward_stack;
    std::string base_url, raw_source, title, bg_color, scratch, report;
    std::vector<Element> elements;
    Aurelius aur;
    LunaStatusCallback cb;
    void* cb_user;

    explicit LunaPage(LunaEngine* e) : eng(e), title("New Tab"), bg_color("#ffffff"), cb(NULL), cb_user(NULL) {}

    void status(int loading, const std::string& url) { if (cb) cb(loading, url.c_str(), cb_user); }

    void apply_source(const std::string& source, const std::string& final_url) {
        base_url = final_url;
        raw_source = source;
        /* title: <title>...</title>, removed from the source before parsing */
        std::string t;
        bool found = false;
        size_t s = find_ci(raw_source, "<title>");
        if (s != std::string::npos) {
            size_t e = find_ci(raw_source, "</title>", s + 7);
            if (e != std::string::npos) { t = raw_source.substr(s + 7, e - s - 7); found = true; }
        }
        title = found ? t : "New Tab";
        for (;;) {
            size_t a = find_ci(raw_source, "<title>");
            if (a == std::string::npos) break;
            size_t b = find_ci(raw_source, "</title>", a + 7);
            if (b == std::string::npos) break;
            raw_source.erase(a, b + 8 - a);
        }
        parse_and_format(raw_source, base_url, aur, elements, bg_color);
    }

    int load(std::string url, int flags) {
        bool save_history = (flags & LUNA_LOAD_SAVE_HISTORY) != 0;
        bool clear_forward = (flags & LUNA_LOAD_CLEAR_FORWARD) != 0;
        bool ignore_security = (flags & LUNA_LOAD_IGNORE_SECURITY) != 0;

        status(1, url);
        if (!starts_with(url, "http://") && !starts_with(url, "https://")) url = "http://" + url;

        std::string final_url;
        std::string source = eng->nexia_request(url, 20, ignore_security, final_url);
        int rc = LUNA_OK;

        if (source == "LUNA_SECURITY_ERROR") {
            std::string html =
                "<h1 style=\"color: #ff0000;\">Security Warning</h1>\n"
                "<span><b>Warning!</b> The connection to " + url + " is not secure.</span><br>\n"
                "<span>[ This could be due to a <b>self-signed certificate</b> or an <b>outdated security key</b>]</span><br>\n"
                "<span>Are you sure you want to proceed?</span><br>\n"
                "<a href=\"luna://proceed?url=" + url + "\">[ Proceed Anyway (Unsafe) ]</a>\n";
            base_url = url;
            raw_source = html;
            parse_and_format(raw_source, base_url, aur, elements, bg_color);
            title = "Security Warning";
            rc = LUNA_SECURITY_WARNING;
        } else {
            if (save_history && !base_url.empty() && base_url != final_url) history.push_back(base_url);
            if (clear_forward) forward_stack.clear();
            apply_source(source, final_url);
        }
        status(0, "");
        return rc;
    }
};

static const char* keep(std::string& slot, const std::string& v) { slot = v; return slot.c_str(); }
static std::string S(const char* p) { return p ? std::string(p) : std::string(); }
static char* dup_cstr(const std::string& s) {
    char* m = (char*)std::malloc(s.size() + 1);
    if (m) std::memcpy(m, s.c_str(), s.size() + 1);
    return m;
}

extern "C" {

/* ---------------- engine ---------------- */

LUNA_API LunaEngine* luna_engine_create(void) { return new (std::nothrow) LunaEngine(); }
LUNA_API void luna_engine_destroy(LunaEngine* e) { delete e; }
LUNA_API const char* luna_engine_version(LunaEngine* e) { return e->version.c_str(); }
LUNA_API const char* luna_engine_shell_version(LunaEngine* e) { return e->shell_ver.c_str(); }
LUNA_API const char* luna_engine_css_version(LunaEngine* e) { return e->cssver.c_str(); }
LUNA_API void luna_engine_set_shell(LunaEngine* e, const char* name, const char* version) {
    if (name && *name) e->shell_name = name;
    if (version && *version) e->shell_ver = version;
}
LUNA_API const char* luna_engine_home_url(LunaEngine* e) { return e->home_url.c_str(); }
LUNA_API int luna_engine_bookmark_count(LunaEngine* e) { return (int)e->bookmarks.size(); }
LUNA_API const char* luna_engine_bookmark_name(LunaEngine* e, int i) {
    return (i >= 0 && (size_t)i < e->bookmarks.size()) ? e->bookmarks[(size_t)i].first.c_str() : "";
}
LUNA_API const char* luna_engine_bookmark_url(LunaEngine* e, int i) {
    return (i >= 0 && (size_t)i < e->bookmarks.size()) ? e->bookmarks[(size_t)i].second.c_str() : "";
}
LUNA_API const char* luna_engine_user_agent(LunaEngine* e) { return keep(e->ua, e->get_ua()); }

LUNA_API char* luna_request(LunaEngine* e, const char* url, int ignore_security, char** final_url_out) {
    std::string final_url;
    std::string body = e->nexia_request(S(url), 20, ignore_security != 0, final_url);
    if (final_url_out) *final_url_out = dup_cstr(final_url);
    return dup_cstr(body);
}

LUNA_API const unsigned char* luna_fetch_image(LunaEngine* e, const char* url_c, int* len_out) {
    std::string url = S(url_c);
    if (len_out) *len_out = 0;
    std::unordered_map<std::string, LunaEngine::Img>::iterator it = e->image_cache.find(url);
    if (it == e->image_cache.end()) {
        LunaEngine::Img img; img.ok = false;
        /* follow redirects like the page loader, 1 second timeout per hop */
        std::string cur = url;
        for (int hop = 0; hop < 10; ++hop) {
            HttpResult r = http_get(cur, e->get_ua(), false, 1000, 32u * 1024u * 1024u);
            if (!r.ok) break;
            if (LunaEngine::is_redirect(r.status) && !r.location.empty()) { cur = urljoin(cur, r.location); continue; }
            if (r.status >= 200 && r.status < 300 && !r.body.empty()) {
                img.data.assign(r.body.begin(), r.body.end());
                img.ok = true;
            }
            break;
        }
        it = e->image_cache.insert(std::make_pair(url, img)).first;
    }
    if (!it->second.ok) return NULL;
    if (len_out) *len_out = (int)it->second.data.size();
    return &it->second.data[0];
}

LUNA_API void luna_engine_clear_image_cache(LunaEngine* e) { e->image_cache.clear(); }
LUNA_API void luna_free(void* p) { std::free(p); }

/* ---------------- page ---------------- */

LUNA_API LunaPage* luna_page_create(LunaEngine* e) { return new (std::nothrow) LunaPage(e); }
LUNA_API void luna_page_destroy(LunaPage* p) { delete p; }
LUNA_API void luna_page_set_status_callback(LunaPage* p, LunaStatusCallback cb, void* user) {
    p->cb = cb; p->cb_user = user;
}

LUNA_API int luna_page_load(LunaPage* p, const char* url, int flags) { return p->load(S(url), flags); }

LUNA_API void luna_page_set_html(LunaPage* p, const char* base_url, const char* html) {
    p->apply_source(sanitize_utf8(S(html)), S(base_url));
}

LUNA_API const char* luna_page_title(LunaPage* p) { return p->title.c_str(); }
LUNA_API const char* luna_page_base_url(LunaPage* p) { return p->base_url.c_str(); }
LUNA_API const char* luna_page_raw_source(LunaPage* p) { return p->raw_source.c_str(); }
LUNA_API const char* luna_page_bg_color(LunaPage* p) { return p->bg_color.c_str(); }

LUNA_API int luna_page_can_go_back(LunaPage* p) { return !p->history.empty(); }
LUNA_API int luna_page_can_go_forward(LunaPage* p) { return !p->forward_stack.empty(); }

LUNA_API int luna_page_back(LunaPage* p) {
    if (p->history.empty()) return LUNA_OK;
    p->forward_stack.push_back(p->base_url);
    std::string url = p->history.back();
    p->history.pop_back();
    return p->load(url, 0);
}

LUNA_API int luna_page_forward(LunaPage* p) {
    if (p->forward_stack.empty()) return LUNA_OK;
    std::string url = p->forward_stack.back();
    p->forward_stack.pop_back();
    return p->load(url, LUNA_LOAD_SAVE_HISTORY);
}

LUNA_API int luna_page_refresh(LunaPage* p) {
    return p->load(p->base_url, LUNA_LOAD_CLEAR_FORWARD);
}

LUNA_API int luna_page_click_link(LunaPage* p, const char* href_c) {
    std::string href = S(href_c);
    const std::string proceed = "luna://proceed?url=";
    if (starts_with(href, proceed.c_str()))
        return p->load(href.substr(proceed.size()), LUNA_LOAD_DEFAULT | LUNA_LOAD_IGNORE_SECURITY);
    return p->load(urljoin(p->base_url, href), LUNA_LOAD_DEFAULT);
}

LUNA_API const char* luna_page_resolve_url(LunaPage* p, const char* rel) {
    return keep(p->scratch, urljoin(p->base_url, S(rel)));
}

LUNA_API int luna_page_element_count(LunaPage* p) { return (int)p->elements.size(); }

LUNA_API int luna_page_element_get(LunaPage* p, int index, LunaElement* out) {
    if (!out || index < 0 || (size_t)index >= p->elements.size()) return 0;
    Element& el = p->elements[(size_t)index];
    el.classes_joined.clear();
    for (size_t i = 0; i < el.classes.size(); ++i) { if (i) el.classes_joined += ' '; el.classes_joined += el.classes[i]; }
    out->tag = el.tag.c_str();
    out->text = el.text.c_str();
    out->url = el.url.c_str();
    out->id = el.id.c_str();
    out->classes = el.classes_joined.c_str();
    out->bold = el.bold; out->italic = el.italic;
    out->has_inline_style = !el.css.empty();
    out->is_text = el.is_text;
    return 1;
}

LUNA_API const char* luna_page_element_prop(LunaPage* p, int index, const char* prop) {
    if (index < 0 || (size_t)index >= p->elements.size()) return "undefined";
    Element& el = p->elements[(size_t)index];
    return keep(p->scratch, p->aur.get_prop(el.tag, el.classes, el.id, S(prop), &el.css));
}

LUNA_API const char* luna_page_style_report(LunaPage* p) {
    size_t inline_count = 0;
    for (size_t i = 0; i < p->elements.size(); ++i) if (!p->elements[i].css.empty()) ++inline_count;
    const std::vector<Aurelius::Rule>& rules = p->aur.rules();
    std::string r = "--- Stellae v" + p->eng->cssver + " Diagnostics ---\n\n";
    r += "Global Selectors Found: " + std::to_string(rules.size()) + "\n";
    for (size_t i = 0; i < rules.size(); ++i) {
        r += "  [" + rules[i].selector + "] ->";
        for (size_t k = 0; k < rules[i].props.size(); ++k) r += (k ? ", " : " ") + rules[i].props[k].first;
        r += "\n";
    }
    r += "\nElements with Inline Styles: " + std::to_string(inline_count) + "\n";
    return keep(p->report, r);
}

LUNA_API void luna_tag_info(const char* tag_c, int* pt, int* bold, int* italic, int* is_block, int* is_inline) {
    std::string t = S(tag_c);
    int sz = 12, b = 0, it = 0;
    if (t == "h1") { sz = 32; b = 1; }
    else if (t == "h2") { sz = 24; b = 1; }
    else if (t == "h3") { sz = 19; b = 1; }
    else if (t == "h4") { sz = 16; b = 1; }
    else if (t == "h5") { sz = 13; b = 1; }
    else if (t == "h6") { sz = 11; b = 1; }
    else if (t == "address") { sz = 12; it = 1; }
    bool block = t == "h1" || t == "h2" || t == "h3" || t == "h4" || t == "h5" || t == "h6" ||
                 t == "p" || t == "li" || t == "address" || t == "video" || t == "img";
    bool inl = t == "span" || t == "a" || t == "b" || t == "i" || t == "strong" || t == "em";
    if (pt) *pt = sz;
    if (bold) *bold = b;
    if (italic) *italic = it;
    if (is_block) *is_block = block;
    if (is_inline) *is_inline = inl;
}

} /* extern "C" */
