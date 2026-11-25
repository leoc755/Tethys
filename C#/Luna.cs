// Luna.cs - P/Invoke wrapper for luna.dll (targets .NET Framework 4.7.2, C# 7.3)
// Put luna.dll next to Tethys.exe. Build luna.dll with the same bitness as the C# project
// (x86 or x64; AnyCPU with "Prefer 32-bit" runs as x86).
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace Tethys.Luna
{
    internal static class Native
    {
        const string Dll = "luna.dll";
        const CallingConvention CC = CallingConvention.Cdecl;

        [UnmanagedFunctionPointer(CC)]
        public delegate void StatusCallback(int loading, IntPtr url, IntPtr user);

        [StructLayout(LayoutKind.Sequential)]
        public struct Element
        {
            public IntPtr tag, text, url, id, classes, link;
            public int bold, italic, hasInlineStyle, isText, width, height, listDepth, listMarker, listNumber, layout;
        }

        [DllImport(Dll, CallingConvention = CC)] public static extern IntPtr luna_engine_create();
        [DllImport(Dll, CallingConvention = CC)] public static extern void luna_engine_destroy(IntPtr e);
        [DllImport(Dll, CallingConvention = CC)] public static extern IntPtr luna_engine_version(IntPtr e);
        [DllImport(Dll, CallingConvention = CC)] public static extern IntPtr luna_engine_shell_version(IntPtr e);
        [DllImport(Dll, CallingConvention = CC)] public static extern IntPtr luna_engine_css_version(IntPtr e);
        [DllImport(Dll, CallingConvention = CC)] public static extern void luna_engine_set_shell(IntPtr e,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string name, [MarshalAs(UnmanagedType.LPUTF8Str)] string version);
        [DllImport(Dll, CallingConvention = CC)] public static extern IntPtr luna_engine_home_url(IntPtr e);
        [DllImport(Dll, CallingConvention = CC)] public static extern int luna_engine_bookmark_count(IntPtr e);
        [DllImport(Dll, CallingConvention = CC)] public static extern IntPtr luna_engine_bookmark_name(IntPtr e, int i);
        [DllImport(Dll, CallingConvention = CC)] public static extern IntPtr luna_engine_bookmark_url(IntPtr e, int i);
        [DllImport(Dll, CallingConvention = CC)] public static extern IntPtr luna_engine_user_agent(IntPtr e);
        [DllImport(Dll, CallingConvention = CC)] public static extern void luna_engine_set_settings_path(IntPtr e,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string path);
        [DllImport(Dll, CallingConvention = CC)] public static extern void luna_engine_set_download_dir(IntPtr e,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string dir);
        [DllImport(Dll, CallingConvention = CC)] public static extern IntPtr luna_request(IntPtr e,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string url, int ignoreSecurity, out IntPtr finalUrl);
        [DllImport(Dll, CallingConvention = CC)] public static extern IntPtr luna_fetch_image(IntPtr e,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string url, out int len);
        [DllImport(Dll, CallingConvention = CC)] public static extern void luna_engine_clear_image_cache(IntPtr e);
        [DllImport(Dll, CallingConvention = CC)] public static extern void luna_free(IntPtr p);

        [DllImport(Dll, CallingConvention = CC)] public static extern IntPtr luna_page_create(IntPtr e);
        [DllImport(Dll, CallingConvention = CC)] public static extern void luna_page_destroy(IntPtr p);
        [DllImport(Dll, CallingConvention = CC)] public static extern void luna_page_set_status_callback(IntPtr p, StatusCallback cb, IntPtr user);
        [DllImport(Dll, CallingConvention = CC)] public static extern int luna_page_load(IntPtr p,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string url, int flags);
        [DllImport(Dll, CallingConvention = CC)] public static extern void luna_page_set_html(IntPtr p,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string baseUrl, [MarshalAs(UnmanagedType.LPUTF8Str)] string html);
        [DllImport(Dll, CallingConvention = CC)] public static extern IntPtr luna_page_title(IntPtr p);
        [DllImport(Dll, CallingConvention = CC)] public static extern IntPtr luna_page_base_url(IntPtr p);
        [DllImport(Dll, CallingConvention = CC)] public static extern IntPtr luna_page_raw_source(IntPtr p);
        [DllImport(Dll, CallingConvention = CC)] public static extern IntPtr luna_page_bg_color(IntPtr p);
        [DllImport(Dll, CallingConvention = CC)] public static extern int luna_page_can_go_back(IntPtr p);
        [DllImport(Dll, CallingConvention = CC)] public static extern int luna_page_can_go_forward(IntPtr p);
        [DllImport(Dll, CallingConvention = CC)] public static extern int luna_page_back(IntPtr p);
        [DllImport(Dll, CallingConvention = CC)] public static extern int luna_page_forward(IntPtr p);
        [DllImport(Dll, CallingConvention = CC)] public static extern int luna_page_refresh(IntPtr p);
        [DllImport(Dll, CallingConvention = CC)] public static extern int luna_page_click_link(IntPtr p,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string href);
        [DllImport(Dll, CallingConvention = CC)] public static extern IntPtr luna_page_resolve_url(IntPtr p,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string rel);
        [DllImport(Dll, CallingConvention = CC)] public static extern int luna_page_element_count(IntPtr p);
        [DllImport(Dll, CallingConvention = CC)] public static extern int luna_page_element_get(IntPtr p, int index, out Element el);
        [DllImport(Dll, CallingConvention = CC)] public static extern IntPtr luna_page_element_prop(IntPtr p, int index,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string prop);
        [DllImport(Dll, CallingConvention = CC)] public static extern IntPtr luna_page_style_report(IntPtr p);
        [DllImport(Dll, CallingConvention = CC)] public static extern IntPtr luna_page_element_inspect(IntPtr p, int index);
        [DllImport(Dll, CallingConvention = CC)] public static extern IntPtr luna_page_element_attr(IntPtr p, int index,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string name);
        [DllImport(Dll, CallingConvention = CC)] public static extern IntPtr luna_page_element_css(IntPtr p, int index,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string prop);
        [DllImport(Dll, CallingConvention = CC)] public static extern IntPtr luna_decode_webp(byte[] data, int len, out int width, out int height);
        [DllImport(Dll, CallingConvention = CC)] public static extern void luna_tag_info(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string tag, out int pt, out int bold, out int italic, out int isBlock, out int isInline);

        public static string Str(IntPtr p)
        {
            if (p == IntPtr.Zero) return "";
            int n = 0;
            while (Marshal.ReadByte(p, n) != 0) n++;
            if (n == 0) return "";
            byte[] b = new byte[n];
            Marshal.Copy(p, b, 0, n);
            return Encoding.UTF8.GetString(b);
        }
    }

    public sealed class LunaElement
    {
        public string Tag, Text, Url, Id, Link;   // Text is the alt text for img
        public string[] Classes;
        public bool Bold, Italic, HasInlineStyle, IsText;
        public int Width, Height;                  // img/video width and height attributes, 0 = not given
        public int ListDepth;                      // nesting level of <ul>/<ol>, 0 = not in a list
        public int ListMarker;                     // first element of a list item: 1 disc, 2 circle, 3 square, 4 number
        public int ListNumber;                     // number to draw when ListMarker is 4
        public int Layout;                         // LayoutNewLine / LayoutInline flags
        public const int LayoutNewLine = 1;        // starts a new line even if its tag is inline
        public const int LayoutInline = 2;         // continues the current line even if its tag is a block
        public const int LayoutDisabled = 4;       // a disabled button
        public const int LayoutContinues = 8;      // the next element continues on this line (a block tag must not end it)
        // text inside <a href> keeps the link even when it sits in a span, heading or paragraph; img/video Url is their source, not a link
        public bool IsLink { get { return IsText && Url.Length > 0; } }
    }

    public struct TagInfo
    {
        public int PointSize;
        public bool Bold, Italic, IsBlock, IsInline;
    }

    public sealed class LunaEngine : IDisposable
    {
        internal IntPtr Handle;
        internal readonly object Sync = new object();   // serialises image fetches and disposal

        public LunaEngine()
        {
            Handle = Native.luna_engine_create();
            if (Handle == IntPtr.Zero) throw new OutOfMemoryException("luna_engine_create failed");
        }

        public string Version { get { return Native.Str(Native.luna_engine_version(Handle)); } }
        public string ShellVersion { get { return Native.Str(Native.luna_engine_shell_version(Handle)); } }
        public string CssVersion { get { return Native.Str(Native.luna_engine_css_version(Handle)); } }
        public string HomeUrl { get { return Native.Str(Native.luna_engine_home_url(Handle)); } }
        public string UserAgent { get { return Native.Str(Native.luna_engine_user_agent(Handle)); } }

        /// <summary>Settings file for luna://settings (default %APPDATA%\Tethys\luna.ini). Pass "" to keep settings for the session only.</summary>
        public void SetSettingsPath(string path) { Native.luna_engine_set_settings_path(Handle, path); }

        /// <summary>Folder for luna://downloads (default %USERPROFILE%\Downloads).</summary>
        public void SetDownloadDir(string dir) { Native.luna_engine_set_download_dir(Handle, dir); }

        /// <summary>Name and version used in the User-Agent (Callisto 27.2.4 by default).</summary>
        public void SetShell(string name, string version) { Native.luna_engine_set_shell(Handle, name, version); }

        public List<KeyValuePair<string, string>> Bookmarks
        {
            get
            {
                var list = new List<KeyValuePair<string, string>>();
                int n = Native.luna_engine_bookmark_count(Handle);
                for (int i = 0; i < n; i++)
                    list.Add(new KeyValuePair<string, string>(
                        Native.Str(Native.luna_engine_bookmark_name(Handle, i)),
                        Native.Str(Native.luna_engine_bookmark_url(Handle, i))));
                return list;
            }
        }

        /// <summary>Blocking GET. Returns the body, or "LUNA_SECURITY_ERROR". Run it off the UI thread if you want it non-blocking.</summary>
        public string Request(string url, bool ignoreSecurity, out string finalUrl)
        {
            IntPtr fu;
            IntPtr body = Native.luna_request(Handle, url, ignoreSecurity ? 1 : 0, out fu);
            try { finalUrl = Native.Str(fu); return Native.Str(body); }
            finally { Native.luna_free(body); Native.luna_free(fu); }
        }

        /// <summary>Image bytes (cached inside the engine), or null. Safe to call from a worker thread; calls are serialised.</summary>
        public byte[] FetchImage(string url)
        {
            lock (Sync)
            {
                if (Handle == IntPtr.Zero) return null;
                int len;
                IntPtr p = Native.luna_fetch_image(Handle, url, out len);
                if (p == IntPtr.Zero || len <= 0) return null;
                byte[] b = new byte[len];
                Marshal.Copy(p, b, 0, len);
                return b;
            }
        }

        /// <summary>True when the bytes start with a WebP header (RIFF....WEBP).</summary>
        public static bool IsWebP(byte[] d)
        {
            return d != null && d.Length >= 12 && d[0] == 'R' && d[1] == 'I' && d[2] == 'F' && d[3] == 'F' &&
                   d[8] == 'W' && d[9] == 'E' && d[10] == 'B' && d[11] == 'P';
        }

        /// <summary>Decodes a WebP image (lossy, lossless, alpha; first frame of an animation) with the bundled libwebp. Null on failure.</summary>
        public static Bitmap DecodeWebP(byte[] data)
        {
            if (data == null || data.Length < 12) return null;
            int w, h;
            IntPtr px = Native.luna_decode_webp(data, data.Length, out w, out h);
            if (px == IntPtr.Zero) return null;
            try
            {
                Bitmap bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb);
                BitmapData bd = bmp.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
                try
                {
                    int rowBytes = w * 4;
                    byte[] row = new byte[rowBytes];
                    for (int y = 0; y < h; y++)
                    {
                        Marshal.Copy(IntPtr.Add(px, y * rowBytes), row, 0, rowBytes);
                        Marshal.Copy(row, 0, IntPtr.Add(bd.Scan0, y * bd.Stride), rowBytes);
                    }
                }
                finally { bmp.UnlockBits(bd); }
                return bmp;
            }
            finally { Native.luna_free(px); }
        }

        public void ClearImageCache()
        {
            lock (Sync) { if (Handle != IntPtr.Zero) Native.luna_engine_clear_image_cache(Handle); }
        }

        public static TagInfo GetTagInfo(string tag)
        {
            int pt, b, i, blk, inl;
            Native.luna_tag_info(tag, out pt, out b, out i, out blk, out inl);
            return new TagInfo { PointSize = pt, Bold = b != 0, Italic = i != 0, IsBlock = blk != 0, IsInline = inl != 0 };
        }

        public void Dispose()
        {
            lock (Sync)
            {
                if (Handle != IntPtr.Zero) { Native.luna_engine_destroy(Handle); Handle = IntPtr.Zero; }
            }
            GC.SuppressFinalize(this);
        }
        ~LunaEngine() { Dispose(); }
    }

    /// <summary>One tab's document: history, parsed elements and Aurelius styles. Dispose pages before their engine.</summary>
    public sealed class LunaPage : IDisposable
    {
        public const int SaveHistory = 1, ClearForward = 2, IgnoreSecurity = 4;
        public const int Ok = 0, SecurityWarning = 1, Crashed = 2, Download = 3;

        IntPtr handle;
        Native.StatusCallback cb;   // keep a reference so the GC does not collect it

        /// <summary>loading flag, url. Raised on the thread that calls Load.</summary>
        public event Action<bool, string> LoadingChanged;

        public LunaPage(LunaEngine engine)
        {
            handle = Native.luna_page_create(engine.Handle);
            if (handle == IntPtr.Zero) throw new OutOfMemoryException("luna_page_create failed");
            cb = (loading, url, user) =>
            {
                var h = LoadingChanged;
                if (h != null) h(loading != 0, Native.Str(url));
            };
            Native.luna_page_set_status_callback(handle, cb, IntPtr.Zero);
        }

        public int Load(string url, int flags = SaveHistory | ClearForward) { return Native.luna_page_load(handle, url, flags); }
        public void SetHtml(string baseUrl, string html) { Native.luna_page_set_html(handle, baseUrl, html); }

        public string Title { get { return Native.Str(Native.luna_page_title(handle)); } }
        /// <summary>The Callisto tab text: first 15 characters plus "..." when longer.</summary>
        public string TabTitle { get { string t = Title; return t.Length > 15 ? t.Substring(0, 15) + "..." : t; } }
        public string BaseUrl { get { return Native.Str(Native.luna_page_base_url(handle)); } }
        public string RawSource { get { return Native.Str(Native.luna_page_raw_source(handle)); } }
        /// <summary>CSS colour text of the body background ("#ffffff" default); feed to ColorTranslator.FromHtml.</summary>
        public string BackgroundColor { get { return Native.Str(Native.luna_page_bg_color(handle)); } }
        public string StyleReport { get { return Native.Str(Native.luna_page_style_report(handle)); } }

        public bool CanGoBack { get { return Native.luna_page_can_go_back(handle) != 0; } }
        public bool CanGoForward { get { return Native.luna_page_can_go_forward(handle) != 0; } }
        public int Back() { return Native.luna_page_back(handle); }
        public int Forward() { return Native.luna_page_forward(handle); }
        public int Refresh() { return Native.luna_page_refresh(handle); }
        public int ClickLink(string href) { return Native.luna_page_click_link(handle, href); }
        public string ResolveUrl(string rel) { return Native.Str(Native.luna_page_resolve_url(handle, rel)); }

        public int ElementCount { get { return Native.luna_page_element_count(handle); } }

        public LunaElement GetElement(int index)
        {
            Native.Element e;
            if (Native.luna_page_element_get(handle, index, out e) == 0) return null;
            string cls = Native.Str(e.classes);
            return new LunaElement
            {
                Tag = Native.Str(e.tag), Text = Native.Str(e.text), Url = Native.Str(e.url), Id = Native.Str(e.id),
                Link = Native.Str(e.link),
                Classes = cls.Length == 0 ? new string[0] : cls.Split(' '),
                Bold = e.bold != 0, Italic = e.italic != 0, HasInlineStyle = e.hasInlineStyle != 0, IsText = e.isText != 0,
                Width = e.width, Height = e.height,
                ListDepth = e.listDepth, ListMarker = e.listMarker, ListNumber = e.listNumber, Layout = e.layout
            };
        }

        public List<LunaElement> GetElements()
        {
            int n = ElementCount;
            var list = new List<LunaElement>(n);
            for (int i = 0; i < n; i++) list.Add(GetElement(i));
            return list;
        }

        /// <summary>Raw attribute of an element ("" if it has none): colspan, rowspan, border, cellpadding, width, align, bgcolor ...</summary>
        public string GetAttr(int index, string name) { return Native.Str(Native.luna_page_element_attr(handle, index, name)); }

        /// <summary>Like GetProp, but "" when no rule or inline style sets the property (no fallback value).</summary>
        public string GetCss(int index, string prop) { return Native.Str(Native.luna_page_element_css(handle, index, prop)); }

        /// <summary>Inspect element report: tag, text, link, the CSS rules that match (lowest priority first), computed values.</summary>
        public string Inspect(int index) { return Native.Str(Native.luna_page_element_inspect(handle, index)); }

        /// <summary>Aurelius lookup (inline > id > class > tag > body > *); "undefined" when unset.</summary>
        public string GetProp(int index, string prop) { return Native.Str(Native.luna_page_element_prop(handle, index, prop)); }

        public void Dispose()
        {
            if (handle != IntPtr.Zero) { Native.luna_page_destroy(handle); handle = IntPtr.Zero; }
            GC.SuppressFinalize(this);
        }
        ~LunaPage() { Dispose(); }
    }
}
