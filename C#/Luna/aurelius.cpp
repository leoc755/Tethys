/* Aurelius - Stellae 1.1 CSS engine (port of aur.py) */
#include "luna_internal.hpp"

namespace luna {

static void expand_hex(std::string& v) {
    /* Stellae hex expansion: #abc -> #aabbcc */
    if (v.size() == 4 && v[0] == '#') {
        std::string r = "#";
        for (size_t i = 1; i < 4; ++i) { r += v[i]; r += v[i]; }
        v = r;
    }
}

static void set_prop(PropMap& m, const std::string& name, const std::string& val) {
    for (size_t i = 0; i < m.size(); ++i)
        if (m[i].first == name) { m[i].second = val; return; }
    m.push_back(std::make_pair(name, val));
}

static const std::string* find_prop(const PropMap& m, const std::string& name) {
    for (size_t i = 0; i < m.size(); ++i)
        if (m[i].first == name) return &m[i].second;
    return NULL;
}

static void parse_declarations(const std::string& block, PropMap& dest) {
    size_t pos = 0;
    while (pos <= block.size()) {
        size_t semi = block.find(';', pos);
        if (semi == std::string::npos) semi = block.size();
        std::string rule = block.substr(pos, semi - pos);
        size_t colon = rule.find(':');
        if (colon != std::string::npos) {
            std::string name = lower(trim(rule.substr(0, colon)));
            std::string val = trim(rule.substr(colon + 1));
            expand_hex(val);
            set_prop(dest, name, val);
        }
        pos = semi + 1;
    }
}

void Aurelius::reset() {
    rules_.clear();
    index_.clear();
}

PropMap Aurelius::parse_inline(const std::string& style_text) {
    PropMap m;
    if (style_text.empty()) return m;
    parse_declarations(style_text, m);
    return m;
}

void Aurelius::parse(const std::string& css_in) {
    /* strip CSS comments (not in the Python version, harmless addition) */
    std::string css;
    css.reserve(css_in.size());
    for (size_t i = 0; i < css_in.size(); ++i) {
        if (css_in[i] == '/' && i + 1 < css_in.size() && css_in[i + 1] == '*') {
            size_t e = css_in.find("*/", i + 2);
            if (e == std::string::npos) break;
            i = e + 1;
        } else css += css_in[i];
    }

    size_t pos = 0;
    while (true) {
        size_t ob = css.find('{', pos);
        if (ob == std::string::npos) break;
        size_t cb = css.find('}', ob + 1);
        if (cb == std::string::npos) break;
        std::string group = css.substr(pos, ob - pos);
        std::string body = css.substr(ob + 1, cb - ob - 1);
        pos = cb + 1;
        if (trim(group).empty() || trim(body).empty()) continue;

        size_t sp = 0;
        while (sp <= group.size()) {
            size_t comma = group.find(',', sp);
            if (comma == std::string::npos) comma = group.size();
            std::string sel = lower(trim(group.substr(sp, comma - sp)));
            sp = comma + 1;
            if (sel.empty()) continue;
            std::unordered_map<std::string, size_t>::iterator it = index_.find(sel);
            size_t idx;
            if (it == index_.end()) {
                Rule r; r.selector = sel;
                rules_.push_back(r);
                idx = rules_.size() - 1;
                index_[sel] = idx;
            } else idx = it->second;
            parse_declarations(body, rules_[idx].props);
        }
    }
}

const std::string* Aurelius::lookup(const std::string& selector, const std::string& prop) const {
    std::unordered_map<std::string, size_t>::const_iterator it = index_.find(selector);
    if (it == index_.end()) return NULL;
    return find_prop(rules_[it->second].props, prop);
}

std::string Aurelius::explicit_prop(const std::string& tag, const std::vector<std::string>& classes,
                                    const std::string& element_id, const std::string& prop_in,
                                    const PropMap* inline_styles) const {
    /* Priority: Inline (1.1) > ID > Class > Tag > Body > Universal */
    std::string prop = lower(prop_in);
    const std::string* val = NULL;
#define OK(v) ((v) != NULL && !(v)->empty())

    if (inline_styles) val = find_prop(*inline_styles, prop);
    if (!OK(val) && !element_id.empty()) val = lookup("#" + lower(element_id), prop);
    if (!OK(val)) {
        for (size_t i = 0; i < classes.size(); ++i) {
            val = lookup("." + lower(classes[i]), prop);
            if (OK(val)) break;
        }
    }
    if (!OK(val)) val = lookup(lower(tag), prop);
    if (!OK(val)) val = lookup("body", prop);
    if (!OK(val)) val = lookup("*", prop);
    std::string r = OK(val) ? *val : std::string();
#undef OK
    return r;
}

std::string Aurelius::get_prop(const std::string& tag, const std::vector<std::string>& classes,
                               const std::string& element_id, const std::string& prop_in,
                               const PropMap* inline_styles) const {
    std::string r = explicit_prop(tag, classes, element_id, prop_in, inline_styles);
    if (!r.empty()) return r;

    std::string prop = lower(prop_in);
    if (prop == "text-align") return "left";
    if (prop == "font-family") return "Roboto";
    return "undefined";
}

static std::string join_props(const PropMap& props) {
    std::string s;
    for (size_t i = 0; i < props.size(); ++i) {
        if (i) s += "; ";
        s += props[i].first + ": " + props[i].second;
    }
    return s;
}

std::string Aurelius::inspect(const std::string& tag, const std::vector<std::string>& classes,
                              const std::string& element_id, const PropMap* inline_styles) const {
    std::vector<std::string> sels;
    sels.push_back("*");
    if (lower(tag) != "body") sels.push_back("body");
    sels.push_back(lower(tag));
    for (size_t i = 0; i < classes.size(); ++i) sels.push_back("." + lower(classes[i]));
    if (!element_id.empty()) sels.push_back("#" + lower(element_id));

    std::string out;
    for (size_t i = 0; i < sels.size(); ++i) {
        std::unordered_map<std::string, size_t>::const_iterator it = index_.find(sels[i]);
        if (it == index_.end() || rules_[it->second].props.empty()) continue;
        out += "  " + sels[i] + " { " + join_props(rules_[it->second].props) + " }\n";
    }
    if (inline_styles && !inline_styles->empty()) out += "  element.style { " + join_props(*inline_styles) + " }\n";
    if (out.empty()) out = "  (none)\n";
    return out;
}

} // namespace luna
