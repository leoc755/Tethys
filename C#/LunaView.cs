using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using Tethys.Luna;

namespace WindowsFormsApp1
{
    /// <summary>
    /// Draws a Luna page with GDI+. Page data is copied out in SetPage, so painting never touches the native page.
    /// Images load on a background thread; videos are NyxPlayer child controls.
    /// </summary>
    public class LunaView : Panel
    {
        private sealed class Box
        {
            public int Kind;                  // 0 text, 1 br, 2 hr, 3 image, 4 video
            public string Text;               // text run, or the alt text of an image
            public string Url;                // image or video source
            public string Link;               // href when clickable (text runs and linked images)
            public bool Block, Inline;
            public Font Font;
            public Color Color;
            public StringAlignment Align;
            public int W, H;                  // requested size, 0 = automatic
            public RectangleF Rect;           // document coordinates, set by layout
            public NyxPlayer Player;          // video only
            public bool IsLink { get { return !string.IsNullOrEmpty(Link); } }
        }

        private const int PageMargin = 30;

        private readonly LunaEngine engine;
        private readonly List<Box> boxes = new List<Box>();
        private readonly Dictionary<string, Font> fonts = new Dictionary<string, Font>();
        private readonly List<KeyValuePair<RectangleF, string>> hits = new List<KeyValuePair<RectangleF, string>>();
        private readonly Dictionary<string, Image> images = new Dictionary<string, Image>();
        private readonly HashSet<string> failed = new HashSet<string>();
        private readonly HashSet<string> requested = new HashSet<string>();
        private readonly object qlock = new object();
        private readonly Queue<KeyValuePair<int, string>> queue = new Queue<KeyValuePair<int, string>>();
        private bool pumping;
        private int generation;
        private bool laying;
        private int lastWidth = -1;
        private Color bg = Color.White;

        public event Action<string> LinkClicked;

        public LunaView(LunaEngine engine)
        {
            this.engine = engine;
            DoubleBuffered = true;
            AutoScroll = true;
            TabStop = true;
            SetStyle(ControlStyles.Selectable | ControlStyles.ResizeRedraw, true);
        }

        // ---------------------------------------------------------------- helpers

        private static Color Css(string s, Color fallback)
        {
            if (s == "undefined") return fallback;
            try { return ColorTranslator.FromHtml(s); }
            catch { return fallback; }
        }

        /// <summary>"200px" or "200" from CSS wins over the attribute; percentages are ignored.</summary>
        private static int Dim(string css, int attr)
        {
            if (css != null && css != "undefined")
            {
                string s = css.Trim();
                int i = 0, n = 0;
                while (i < s.Length && s[i] >= '0' && s[i] <= '9' && n < 100000) { n = n * 10 + (s[i] - '0'); i++; }
                if (i > 0 && (i == s.Length || s.Substring(i).Trim().Equals("px", StringComparison.OrdinalIgnoreCase)) && n > 0)
                    return n;
            }
            return attr;
        }

        private Font GetFont(int pt, FontStyle fs)
        {
            string key = pt + "/" + (int)fs;
            Font f;
            if (!fonts.TryGetValue(key, out f))
            {
                f = new Font("Times New Roman", pt, fs, GraphicsUnit.Point);
                fonts[key] = f;
            }
            return f;
        }

        private static StringFormat MakeFormat()
        {
            StringFormat sf = new StringFormat(StringFormat.GenericTypographic);
            sf.FormatFlags &= ~StringFormatFlags.LineLimit;
            return sf;
        }

        private void ClearBoxes()
        {
            foreach (Box b in boxes)
            {
                if (b.Player != null) { Controls.Remove(b.Player); b.Player.Dispose(); b.Player = null; }
            }
            boxes.Clear();
            hits.Clear();
            foreach (Font f in fonts.Values) f.Dispose();
            fonts.Clear();
            foreach (Image im in images.Values) im.Dispose();
            images.Clear();
            failed.Clear();
            requested.Clear();
        }

        // ---------------------------------------------------------------- page

        /// <summary>Copy the page's elements and resolved styles. Call on the UI thread when no load is running.</summary>
        public void SetPage(LunaPage p)
        {
            lock (qlock) { generation++; queue.Clear(); }
            ClearBoxes();
            bg = Css(p.BackgroundColor, Color.White);

            int n = p.ElementCount;
            for (int i = 0; i < n; i++)
            {
                LunaElement el = p.GetElement(i);
                if (el == null) continue;

                if (el.Tag == "br") { boxes.Add(new Box { Kind = 1 }); continue; }
                if (el.Tag == "hr") { boxes.Add(new Box { Kind = 2 }); continue; }
                if (el.Tag == "img" || el.Tag == "video")
                {
                    if (el.Url.Length == 0) continue;
                    boxes.Add(new Box
                    {
                        Kind = el.Tag == "img" ? 3 : 4,
                        Url = el.Url,
                        Text = el.Text,
                        Link = el.Link,
                        Block = true,
                        W = Dim(p.GetProp(i, "width"), el.Width),
                        H = Dim(p.GetProp(i, "height"), el.Height)
                    });
                    continue;
                }
                if (!el.IsText) continue;

                TagInfo ti = LunaEngine.GetTagInfo(el.Tag);
                FontStyle fs = FontStyle.Regular;
                if (el.Bold || ti.Bold) fs |= FontStyle.Bold;
                if (el.Italic || ti.Italic) fs |= FontStyle.Italic;
                if (el.IsLink) fs |= FontStyle.Underline;

                string align = p.GetProp(i, "text-align");
                boxes.Add(new Box
                {
                    Kind = 0,
                    Text = el.Text,
                    Link = el.IsLink ? el.Url : null,
                    Block = ti.IsBlock,
                    Inline = ti.IsInline,
                    Font = GetFont(ti.PointSize, fs),
                    Color = Css(p.GetProp(i, "color"), el.Tag == "a" ? Color.Blue : Color.Black),
                    Align = align == "center" ? StringAlignment.Center :
                            align == "right" ? StringAlignment.Far : StringAlignment.Near
                });
            }

            AutoScrollPosition = new Point(0, 0);
            DoLayout();
            StartImageLoads();
            Invalidate();
        }

        // ---------------------------------------------------------------- layout

        private void DoLayout()
        {
            if (laying) return;
            laying = true;
            try
            {
                // a vertical scrollbar can appear while laying out, which changes the width: lay out again
                for (int pass = 0; pass < 3; pass++)
                {
                    LayoutPass();
                    if (ClientSize.Width == lastWidth) break;
                }
            }
            finally { laying = false; }
        }

        private void LayoutPass()
        {
            hits.Clear();
            int cw = ClientSize.Width;
            lastWidth = cw;
            int availW = Math.Max(50, cw - 2 * PageMargin);
            int x = PageMargin, y = 40, lineH = 0;

            using (Graphics g = CreateGraphics())
            using (StringFormat sf = MakeFormat())
            {
                foreach (Box b in boxes)
                {
                    switch (b.Kind)
                    {
                        case 1:
                            y += lineH > 0 ? lineH : 20; x = PageMargin; lineH = 0;
                            break;

                        case 2:
                            y += lineH + 5;
                            b.Rect = new RectangleF(PageMargin, y + 10, 0, 0);
                            y += 25; x = PageMargin; lineH = 0;
                            break;

                        case 0:
                        {
                            if (b.Block)
                            {
                                if (x != PageMargin) y += lineH + 5;
                                x = PageMargin; lineH = 0;
                            }
                            sf.Alignment = b.Align;
                            int avail = Math.Max(50, cw - PageMargin - x);
                            SizeF sz = g.MeasureString(b.Text, b.Font, avail, sf);
                            b.Rect = new RectangleF(x, y, avail, sz.Height + 2);
                            if (b.IsLink) hits.Add(new KeyValuePair<RectangleF, string>(b.Rect, b.Link));
                            if (b.Inline)
                            {
                                x += (int)Math.Ceiling(sz.Width) + 4;
                                lineH = Math.Max(lineH, (int)Math.Ceiling(sz.Height));
                            }
                            else
                            {
                                y += (int)Math.Ceiling(sz.Height) + 10;
                                x = PageMargin; lineH = 0;
                            }
                            break;
                        }

                        default:    // 3 image, 4 video: always on their own line
                        {
                            if (x != PageMargin) y += lineH + 5;
                            x = PageMargin; lineH = 0;
                            Size sz = b.Kind == 3 ? ImageSize(b, availW, g) : VideoSize(b, availW);
                            b.Rect = new RectangleF(PageMargin, y, sz.Width, sz.Height);
                            if (b.IsLink) hits.Add(new KeyValuePair<RectangleF, string>(b.Rect, b.Link));
                            y += sz.Height + 10;
                            break;
                        }
                    }
                }
            }

            Size want = new Size(0, y + 100);
            if (AutoScrollMinSize != want) AutoScrollMinSize = want;

            foreach (Box b in boxes) if (b.Kind == 4) PlacePlayer(b);
        }

        private Size ImageSize(Box b, int availW, Graphics g)
        {
            Image img;
            if (images.TryGetValue(b.Url, out img))
            {
                int w = img.Width, h = img.Height;
                if (b.W > 0 && b.H > 0) { w = b.W; h = b.H; }
                else if (b.W > 0) { w = b.W; h = (int)((long)img.Height * b.W / Math.Max(1, img.Width)); }
                else if (b.H > 0) { h = b.H; w = (int)((long)img.Width * b.H / Math.Max(1, img.Height)); }
                if (w > availW) { h = (int)((long)h * availW / Math.Max(1, w)); w = availW; }
                return new Size(Math.Max(1, w), Math.Max(1, h));
            }
            if (failed.Contains(b.Url))
            {
                if (b.W > 0 && b.H > 0) return new Size(Math.Min(b.W, availW), b.H);
                SizeF ts = g.MeasureString(PlaceholderLabel(b), Font);
                return new Size(Math.Min(availW, (int)ts.Width + 16), (int)ts.Height + 8);
            }
            return new Size(Math.Min(availW, b.W > 0 ? b.W : 100), b.H > 0 ? b.H : 60);   // still loading
        }

        private static Size VideoSize(Box b, int availW)
        {
            int w = Math.Max(160, Math.Min(b.W > 0 ? b.W : 640, availW));
            int h = (b.H > 0 ? b.H : w * 9 / 16) + 32;     // 32 = NyxPlayer control bar
            return new Size(w, h);
        }

        private string PlaceholderLabel(Box b)
        {
            if (failed.Contains(b.Url)) return string.IsNullOrEmpty(b.Text) ? "image" : b.Text;
            return "loading";
        }

        private void PlacePlayer(Box b)
        {
            if (b.Player == null)
            {
                b.Player = new NyxPlayer();
                Controls.Add(b.Player);
                b.Player.Open(b.Url);
            }
            b.Player.SetBounds((int)b.Rect.X + AutoScrollPosition.X, (int)b.Rect.Y + AutoScrollPosition.Y,
                               (int)b.Rect.Width, (int)b.Rect.Height);
        }

        protected override void OnResize(EventArgs e)
        {
            base.OnResize(e);
            if (boxes.Count > 0 && !laying && ClientSize.Width != lastWidth)
            {
                DoLayout();
                Invalidate();
            }
        }

        // ---------------------------------------------------------------- painting

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.Clear(bg);
            g.TranslateTransform(AutoScrollPosition.X, AutoScrollPosition.Y);
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;

            using (StringFormat sf = MakeFormat())
            using (StringFormat center = new StringFormat())
            {
                center.Alignment = StringAlignment.Center;
                center.LineAlignment = StringAlignment.Center;

                foreach (Box b in boxes)
                {
                    switch (b.Kind)
                    {
                        case 0:
                            sf.Alignment = b.Align;
                            using (Brush br = new SolidBrush(b.Color)) g.DrawString(b.Text, b.Font, br, b.Rect, sf);
                            break;

                        case 2:
                            using (Pen pen = new Pen(Color.FromArgb(204, 204, 204)))
                                g.DrawLine(pen, PageMargin, b.Rect.Y, ClientSize.Width - PageMargin, b.Rect.Y);
                            break;

                        case 3:
                        {
                            Image img;
                            if (images.TryGetValue(b.Url, out img))
                            {
                                g.DrawImage(img, b.Rect);
                            }
                            else
                            {
                                using (Pen pen = new Pen(Color.Silver))
                                    g.DrawRectangle(pen, b.Rect.X, b.Rect.Y, b.Rect.Width - 1, b.Rect.Height - 1);
                                g.DrawString(PlaceholderLabel(b), Font, Brushes.Gray, b.Rect, center);
                            }
                            break;
                        }
                        // video is a child control (NyxPlayer), nothing to draw here
                    }
                }
            }
        }

        // ---------------------------------------------------------------- images

        private void StartImageLoads()
        {
            bool start = false;
            lock (qlock)
            {
                foreach (Box b in boxes)
                {
                    if (b.Kind != 3 || !requested.Add(b.Url)) continue;
                    queue.Enqueue(new KeyValuePair<int, string>(generation, b.Url));
                }
                if (queue.Count > 0 && !pumping) { pumping = true; start = true; }
            }
            if (start) Task.Run(new Action(Pump));
        }

        // one background worker per view, so a page with many images cannot flood the thread pool
        private void Pump()
        {
            while (true)
            {
                KeyValuePair<int, string> item;
                lock (qlock)
                {
                    if (queue.Count == 0) { pumping = false; return; }
                    item = queue.Dequeue();
                    if (item.Key != generation) continue;       // belongs to a page that is gone
                }

                Image img = null;
                try
                {
                    byte[] data = LoadBytes(item.Value);
                    if (data != null) img = Decode(data);
                }
                catch (Exception) { img = null; }

                Post(item.Key, item.Value, img);
            }
        }

        private byte[] LoadBytes(string url)
        {
            if (url.StartsWith("data:", StringComparison.OrdinalIgnoreCase))
            {
                int comma = url.IndexOf(',');
                if (comma < 0) return null;
                string meta = url.Substring(5, comma - 5);
                if (!meta.EndsWith(";base64", StringComparison.OrdinalIgnoreCase)) return null;
                return Convert.FromBase64String(Uri.UnescapeDataString(url.Substring(comma + 1)));
            }
            return engine == null ? null : engine.FetchImage(url);
        }

        // GDI+ reads PNG, JPEG, GIF (first frame), BMP and TIFF; not WebP or SVG
        private static Image Decode(byte[] data)
        {
            using (MemoryStream ms = new MemoryStream(data))
            using (Image tmp = Image.FromStream(ms))
                return new Bitmap(tmp);
        }

        private void Post(int gen, string url, Image img)
        {
            try
            {
                if (IsDisposed || !IsHandleCreated) { if (img != null) img.Dispose(); return; }
                BeginInvoke(new Action(() => OnImageLoaded(gen, url, img)));
            }
            catch (Exception)
            {
                if (img != null) img.Dispose();
            }
        }

        private void OnImageLoaded(int gen, string url, Image img)
        {
            if (IsDisposed || gen != generation) { if (img != null) img.Dispose(); return; }
            if (img != null) images[url] = img; else failed.Add(url);
            DoLayout();
            Invalidate();
        }

        // ---------------------------------------------------------------- mouse

        private string FindLink(Point p)
        {
            PointF q = new PointF(p.X - AutoScrollPosition.X, p.Y - AutoScrollPosition.Y);
            foreach (var h in hits) if (h.Key.Contains(q)) return h.Value;
            return null;
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            base.OnMouseDown(e);
            Focus();
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e);
            Cursor = FindLink(e.Location) != null ? Cursors.Hand : Cursors.Default;
        }

        protected override void OnMouseUp(MouseEventArgs e)
        {
            base.OnMouseUp(e);
            if (e.Button != MouseButtons.Left) return;
            string href = FindLink(e.Location);
            if (href != null && LinkClicked != null) LinkClicked(href);
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                lock (qlock) { generation++; queue.Clear(); }
                ClearBoxes();
            }
            base.Dispose(disposing);
        }
    }

    /// <summary>One Luna tab: a LunaPage plus its LunaView. Loads run on a worker thread.</summary>
    public sealed class LunaTab : IDisposable
    {
        public readonly LunaView View;
        private readonly LunaPage page;
        private bool busy, disposeWanted;

        // snapshot of page state, refreshed on the UI thread after each load
        public string Url { get; private set; }
        public string TabTitle { get; private set; }
        public bool CanGoBack { get; private set; }
        public bool CanGoForward { get; private set; }

        /// <summary>Raised on the UI thread after each load finishes.</summary>
        public event Action Navigated;

        public LunaTab(LunaEngine engine)
        {
            View = new LunaView(engine);
            page = new LunaPage(engine);
            Url = "";
            TabTitle = "New Tab";
            View.LinkClicked += href => Run(() => page.ClickLink(href));
        }

        public void Load(string url) { Run(() => page.Load(url)); }
        public void Back() { Run(() => page.Back()); }
        public void Forward() { Run(() => page.Forward()); }
        public void Refresh() { Run(() => page.Refresh()); }

        private async void Run(Func<int> action)
        {
            if (busy || disposeWanted) return;
            busy = true;
            try { await Task.Run(action); }
            catch (Exception) { }
            busy = false;

            if (disposeWanted) { page.Dispose(); return; }

            Url = page.BaseUrl;
            TabTitle = page.TabTitle;
            CanGoBack = page.CanGoBack;
            CanGoForward = page.CanGoForward;
            View.SetPage(page);
            if (Navigated != null) Navigated();
        }

        public void Dispose()
        {
            if (busy) disposeWanted = true;     // the worker still uses the native page
            else page.Dispose();
            View.Dispose();
        }
    }
}
