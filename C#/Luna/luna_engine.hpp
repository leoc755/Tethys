#ifndef LUNA_ENGINE_HPP
#define LUNA_ENGINE_HPP

#include "luna.h"
#include "luna_internal.hpp"

namespace luna {

typedef std::vector<std::pair<std::string, std::string> > Query;

/* luna://settings */
struct Settings {
    bool images, video, ignore_certs;
    int timeout_s;
    Settings() : images(true), video(true), ignore_certs(false), timeout_s(5) {}
};

/* luna://downloads */
struct Download {
    std::string url, name, path, error;
    size_t bytes;
    bool ok;
    Download() : bytes(0), ok(false) {}
};

struct Fetched {
    std::string body, ctype, disp;
    bool security, download, truncated;
    int status;
    Fetched() : security(false), download(false), truncated(false), status(0) {}
};

const size_t kMaxBody = 100u * 1024u * 1024u;   /* page and download size limit */

Query parse_query(const std::string& q);

} // namespace luna

struct LunaEngine {
    std::string version, shell_name, shell_ver, cssver, home_url, ua, scratch;
    std::vector<std::pair<std::string, std::string> > bookmarks;
    struct Img { bool ok; std::vector<unsigned char> data; };
    std::unordered_map<std::string, Img> image_cache;

    luna::Settings settings;
    std::string settings_path, download_dir;
    std::vector<luna::Download> downloads;

    LunaEngine();

    std::string get_ua() const;
    static bool is_redirect(int s);

    /* GET with manual redirects; sets final_url. */
    luna::Fetched fetch(const std::string& start_url, int redirects_left, bool ignore_security, std::string& final_url);
    std::string nexia_request(const std::string& start_url, int redirects_left, bool ignore_security, std::string& final_url);

    /* settings.cpp-style helpers (pages.cpp) */
    void load_settings();
    void save_settings() const;
    bool save_download(const std::string& url, const std::string& data, const std::string& ctype,
                       const std::string& disp, bool truncated);

    /* built-in luna:// pages; may change settings, clear downloads or throw (crashtest) */
    bool builtin_page(const std::string& name, const luna::Query& q, std::string& html);
    std::string crash_html(const std::string& url, const std::string& what) const;
};

#endif
