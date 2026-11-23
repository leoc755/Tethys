/*
 * luna.h - Luna engine public C API (ported from Callisto 27.2.4 main.py + aur.py)
 *
 * Plain C ABI so it can be called from C# via P/Invoke.
 * All strings are UTF-8, NUL terminated.
 *
 * Threading: an engine and its pages are not thread safe. Use them from one
 * thread (the UI thread), or one engine per thread.
 *
 * Lifetime of returned `const char*`: valid until the next call that returns a
 * string from the same page (or engine, for engine functions). Copy it if you
 * need to keep it. Pointers inside LunaElement stay valid until the page is
 * loaded again or destroyed.
 */
#ifndef LUNA_H
#define LUNA_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
  #ifdef LUNA_BUILD
    #define LUNA_API __declspec(dllexport)
  #else
    #define LUNA_API __declspec(dllimport)
  #endif
#else
  #define LUNA_API __attribute__((visibility("default")))
#endif

#define LUNA_ENGINE_VERSION  "27.0.1"
#define LUNA_SHELL_VERSION   "27.2.4"
#define LUNA_CSS_VERSION     "1.1"

/* luna_page_load flags */
#define LUNA_LOAD_SAVE_HISTORY    1
#define LUNA_LOAD_CLEAR_FORWARD   2
#define LUNA_LOAD_IGNORE_SECURITY 4
#define LUNA_LOAD_DEFAULT         (LUNA_LOAD_SAVE_HISTORY | LUNA_LOAD_CLEAR_FORWARD)

/* luna_page_load return values */
#define LUNA_OK               0
#define LUNA_SECURITY_WARNING 1   /* the warning page is now loaded in the page */

typedef struct LunaEngine LunaEngine;
typedef struct LunaPage   LunaPage;

typedef struct LunaElement {
    const char* tag;      /* p h1..h6 li small span address a img video br hr */
    const char* text;     /* decoded text, "" for br/hr/img/video */
    const char* url;      /* a: raw href; img/video: absolute src; else "" */
    const char* id;
    const char* classes;  /* space separated */
    int bold;
    int italic;
    int has_inline_style;
    int is_text;          /* 1 for text runs, 0 for br/hr/img/video */
} LunaElement;

typedef void (*LunaStatusCallback)(int loading, const char* url, void* user);

/* ---- engine (CelesteEngine) ---- */
LUNA_API LunaEngine* luna_engine_create(void);
LUNA_API void        luna_engine_destroy(LunaEngine* e);
LUNA_API const char* luna_engine_version(LunaEngine* e);        /* Luna version */
LUNA_API const char* luna_engine_shell_version(LunaEngine* e);
LUNA_API const char* luna_engine_css_version(LunaEngine* e);    /* Stellae */
LUNA_API void        luna_engine_set_shell(LunaEngine* e, const char* name, const char* version);
LUNA_API const char* luna_engine_home_url(LunaEngine* e);
LUNA_API int         luna_engine_bookmark_count(LunaEngine* e);
LUNA_API const char* luna_engine_bookmark_name(LunaEngine* e, int i);
LUNA_API const char* luna_engine_bookmark_url(LunaEngine* e, int i);
LUNA_API const char* luna_engine_user_agent(LunaEngine* e);

/* nexia_request: GET with manual redirects (max 20). Returns the body as a
 * malloc'ed string (free with luna_free); *final_url_out (optional) receives a
 * malloc'ed final URL. Returns "LUNA_SECURITY_ERROR" on TLS/certificate failure
 * unless ignore_security is non-zero. Network errors come back as an
 * "Engine Alert" HTML snippet, like the Python engine. */
LUNA_API char* luna_request(LunaEngine* e, const char* url, int ignore_security, char** final_url_out);

/* Image bytes (1 second timeout), cached per URL inside the engine. Returns
 * NULL on failure. The pointer is owned by the engine. */
LUNA_API const unsigned char* luna_fetch_image(LunaEngine* e, const char* url, int* len_out);
LUNA_API void luna_engine_clear_image_cache(LunaEngine* e);

LUNA_API void luna_free(void* p);

/* ---- page (BrowserTab + parse_and_format + Aurelius) ---- */
LUNA_API LunaPage*   luna_page_create(LunaEngine* e);
LUNA_API void        luna_page_destroy(LunaPage* p);
LUNA_API void        luna_page_set_status_callback(LunaPage* p, LunaStatusCallback cb, void* user);

LUNA_API int         luna_page_load(LunaPage* p, const char* url, int flags);
LUNA_API void        luna_page_set_html(LunaPage* p, const char* base_url, const char* html);

LUNA_API const char* luna_page_title(LunaPage* p);       /* full title, "New Tab" if none */
LUNA_API const char* luna_page_base_url(LunaPage* p);    /* use for the address bar */
LUNA_API const char* luna_page_raw_source(LunaPage* p);
LUNA_API const char* luna_page_bg_color(LunaPage* p);    /* body background, "#ffffff" default */

LUNA_API int         luna_page_can_go_back(LunaPage* p);
LUNA_API int         luna_page_can_go_forward(LunaPage* p);
LUNA_API int         luna_page_back(LunaPage* p);
LUNA_API int         luna_page_forward(LunaPage* p);
LUNA_API int         luna_page_refresh(LunaPage* p);

/* Handles luna://proceed?url=... (security bypass) and relative links. */
LUNA_API int         luna_page_click_link(LunaPage* p, const char* href);
LUNA_API const char* luna_page_resolve_url(LunaPage* p, const char* rel);

LUNA_API int         luna_page_element_count(LunaPage* p);
LUNA_API int         luna_page_element_get(LunaPage* p, int index, LunaElement* out);
/* Aurelius lookup: inline > id > class > tag > body > *. Returns "undefined"
 * when nothing matches (text-align falls back to "left", font-family to "Roboto"). */
LUNA_API const char* luna_page_element_prop(LunaPage* p, int index, const char* prop);
LUNA_API const char* luna_page_style_report(LunaPage* p); /* the Style Stats text */

/* Default tag metrics from the Callisto painter. */
LUNA_API void luna_tag_info(const char* tag, int* point_size, int* bold, int* italic,
                            int* is_block, int* is_inline);

#ifdef __cplusplus
}
#endif
#endif
