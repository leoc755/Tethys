using System;
using System.Collections.Generic;
using System.Drawing;
using System.Threading.Tasks;
using System.Windows.Forms;
using Tethys.Luna;

namespace WindowsFormsApp1
{
    /// <summary>Draws a Luna page with GDI+. All page data is copied out in SetPage, so painting never touches the native page.</summary>
    public class LunaView : Panel
    {
        private sealed class Item
        {
            public int Kind;                 // 0 = text, 1 = br, 2 = hr
            public string Text, Url;
            public bool IsLink, Block, Inline;
            public Font Font;
            public Color Color;
            public StringAlignment Align;
        }

        private readonly List<Item> items = new List<Item>();
        private readonly Dictionary<string, Font> fonts = new Dictionary<string, Font>();
        private readonly List<KeyValuePair<RectangleF, string>> hits = new List<KeyValuePair<RectangleF, string>>();
        private Color bg = Color.White;

        public event Action<string> LinkClicked;

        public LunaView()
        {
            DoubleBuffered = true;
            AutoScroll = true;
            TabStop = true;
            SetStyle(ControlStyles.Selectable | ControlStyles.ResizeRedraw, true);
        }

        private static Color Css(string s, Color fallback)
        {
            if (s == "undefined") return fallback;
            try { return ColorTranslator.FromHtml(s); }
            catch { return fallback; }
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

        private void ClearItems()
        {
            items.Clear();
            hits.Clear();
            foreach (Font f in fonts.Values) f.Dispose();
            fonts.Clear();
        }

        /// <summary>Copy the page's elements and resolved styles. Call on the UI thread when no load is running.</summary>
        public void SetPage(LunaPage p)
        {
            ClearItems();
            bg = Css(p.BackgroundColor, Color.White);
            int n = p.ElementCount;
            for (int i = 0; i < n; i++)
            {
                LunaElement el = p.GetElement(i);
                if (el == null) continue;
                if (el.Tag == "br") { items.Add(new Item { Kind = 1 }); continue; }
                if (el.Tag == "hr") { items.Add(new Item { Kind = 2 }); continue; }
                if (!el.IsText) continue;   // img / video are not drawn yet

                TagInfo ti = LunaEngine.GetTagInfo(el.Tag);
                FontStyle fs = FontStyle.Regular;
                if (el.Bold || ti.Bold) fs |= FontStyle.Bold;
                if (el.Italic || ti.Italic) fs |= FontStyle.Italic;
                if (el.IsLink) fs |= FontStyle.Underline;

                string align = p.GetProp(i, "text-align");
                items.Add(new Item
                {
                    Kind = 0,
                    Text = el.Text,
                    Url = el.IsLink ? el.Url : null,
                    IsLink = el.IsLink,
                    Block = ti.IsBlock,
                    Inline = ti.IsInline,
                    Font = GetFont(ti.PointSize, fs),
                    Color = Css(p.GetProp(i, "color"), el.Tag == "a" ? Color.Blue : Color.Black),
                    Align = align == "center" ? StringAlignment.Center :
                            align == "right" ? StringAlignment.Far : StringAlignment.Near
                });
            }
            AutoScrollPosition = new Point(0, 0);
            Invalidate();
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.Clear(bg);
            hits.Clear();
            g.TranslateTransform(AutoScrollPosition.X, AutoScrollPosition.Y);

            const int margin = 30;
            int x = margin, y = 40, lineH = 0;

            using (StringFormat sf = new StringFormat(StringFormat.GenericTypographic))
            {
                sf.FormatFlags &= ~StringFormatFlags.LineLimit;

                foreach (Item it in items)
                {
                    if (it.Kind == 1)
                    {
                        y += lineH > 0 ? lineH : 20; x = margin; lineH = 0;
                        continue;
                    }
                    if (it.Kind == 2)
                    {
                        y += lineH + 5;
                        using (Pen pen = new Pen(Color.FromArgb(204, 204, 204)))
                            g.DrawLine(pen, margin, y + 10, ClientSize.Width - margin, y + 10);
                        y += 25; x = margin; lineH = 0;
                        continue;
                    }

                    if (it.Block)
                    {
                        if (x != margin) y += lineH + 5;
                        x = margin; lineH = 0;
                    }

                    sf.Alignment = it.Align;
                    int avail = Math.Max(50, ClientSize.Width - margin - x);
                    SizeF sz = g.MeasureString(it.Text, it.Font, avail, sf);
                    RectangleF r = new RectangleF(x, y, avail, sz.Height + 2);
                    using (Brush b = new SolidBrush(it.Color))
                        g.DrawString(it.Text, it.Font, b, r, sf);
                    if (it.IsLink) hits.Add(new KeyValuePair<RectangleF, string>(r, it.Url));

                    if (it.Inline)
                    {
                        x += (int)Math.Ceiling(sz.Width) + 4;
                        lineH = Math.Max(lineH, (int)Math.Ceiling(sz.Height));
                    }
                    else
                    {
                        y += (int)Math.Ceiling(sz.Height) + 10;
                        x = margin; lineH = 0;
                    }
                }
            }

            Size want = new Size(0, y + 100);
            if (AutoScrollMinSize != want) AutoScrollMinSize = want;
        }

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
            if (disposing) ClearItems();
            base.Dispose(disposing);
        }
    }

    /// <summary>One Luna tab: a LunaPage plus its LunaView. Loads run on a worker thread.</summary>
    public sealed class LunaTab : IDisposable
    {
        public readonly LunaView View = new LunaView();
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
