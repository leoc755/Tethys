#ifndef LUNA_INTERNAL_HPP
#define LUNA_INTERNAL_HPP

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace luna {

typedef std::vector<std::pair<std::string, std::string> > PropMap;

enum { kLayoutNewline = 1, kLayoutInline = 2, kLayoutDisabled = 4, kLayoutContinues = 8 };   /* same values as LUNA_LAYOUT_* */

/* ---- util.cpp ---- */
std::string lower(const std::string& s);
std::string trim(const std::string& s);
bool starts_with(const std::string& s, const char* prefix);
size_t find_ci(const std::string& hay, const std::string& needle, size_t from = 0);
std::vector<std::string> split_ws(const std::string& s);
std::string urljoin(const std::string& base, const std::string& ref);
std::string sanitize_utf8(const std::string& in);
std::string decode_entities(const std::string& in);
std::string escape_html(const std::string& s);
std::string url_decode(const std::string& s, bool plus_as_space);
std::string escape_url(const std::string& u);   /* spaces, non-ASCII and unsafe bytes -> %XX; tabs/newlines dropped */

/* ---- aurelius.cpp (Stellae 1.1) ---- */
class Aurelius {
public:
    struct Rule {
        std::string selector;
        PropMap props;
    };

    void reset();
    void parse(const std::string& css_text);
    static PropMap parse_inline(const std::string& style_text);
    std::string get_prop(const std::string& tag, const std::vector<std::string>& classes,
                         const std::string& element_id, const std::string& prop,
                         const PropMap* inline_styles = NULL) const;
    /* same lookup, "" when nothing sets the property (no fallbacks) */
    std::string explicit_prop(const std::string& tag, const std::vector<std::string>& classes,
                              const std::string& element_id, const std::string& prop,
                              const PropMap* inline_styles = NULL) const;
    const std::vector<Rule>& rules() const { return rules_; }
    /* the rules that apply to an element, lowest priority first, one per line (for inspect element) */
    std::string inspect(const std::string& tag, const std::vector<std::string>& classes,
                        const std::string& element_id, const PropMap* inline_styles) const;

private:
    std::vector<Rule> rules_;
    std::unordered_map<std::string, size_t> index_;
    const std::string* lookup(const std::string& selector, const std::string& prop) const;
};

/* ---- htmlparse.cpp ---- */
struct Element {
    std::string tag, text, url, id, link, classes_joined;
    std::vector<std::string> classes;
    bool bold, italic, is_text;
    PropMap css;
    int width, height;   /* width/height attributes of img and video, 0 = not given */
    int list_depth, list_marker, list_number, layout;   /* see LunaElement */
    PropMap attrs;                                      /* raw attributes of table markers and buttons */
    Element() : bold(false), italic(false), is_text(false), width(0), height(0),
                list_depth(0), list_marker(0), list_number(0), layout(0) {}
};

void parse_and_format(const std::string& raw_html, const std::string& base_url,
                      Aurelius& aur, std::vector<Element>& out, std::string& bg_color);

/* ---- net.cpp ---- */
struct HttpResult {
    bool ok;
    bool security_error;
    bool truncated;                 /* body was cut at max_bytes */
    int status;
    std::string body, location, error, content_type, content_disposition;
    HttpResult() : ok(false), security_error(false), truncated(false), status(0) {}
};

HttpResult http_get(const std::string& url, const std::string& user_agent,
                    bool ignore_security, int timeout_ms, size_t max_bytes);
std::string os_info();

/* platform helpers (net.cpp); paths are UTF-8 */
std::string default_settings_path();     /* %APPDATA%\\Tethys\\luna.ini, "" if unknown */
std::string default_download_dir();      /* %USERPROFILE%\\Downloads */
char path_sep();
std::string path_join(const std::string& dir, const std::string& name);
bool make_dirs(const std::string& dir);
bool file_exists(const std::string& path);
bool write_file(const std::string& path, const std::string& data);
bool read_file(const std::string& path, std::string& out);

/* pages.cpp */
bool is_download(const std::string& content_type, const std::string& content_disposition);

} // namespace luna

#endif
