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
        std::string s = "Mozilla/5.0 (" + os_info() + ") Nyxora/1.14 Nex/27.1.2 ";
        /* Callisto token goes before the shell token (skipped when the shell is Callisto itself) */
        if (shell_name != "Callisto") s += "Callisto/" LUNA_SHELL_VERSION " ";
        s += shell_name + "/" + shell_ver + " (Luna, rv:" + version + ")";
        return s;
    }

    /* Built-in luna:// pages. name is lower case without slashes. */
    bool builtin_page(const std::string& name, std::string& html) const {
        if (name == "about") {
            html =
                "<title>About Luna</title>\n"
                "<h1>Luna</h1>\n"
                "<h3 style=\"color: #666666;\">Rendering engine of the Tethys browser</h3>\n"
                "<hr>\n"
                "<span><b>Engine:</b> Luna " + escape_html(version) + "</span><br>\n"
                "<span><b>CSS engine:</b> Aurelius (Stellae " + escape_html(cssver) + ")</span><br>\n"
                "<span><b>Shell:</b> " + escape_html(shell_name) + " " + escape_html(shell_ver) + "</span><br>\n"
                "<span><b>Network:</b> WinHTTP, TLS 1.2 and 1.3, system certificate store</span><br>\n"
                "<span><b>Origin:</b> ported from the Python Callisto 27.2.4 engine to C and C++</span><br>\n"
                "<hr>\n"
                "<span><b>User-Agent</b></span><br>\n"
                "<p>" + escape_html(get_ua()) + "</p>\n"
                "<hr>\n"
                "<a href=\"luna://demohtml\">Open the demo page (luna://demohtml)</a><br>\n"
                "<a href=\"" + escape_html(home_url) + "\">Open the home page</a>\n";
            return true;
        }
        if (name == "demohtml") {
            html = R"LUNA(<!DOCTYPE html>
<html>
<head>
<title>Luna Demo</title>
<style>
body { background-color: #f4f4f8; }
h1 { color: #223366; text-align: center; }
h2, h3 { color: #36c; }
address { color: #555555; }
.ok { color: #008800; }
.note { color: #aa6600; }
.right { text-align: right; }
#special { color: #cc0000; text-align: center; }
</style>
</head>
<body>
<h1>Luna Demo Page</h1>
<p style="text-align: center;">Everything on this page is rendered by the Luna engine.</p>
<hr>

<h2>Headings (h2 and h3 are blue from a group selector)</h2>
<h1>Heading 1</h1>
<h2>Heading 2</h2>
<h3>Heading 3</h3>
<h4>Heading 4</h4>
<h5>Heading 5</h5>
<h6>Heading 6</h6>
<hr>

<h2>Text styles</h2>
<span>This is</span> <b>bold</b> <span>and this is</span> <i>italic</i> <span>and this is</span> <b><i>both</i></b><br>
<p>A paragraph is a block, so it starts on its own line.</p>
<address>Address text is italic: Luna Engine, part of Tethys</address>
<hr>

<h2>CSS: class, id, inline and alignment</h2>
<p class="ok">Class selector: green (.ok)</p>
<p class="note">Class selector: orange (.note)</p>
<p id="special">ID selector: red and centered (#special)</p>
<p style="color:#36c;">Inline style with a short hex color (#36c becomes #3366cc)</p>
<p style="color:#00f;" id="special">Inline beats id: this one is blue, centered by the id</p>
<p class="right">Right aligned with .right</p>
<hr>

<h2>Lists</h2>
<li>First item</li>
<li>Second item</li>
<li>Third item</li>
<hr>

<h2>Entities</h2>
<p>&copy; &euro; &mdash; &lt;tag&gt; &amp; &#65; &#x42; &nbsp;(nbsp)</p>
<hr>

<h2>Links</h2>
<a href="luna://about">About Luna (luna://about)</a><br>
<a href="https://example.com/">example.com over HTTPS</a><br>
<a href="http://neverssl.com/">neverssl.com over plain HTTP</a>
<hr>

<h2>Images</h2>
<p>A PNG from a data: URI, scaled to 128 by 128 with the width and height attributes:</p>
<img src="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAIAAAD8GO2jAAAFM0lEQVR42r3WkRYGKQBA4X2EgYWhPYNhGCyEYTgYhoPhYBgOhp1L4eBgGIZhj7G8v++ec/3T+8eff/39n/bH/wRsG/vGsSE25Iba0Btmw26cG27Db1wbYePeiBtp49nIG2Wjbrwb30bb6BtjY26sjX8DO/vOsSN25I7a0Ttmx+6cO27H71w7YefeiTtp59nJO2Wn7rw7307b6TtjZ+6s/Qc42A+OA3EgD9SBPjAH9uA8cAf+4DoIB/dBPEgHz0E+KAf14D34DtpBPxgH82AdP4BgFxwCIZACJdACI7CCU+AEXnAJguAWREESPIIsKIIqeAWfoAm6YAimYIkfQLJLDomQSImSaImRWMkpcRIvuSRBckuiJEkeSZYUSZW8kk/SJF0yJFOy5A+g2BWHQiikQim0wiis4lQ4hVdciqC4FVGRFI8iK4qiKl7Fp2iKrhiKqVjqB9DsmkMjNFKjNFpjNFZzapzGay5N0NyaqEmaR5M1RVM1r+bTNE3XDM3ULP0DGHbDYRAGaVAGbTAGazgNzuANlyEYbkM0JMNjyIZiqIbX8BmaoRuGYRqW+QEsu+WwCIu0KIu2GIu1nBZn8ZbLEiy3JVqS5bFkS7FUy2v5LM3SLcMyLcv+ACf7yXEiTuSJOtEn5sSenCfuxJ9cJ+HkPokn6eQ5ySflpJ68J99JO+kn42SerPMHcOyOwyEc0qEc2mEc1nE6nMM7Lkdw3I7oSI7HkR3FUR2v43M0R3cMx3Qs9wN4ds/hER7pUR7tMR7rOT3O4z2XJ3huT/Qkz+PJnuKpntfzeZqne4Znepb/AS72i+NCXMgLdaEvzIW9OC/chb+4LsLFfREv0sVzkS/KRb14L76LdtEvxsW8WNcPENgDR0AEZEAFdMAEbOAMuIAPXIEQuAMxkAJPIAdKoAbewBdogR4YgRlY4Qe42W+OG3Ejb9SNvjE39ua8cTf+5roJN/dNvEk3z02+KTf15r35btpNvxk382bdP0BkjxwREZERFdERE7GRM+IiPnJFQuSOxEiKPJEcKZEaeSNfpEV6ZERmZMUfILEnjoRIyIRK6IRJ2MSZcAmfuBIhcSdiIiWeRE6URE28iS/REj0xEjOx0g/wsD8cD+JBPqgH/WAe7MP54B78w/UQHu6H+JAenof8UB7qw/vwPbSH/jAe5sN6foDMnjkyIiMzKqMzJmMzZ8ZlfObKhMydiZmUeTI5UzI182a+TMv0zMjMzMo/QGEvHAVRkAVV0AVTsIWz4Aq+cBVC4S7EQio8hVwohVp4C1+hFXphFGZhlR+gsleOiqjIiqroiqnYyllxFV+5KqFyV2IlVZ5KrpRKrbyVr9IqvTIqs7LqD/Cyvxwv4kW+qBf9Yl7sy/niXvzL9RJe7pf4kl6el/xSXurL+/K9tJf+Ml7my3p/gI/94/gQH/JDfegP82E/zg/34T+uj/Bxf8SP9PF85I/yUT/ej++jffSP8TE/1vcDNPbG0RAN2VAN3TAN2zgbruEbVyM07kZspMbTyI3SqI238TVaozdGYzZW+wE6e+foiI7sqI7umI7tnB3X8Z2rEzp3J3ZS5+nkTunUztv5Oq3TO6MzO6v/AIN9cAzEQA7UQA/MwA7OgRv4wTUIg3sQB2nwDPKgDOrgHXyDNuiDMZiDNX6AyT45JmIiJ2qiJ2ZiJ+fETfzkmoTJPYmTNHkmeVImdfJOvkmb9MmYzMmaP8BiXxwLsZALtdALs7CLc+EWfnEtwuJexEVaPIu8KIu6eBffoi36YizmYi3+3/n97/oHng5njCsWWKQAAAAASUVORK5CYII=" width="128" height="128" alt="Generated test image">
<p>The same image at its natural size, inside a link to luna://about:</p>
<a href="luna://about"><img src="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAIAAAD8GO2jAAAFM0lEQVR42r3WkRYGKQBA4X2EgYWhPYNhGCyEYTgYhoPhYBgOhp1L4eBgGIZhj7G8v++ec/3T+8eff/39n/bH/wRsG/vGsSE25Iba0Btmw26cG27Db1wbYePeiBtp49nIG2Wjbrwb30bb6BtjY26sjX8DO/vOsSN25I7a0Ttmx+6cO27H71w7YefeiTtp59nJO2Wn7rw7307b6TtjZ+6s/Qc42A+OA3EgD9SBPjAH9uA8cAf+4DoIB/dBPEgHz0E+KAf14D34DtpBPxgH82AdP4BgFxwCIZACJdACI7CCU+AEXnAJguAWREESPIIsKIIqeAWfoAm6YAimYIkfQLJLDomQSImSaImRWMkpcRIvuSRBckuiJEkeSZYUSZW8kk/SJF0yJFOy5A+g2BWHQiikQim0wiis4lQ4hVdciqC4FVGRFI8iK4qiKl7Fp2iKrhiKqVjqB9DsmkMjNFKjNFpjNFZzapzGay5N0NyaqEmaR5M1RVM1r+bTNE3XDM3ULP0DGHbDYRAGaVAGbTAGazgNzuANlyEYbkM0JMNjyIZiqIbX8BmaoRuGYRqW+QEsu+WwCIu0KIu2GIu1nBZn8ZbLEiy3JVqS5bFkS7FUy2v5LM3SLcMyLcv+ACf7yXEiTuSJOtEn5sSenCfuxJ9cJ+HkPokn6eQ5ySflpJ68J99JO+kn42SerPMHcOyOwyEc0qEc2mEc1nE6nMM7Lkdw3I7oSI7HkR3FUR2v43M0R3cMx3Qs9wN4ds/hER7pUR7tMR7rOT3O4z2XJ3huT/Qkz+PJnuKpntfzeZqne4Znepb/AS72i+NCXMgLdaEvzIW9OC/chb+4LsLFfREv0sVzkS/KRb14L76LdtEvxsW8WNcPENgDR0AEZEAFdMAEbOAMuIAPXIEQuAMxkAJPIAdKoAbewBdogR4YgRlY4Qe42W+OG3Ejb9SNvjE39ua8cTf+5roJN/dNvEk3z02+KTf15r35btpNvxk382bdP0BkjxwREZERFdERE7GRM+IiPnJFQuSOxEiKPJEcKZEaeSNfpEV6ZERmZMUfILEnjoRIyIRK6IRJ2MSZcAmfuBIhcSdiIiWeRE6URE28iS/REj0xEjOx0g/wsD8cD+JBPqgH/WAe7MP54B78w/UQHu6H+JAenof8UB7qw/vwPbSH/jAe5sN6foDMnjkyIiMzKqMzJmMzZ8ZlfObKhMydiZmUeTI5UzI182a+TMv0zMjMzMo/QGEvHAVRkAVV0AVTsIWz4Aq+cBVC4S7EQio8hVwohVp4C1+hFXphFGZhlR+gsleOiqjIiqroiqnYyllxFV+5KqFyV2IlVZ5KrpRKrbyVr9IqvTIqs7LqD/Cyvxwv4kW+qBf9Yl7sy/niXvzL9RJe7pf4kl6el/xSXurL+/K9tJf+Ml7my3p/gI/94/gQH/JDfegP82E/zg/34T+uj/Bxf8SP9PF85I/yUT/ej++jffSP8TE/1vcDNPbG0RAN2VAN3TAN2zgbruEbVyM07kZspMbTyI3SqI238TVaozdGYzZW+wE6e+foiI7sqI7umI7tnB3X8Z2rEzp3J3ZS5+nkTunUztv5Oq3TO6MzO6v/AIN9cAzEQA7UQA/MwA7OgRv4wTUIg3sQB2nwDPKgDOrgHXyDNuiDMZiDNX6AyT45JmIiJ2qiJ2ZiJ+fETfzkmoTJPYmTNHkmeVImdfJOvkmb9MmYzMmaP8BiXxwLsZALtdALs7CLc+EWfnEtwuJexEVaPIu8KIu6eBffoi36YizmYi3+3/n97/oHng5njCsWWKQAAAAASUVORK5CYII=" alt="Link image"></a>
<p class="note">Images load in the background; a box with the alt text shows while loading or when loading fails.</p>
<hr>

<h2>Video</h2>
<p>Played by the Windows media engine with the NyxPlayer controls. Needs an internet connection and an installed codec (MP4 with H.264 works on Windows 10).</p>
<video width="480" height="270"><source src="https://www.w3schools.com/html/mov_bbb.mp4"></video>
</body>
</html>
)LUNA";
            return true;
        }
        return false;
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

        /* built-in pages: luna://about, luna://demohtml */
        if (starts_with(url, "luna://")) {
            std::string name = lower(url.substr(7));
            size_t cut = name.find_first_of("/?#");
            if (cut != std::string::npos) name.erase(cut);
            std::string html;
            if (!eng->builtin_page(name, html))
                html = "<title>Not Found</title>\n<h1>Not Found</h1>\n<p>There is no built-in page named "
                       + escape_html(name) + ".</p>\n<a href=\"luna://about\">luna://about</a>\n";
            std::string final_url = "luna://" + name;
            if (save_history && !base_url.empty() && base_url != final_url) history.push_back(base_url);
            if (clear_forward) forward_stack.clear();
            apply_source(html, final_url);
            status(0, "");
            return LUNA_OK;
        }

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
        /* follow redirects like the page loader */
        std::string cur = url;
        for (int hop = 0; hop < 10; ++hop) {
            HttpResult r = http_get(cur, e->get_ua(), false, 5000, 32u * 1024u * 1024u);
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
    out->link = el.link.c_str();
    out->bold = el.bold; out->italic = el.italic;
    out->has_inline_style = !el.css.empty();
    out->is_text = el.is_text;
    out->width = el.width; out->height = el.height;
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
