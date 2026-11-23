#ifndef LUNA_INTERNAL_HPP
#define LUNA_INTERNAL_HPP

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace luna {

typedef std::vector<std::pair<std::string, std::string> > PropMap;

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
    const std::vector<Rule>& rules() const { return rules_; }

private:
    std::vector<Rule> rules_;
    std::unordered_map<std::string, size_t> index_;
    const std::string* lookup(const std::string& selector, const std::string& prop) const;
};

/* ---- htmlparse.cpp ---- */
struct Element {
    std::string tag, text, url, id, classes_joined;
    std::vector<std::string> classes;
    bool bold, italic, is_text;
    PropMap css;
    Element() : bold(false), italic(false), is_text(false) {}
};

void parse_and_format(const std::string& raw_html, const std::string& base_url,
                      Aurelius& aur, std::vector<Element>& out, std::string& bg_color);

/* ---- net.cpp ---- */
struct HttpResult {
    bool ok;
    bool security_error;
    int status;
    std::string body, location, error;
    HttpResult() : ok(false), security_error(false), status(0) {}
};

HttpResult http_get(const std::string& url, const std::string& user_agent,
                    bool ignore_security, int timeout_ms, size_t max_bytes);
std::string os_info();

} // namespace luna

#endif
