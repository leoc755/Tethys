/* Built-in luna:// pages (about, demohtml, settings, downloads, crashtest), settings file and download saving */
#include "luna_engine.hpp"
#include <cstdio>
#include <cstring>
#include <new>
#include <stdexcept>

using namespace luna;

namespace luna {

Query parse_query(const std::string& q) {
    Query out;
    size_t pos = 0;
    while (pos <= q.size()) {
        size_t amp = q.find('&', pos);
        if (amp == std::string::npos) amp = q.size();
        std::string item = q.substr(pos, amp - pos);
        pos = amp + 1;
        if (item.empty()) continue;
        size_t eq = item.find('=');
        if (eq == std::string::npos) out.push_back(std::make_pair(lower(url_decode(item, true)), std::string()));
        else out.push_back(std::make_pair(lower(url_decode(item.substr(0, eq), true)), url_decode(item.substr(eq + 1), true)));
    }
    return out;
}

/* Navigation that should be saved instead of shown. */
bool is_download(const std::string& content_type, const std::string& content_disposition) {
    if (find_ci(content_disposition, "attachment") != std::string::npos) return true;
    std::string ct = lower(trim(content_type.substr(0, content_type.find(';'))));
    if (ct.empty()) return false;
    if (starts_with(ct, "text/")) return false;
    if (ct.find("html") != std::string::npos || ct.find("xml") != std::string::npos ||
        ct.find("json") != std::string::npos || ct.find("javascript") != std::string::npos ||
        ct.find("ecmascript") != std::string::npos) return false;
    return true;   // zip, pdf, octet-stream, image, video, audio and so on
}

} // namespace luna

namespace {

std::string format_size(size_t n) {
    char b[48];
    if (n < 1024) std::snprintf(b, sizeof(b), "%u bytes", (unsigned)n);
    else if (n < 1024u * 1024u) std::snprintf(b, sizeof(b), "%.1f KB", n / 1024.0);
    else if (n < 1024u * 1024u * 1024u) std::snprintf(b, sizeof(b), "%.1f MB", n / (1024.0 * 1024.0));
    else std::snprintf(b, sizeof(b), "%.2f GB", n / (1024.0 * 1024.0 * 1024.0));
    return b;
}

const std::string* qget(const Query& q, const char* key) {
    for (size_t i = 0; i < q.size(); ++i) if (q[i].first == key) return &q[i].second;
    return NULL;
}

std::string nav() {
    return "<a href=\"luna://about\">[ about ]</a> <a href=\"luna://settings\">[ settings ]</a> "
           "<a href=\"luna://downloads\">[ downloads ]</a> <a href=\"luna://demohtml\">[ demohtml ]</a> "
           "<a href=\"luna://crashtest\">[ crashtest ]</a><br>\n<hr>\n";
}

/* ---- download file names ---- */

std::string clean_name(const std::string& in) {
    std::string s = in;
    size_t cut = s.find_last_of("/\\");
    if (cut != std::string::npos) s = s.substr(cut + 1);
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c < 32 || c == '<' || c == '>' || c == ':' || c == '"' || c == '|' || c == '?' || c == '*') continue;
        out += (char)c;
    }
    out = trim(out);
    while (!out.empty() && (out[out.size() - 1] == '.' || out[out.size() - 1] == ' ')) out.erase(out.size() - 1);
    if (out.size() > 150) out.erase(150);
    /* reserved Windows device names */
    std::string base = lower(out.substr(0, out.find('.')));
    static const char* reserved[] = { "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "lpt1", "lpt2", "lpt3" };
    for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); ++i)
        if (base == reserved[i]) { out = "_" + out; break; }
    return out.empty() ? std::string("download") : out;
}

std::string disposition_name(const std::string& disp) {
    size_t p = find_ci(disp, "filename*=");
    if (p != std::string::npos) {
        std::string v = trim(disp.substr(p + 10));
        size_t semi = v.find(';');
        if (semi != std::string::npos) v.erase(semi);
        size_t q = v.find("''");
        if (q != std::string::npos) v = v.substr(q + 2);
        return url_decode(trim(v), false);
    }
    p = find_ci(disp, "filename=");
    if (p == std::string::npos) return std::string();
    std::string v = trim(disp.substr(p + 9));
    if (!v.empty() && v[0] == '"') {
        size_t e = v.find('"', 1);
        return e == std::string::npos ? v.substr(1) : v.substr(1, e - 1);
    }
    size_t semi = v.find(';');
    if (semi != std::string::npos) v.erase(semi);
    return trim(v);
}

std::string download_name(const std::string& disp, const std::string& url) {
    std::string n = disposition_name(disp);
    if (n.empty()) {
        size_t se = url.find("://");
        size_t ps = se == std::string::npos ? 0 : url.find('/', se + 3);
        std::string path = ps == std::string::npos ? std::string() : url.substr(ps);
        size_t cut = path.find_first_of("?#");
        if (cut != std::string::npos) path.erase(cut);
        size_t sl = path.rfind('/');
        n = url_decode(sl == std::string::npos ? path : path.substr(sl + 1), false);
    }
    return clean_name(n);
}

std::string unique_path(const std::string& dir, const std::string& name) {
    std::string p = path_join(dir, name);
    if (!file_exists(p)) return p;
    size_t dot = name.rfind('.');
    std::string stem = (dot == std::string::npos || dot == 0) ? name : name.substr(0, dot);
    std::string ext = (dot == std::string::npos || dot == 0) ? std::string() : name.substr(dot);
    for (int i = 1; i < 10000; ++i) {
        p = path_join(dir, stem + " (" + std::to_string(i) + ")" + ext);
        if (!file_exists(p)) return p;
    }
    return p;
}

/* ---- settings page ---- */

std::string choice_row(const char* label, const char* key, bool on, const char* on_text, const char* off_text,
                       const char* on_value, const char* off_value) {
    std::string r = std::string("<span><b>") + label + ":</b> " + (on ? on_text : off_text) + "</span> ";
    r += std::string("<a href=\"luna://settings?") + key + "=" + (on ? off_value : on_value) + "\">[ " +
         (on ? "Change to " + std::string(off_text) : "Change to " + std::string(on_text)) + " ]</a><br>\n";
    return r;
}

void apply_setting_changes(LunaEngine& e, const Query& q) {
    bool changed = false;
    if (qget(q, "reset")) { e.settings = Settings(); changed = true; }
    const std::string* v;
    if ((v = qget(q, "images")) != NULL) { e.settings.images = (lower(*v) != "off"); changed = true; }
    if ((v = qget(q, "video")) != NULL) { e.settings.video = (lower(*v) != "off"); changed = true; }
    if ((v = qget(q, "certs")) != NULL) { e.settings.ignore_certs = (lower(*v) == "ignore"); changed = true; }
    if ((v = qget(q, "timeout")) != NULL) {
        int t = std::atoi(v->c_str());
        if (t == 5 || t == 10 || t == 30) { e.settings.timeout_s = t; changed = true; }
    }
    if (changed) e.save_settings();
}

std::string settings_page(const LunaEngine& e) {
    const Settings& s = e.settings;
    std::string h = "<title>Luna Settings</title>\n" + nav() + "<h1>Settings</h1>\n";
    h += e.settings_path.empty()
        ? "<span>Settings are kept for this session only.</span><br>\n"
        : "<span><b>Saved to:</b> " + escape_html(e.settings_path) + "</span><br>\n";
    h += "<span><b>Download folder:</b> " + escape_html(e.download_dir.empty() ? std::string("(not set)") : e.download_dir) + "</span><br>\n";
    h += "<hr>\n<h3>Content</h3>\n";
    h += choice_row("Images", "images", s.images, "On", "Off", "on", "off");
    h += choice_row("Video", "video", s.video, "On", "Off", "on", "off");
    h += "<span>Pages load again with the new setting when you refresh or navigate.</span><br>\n";
    h += "<hr>\n<h3>Network</h3>\n";
    h += "<span><b>Request timeout:</b> " + std::to_string(s.timeout_s) + " seconds</span> ";
    static const int opts[] = { 5, 10, 30 };
    for (size_t i = 0; i < 3; ++i)
        if (opts[i] != s.timeout_s)
            h += "<a href=\"luna://settings?timeout=" + std::to_string(opts[i]) + "\">[ " + std::to_string(opts[i]) + " s ]</a> ";
    h += "<br>\n";
    h += "<hr>\n<h3>Security</h3>\n";
    h += choice_row("Certificate errors", "certs", !s.ignore_certs, "Warn (recommended)", "Ignore (unsafe)", "warn", "ignore");
    if (s.ignore_certs)
        h += "<span style=\"color: #cc0000;\"><b>Unsafe:</b> invalid and self-signed certificates and old TLS 1.0/1.1 are accepted without a warning.</span><br>\n";
    h += "<hr>\n<a href=\"luna://settings?reset=1\">[ Reset to defaults ]</a>\n";
    return h;
}

/* ---- downloads page ---- */

std::string downloads_page(const LunaEngine& e) {
    std::string h = "<title>Downloads</title>\n" + nav() + "<h1>Downloads</h1>\n";
    h += "<span><b>Saved to:</b> " + escape_html(e.download_dir.empty() ? std::string("(not set)") : e.download_dir) + "</span><br>\n";
    if (e.downloads.empty()) {
        h += "<hr>\n<p>No downloads in this session.</p>\n";
        h += "<span>Links to files that are not web pages (zip, pdf, images, video and so on) are saved here instead of being shown.</span><br>\n";
        return h;
    }
    for (size_t i = e.downloads.size(); i-- > 0; ) {
        const Download& d = e.downloads[i];
        h += "<hr>\n<span><b>" + escape_html(d.name) + "</b></span><br>\n";
        if (d.ok) h += "<span>Done, " + format_size(d.bytes) + "</span><br>\n<span><b>Saved as:</b> " + escape_html(d.path) + "</span><br>\n";
        else h += "<span style=\"color: #cc0000;\"><b>Failed:</b> " + escape_html(d.error) + "</span><br>\n";
        h += "<span><b>From:</b> " + escape_html(d.url) + "</span><br>\n";
    }
    h += "<hr>\n<a href=\"luna://downloads?clear=1\">[ Clear list ]</a>\n";
    return h;
}

/* ---- crash test page ---- */

std::string crashtest_page() {
    return "<title>Crash Test</title>\n" + nav() + "<h1>Crash Test</h1>\n"
        "<span>Checks how Luna behaves when something goes wrong while it loads a page.</span><br>\n"
        "<hr>\n"
        "<h3>Engine exception</h3>\n"
        "<span>Throws a C++ exception inside Luna. The tab should show a \"Page Crashed\" page and keep working. Safe.</span><br>\n"
        "<a href=\"luna://crashtest?type=exception\">[ Run test ]</a><br>\n"
        "<hr>\n"
        "<h3>Out of memory</h3>\n"
        "<span>Asks for an impossible amount of memory. Same result as above. Safe.</span><br>\n"
        "<a href=\"luna://crashtest?type=memory\">[ Run test ]</a><br>\n"
        "<hr>\n"
        "<h3>Native crash</h3>\n"
        "<span style=\"color: #cc0000;\"><b>Closes Tethys.</b></span> <span>Writes to a null pointer. Luna runs inside the browser process, so every tab closes.</span><br>\n"
        "<a href=\"luna://crashtest?type=native\">[ Continue ]</a><br>\n";
}

std::string crash_confirm_page() {
    return "<title>Confirm Crash Test</title>\n" + nav() + "<h1 style=\"color: #cc0000;\">Crash Tethys?</h1>\n"
        "<p>This crashes the whole browser immediately. All tabs close and anything unsaved is lost.</p>\n"
        "<a href=\"luna://crashtest?type=native&confirm=1\">[ Crash Tethys now ]</a><br>\n"
        "<a href=\"luna://crashtest\">[ Cancel ]</a>\n";
}

const char* kDemoHtml = R"LUNA(<!DOCTYPE html>
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
<ul>
<li>Bullet one</li>
<li>Bullet two with a <a href="luna://about">link</a> in the middle and <b>bold</b> text after it</li>
<li>Bullet three
<ul>
<li>Nested bullet</li>
<li>Another nested bullet
<ul><li>Third level</li></ul>
</li>
</ul>
</li>
<li><a href="luna://settings">A list item that starts with a link</a></li>
</ul>
<p>An ordered list that starts at 3, with an item that jumps to 10:</p>
<ol start="3">
<li>Third</li>
<li>Fourth</li>
<li value="10">Tenth</li>
<li>Eleventh</li>
</ol>
<hr>

<h2>Tables</h2>
<table border="1" cellpadding="4" cellspacing="0">
<caption>A table with a border</caption>
<tr><th>Name</th><th>Type</th><th>Version</th></tr>
<tr><td>Luna</td><td>Engine</td><td align="right">27.3</td></tr>
<tr><td>Aurelius</td><td>CSS engine</td><td align="right">1.1</td></tr>
<tr><td colspan="2" align="center">This cell spans two columns</td><td>x</td></tr>
</table>
<p>Without a border, with cell colors and a nested table:</p>
<table cellpadding="6">
<tr><td style="background-color:#e8f0fe">Blue cell</td><td style="background-color:#fce8e6">Red cell</td></tr>
<tr><td colspan="2"><table border="1"><tr><td>Nested A</td><td>Nested B</td></tr></table></td></tr>
</table>
<hr>

<h2>Buttons</h2>
<p>Buttons: <button>Plain button</button> <input type="button" value="Input button"> <button disabled>Disabled</button> <a href="luna://about"><button>Button inside a link</button></a></p>
<p class="note">Buttons are drawn, and a button inside a link follows the link. Forms and scripts are not supported.</p>
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
<p>A WebP image, decoded by the bundled libwebp:</p>
<img src="data:image/webp;base64,UklGRvAAAABXRUJQVlA4TOQAAAAvH8AHAM2VIaL/sQAIwn+4hgFRkLYBU/8auyOl/+EB8HDURpIkeQ8Ig2mfC3bx9W+6s6q0BBi1keTIv8NyHBKAw3vUZjYvo0iSpOTM7P4wiiI04Agh/PaKCbiuD////W/1uqu9+bdfAG9wZtNwtKmwXRTAoR/OpbNF0cYkN2ENLAqASRoAm4a4Axy6QZiBTYUhJjlkUbCoGQBs2oRJAsChD1QbDMKGSXbZVNizqKaUTWuxKAJw6B2NFZOUMclamUVVDEIFbFpmU6HOWVrUu0gsiqnmxCRNdd8+3+P4787hGL3XC/M=" width="64" height="64" alt="WebP test image">
<p class="note">Images load in the background; a box with the alt text shows while loading or when loading fails.</p>
<hr>

<h2>Video</h2>
<p>Played by the Windows media engine with the NyxPlayer controls. Needs an internet connection and an installed codec (MP4 with H.264 works on Windows 10).</p>
<video width="480" height="270"><source src="https://www.w3schools.com/html/mov_bbb.mp4">Your browser does not support the video tag.</video>
</body>
</html>
)LUNA";

} // namespace

/* ---------------------------------------------------------------- engine methods */

void LunaEngine::load_settings() {
    if (settings_path.empty()) return;
    std::string text;
    if (!read_file(settings_path, text)) return;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = trim(text.substr(pos, nl - pos));
        pos = nl + 1;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = lower(trim(line.substr(0, eq))), v = trim(line.substr(eq + 1));
        if (k == "images") settings.images = (v != "0");
        else if (k == "video") settings.video = (v != "0");
        else if (k == "ignore_certs") settings.ignore_certs = (v == "1");
        else if (k == "timeout") { int t = std::atoi(v.c_str()); if (t >= 1 && t <= 120) settings.timeout_s = t; }
    }
}

void LunaEngine::save_settings() const {
    if (settings_path.empty()) return;
    size_t cut = settings_path.find_last_of("/\\");
    if (cut != std::string::npos) make_dirs(settings_path.substr(0, cut));
    std::string text = std::string("images=") + (settings.images ? "1" : "0") + "\n" +
                       "video=" + (settings.video ? "1" : "0") + "\n" +
                       "ignore_certs=" + (settings.ignore_certs ? "1" : "0") + "\n" +
                       "timeout=" + std::to_string(settings.timeout_s) + "\n";
    write_file(settings_path, text);
}

bool LunaEngine::save_download(const std::string& url, const std::string& data, const std::string& ctype,
                               const std::string& disp, bool truncated) {
    (void)ctype;
    Download d;
    d.url = url;
    d.name = download_name(disp, url);
    d.bytes = data.size();
    if (truncated) {
        d.error = "the file is larger than the " + format_size(kMaxBody) + " limit";
    } else if (download_dir.empty() || !make_dirs(download_dir)) {
        d.error = "the download folder could not be created";
    } else {
        std::string path = unique_path(download_dir, d.name);
        if (write_file(path, data)) { d.ok = true; d.path = path; }
        else d.error = "the file could not be written";
    }
    downloads.push_back(d);
    return d.ok;
}

std::string LunaEngine::crash_html(const std::string& url, const std::string& what) const {
    return "<title>Page Crashed</title>\n"
           "<h1 style=\"color: #cc0000;\">This page crashed</h1>\n"
           "<span>Luna recovered from an internal error. The tab is still usable.</span><br>\n"
           "<hr>\n"
           "<span><b>Address:</b> " + escape_html(url) + "</span><br>\n"
           "<span><b>Error:</b> " + escape_html(what) + "</span><br>\n"
           "<span><b>Engine:</b> Luna " + escape_html(version) + "</span><br>\n"
           "<hr>\n"
           "<a href=\"luna://about\">[ About Luna ]</a> <a href=\"luna://crashtest\">[ Crash test ]</a>\n";
}

bool LunaEngine::builtin_page(const std::string& name, const Query& q, std::string& html) {
    if (name == "about") {
        html =
            "<title>About Luna</title>\n" + nav() +
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
            "<a href=\"" + escape_html(home_url) + "\">Open the home page</a>\n";
        return true;
    }
    if (name == "demohtml") { html = kDemoHtml; return true; }
    if (name == "settings") { apply_setting_changes(*this, q); html = settings_page(*this); return true; }
    if (name == "downloads") {
        if (qget(q, "clear")) downloads.clear();
        html = downloads_page(*this);
        return true;
    }
    if (name == "crashtest") {
        const std::string* type = qget(q, "type");
        if (!type) { html = crashtest_page(); return true; }
        if (*type == "exception") throw std::runtime_error("Deliberate test exception from luna://crashtest");
        if (*type == "memory") {
            volatile size_t huge = ((size_t)-1) / 2;
            char* p = new char[huge];                 /* throws std::bad_alloc */
            p[0] = 0;
            delete[] p;
            throw std::runtime_error("The impossible allocation did not fail");
        }
        if (*type == "native") {
            if (qget(q, "confirm")) {
                volatile int* bad = NULL;
                *bad = 42;                             /* access violation: closes the process */
                __builtin_trap();
            }
            html = crash_confirm_page();
            return true;
        }
        html = crashtest_page();
        return true;
    }
    return false;
}
