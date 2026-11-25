using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Threading.Tasks;
using System.Windows.Forms;
using Tethys.Luna;

namespace WindowsFormsApp1
{
    /// <summary>
    /// The scrollable page surface: layout (text flow, lists, tables), painting, images, buttons and video.
    /// Selection, copy, the link bubble and inspect element are in LunaCanvas.Input.cs.
    /// Page data is copied out in SetPage, so painting never touches the native page.
    /// </summary>
    public partial class LunaCanvas : Panel
    {
        private sealed class Box
        {
            public int Kind;                  // 0 text, 1 br, 2 hr, 3 image, 4 video, 5 button
            public int Index;                 // element index in the page (used by inspect element)
            public string Tag;
            public string Text;               // text run, the alt text of an image, or the label of a button
            public string Url;                // image or video source
            public string Link;               // href when clickable (text runs, linked images, buttons in links)
            public string Target;             // Link resolved against the page address (shown in the link bubble)
            public string Id;                 // id attribute, for #fragment links
            public bool Block, Inline;
            public bool Disabled;             // buttons
            public Font Font;
            public Color Color;
            public Color? Back;               // buttons: explicit background colour
            public StringAlignment Align;
            public int W, H;                  // requested size, 0 = automatic
            public int ListDepth;             // nesting level of the list the element is in
            public int Marker;                // list marker on the first element of an item: 1 disc, 2 circle, 3 square, 4 number
            public int Number;                // the number for marker 4
            public int Left;                  // left edge of the content: container edge plus list indent
            public RectangleF Rect;           // layout box in document coordinates
            public RectangleF Ink;            // the area the text really covers
            public RectangleF[] Chars;        // one rectangle per character, built when needed
            public NyxPlayer Player;          // video only
            public bool IsLink { get { return !string.IsNullOrEmpty(Link); } }
        }

        private sealed class Hit
        {
            public RectangleF Rect;
            public Box Box;
        }

        // ---- tables: built from the table marker elements, laid out recursively ----

        private sealed class Table
        {
            public int Index, Depth;                       // element index; list depth at the <table> tag
            public List<Row> Rows = new List<Row>();
            public List<object> Caption = new List<object>();
            public int Border;                             // outer border width in pixels
            public Color BorderColor = Color.Gray;
            public int Spacing = 2, Padding = 1;           // browser defaults
            public bool Collapse;
            public string WidthSpec;                       // "300" or "50%"
            public string Place;                           // align attribute: left, center, right
            public Color? Back, Fore;
            public List<Cell> Cells = new List<Cell>();
            // metrics, rebuilt on every layout pass
            public bool MetricsReady;
            public int Cols, RowCount;
            public float[] ColMin, ColMax;
            public float[] ColFixed, ColPct;
        }

        private sealed class Row
        {
            public int Index;
            public List<Cell> Cells = new List<Cell>();
            public Color? Back, Fore;
            public StringAlignment? Align;
            public string Valign;
        }

        private sealed class Cell
        {
            public int Index, BaseDepth;
            public bool Header;
            public int ColSpan = 1, RowSpan = 1;
            public int Row, Col;                           // grid position
            public string WidthSpec;
            public int HeightSpec;
            public Color? Back, Fore;
            public StringAlignment? Align;
            public string Valign = "middle";
            public int Pad, Border;
            public Color BorderColor = Color.Gray;
            public List<object> Items = new List<object>();
            public float MinW, MaxW;                       // including padding and border
            public float ContentH;
        }

        private sealed class Deco                          // a filled and/or stroked rectangle drawn under the text
        {
            public RectangleF Rect;
            public Color? Fill;
            public int BorderW;
            public Color BorderColor;
            public bool Centered;                          // stroke centred on the edge (collapsed borders)
        }

        private sealed class TableCtx
        {
            public Table Table; public Row Row; public Cell Cell;
            public List<object> Items; public Color? Fore; public StringAlignment? Align;
        }

        private const int PageMargin = 30;
        private const int ListIndent = 28;
        private const float Huge = 100000f;

        private readonly LunaEngine engine;
        private readonly List<Box> boxes = new List<Box>();                  // every drawable box, in document order
        private readonly List<object> rootItems = new List<object>();        // Box and Table, in layout structure
        private readonly List<Table> allTables = new List<Table>();
        private readonly List<Deco> decos = new List<Deco>();
        private readonly Dictionary<string, Font> fonts = new Dictionary<string, Font>();
        private readonly List<Hit> hits = new List<Hit>();
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
        private Font btnFont;

        public event Action<string> LinkClicked;

        public LunaCanvas(LunaEngine engine)
        {
            this.engine = engine;
            DoubleBuffered = true;
            AutoScroll = true;
            TabStop = true;
            SetStyle(ControlStyles.Selectable | ControlStyles.ResizeRedraw, true);
            InitInput();
        }

        // ---------------------------------------------------------------- helpers

        private static Color Css(string s, Color fallback)
        {
            Color? c = TryColor(s);
            return c.HasValue ? c.Value : fallback;
        }

        private static Color? TryColor(string s)
        {
            if (string.IsNullOrEmpty(s) || s == "undefined") return null;
            s = s.Trim();
            if (s.Equals("transparent", StringComparison.OrdinalIgnoreCase)) return null;
            try { return ColorTranslator.FromHtml(s); }
            catch { return null; }
        }

        private static StringAlignment? TryAlign(string s)
        {
            if (string.IsNullOrEmpty(s)) return null;
            s = s.Trim().ToLowerInvariant();
            if (s == "center" || s == "middle") return StringAlignment.Center;
            if (s == "right") return StringAlignment.Far;
            if (s == "left" || s == "justify") return StringAlignment.Near;
            return null;
        }

        private static int ParseInt(string s, int def)
        {
            if (string.IsNullOrEmpty(s)) return def;
            s = s.Trim();
            int i = 0, n = 0;
            while (i < s.Length && s[i] >= '0' && s[i] <= '9' && n < 100000) { n = n * 10 + (s[i] - '0'); i++; }
            return i == 0 ? def : n;
        }

        /// <summary>"200px" or "200" from CSS wins over the attribute; percentages are ignored.</summary>
        private static int Dim(string css, int attr)
        {
            if (css != null && css != "undefined" && css.Length > 0)
            {
                string s = css.Trim();
                int i = 0, n = 0;
                while (i < s.Length && s[i] >= '0' && s[i] <= '9' && n < 100000) { n = n * 10 + (s[i] - '0'); i++; }
                if (i > 0 && (i == s.Length || s.Substring(i).Trim().Equals("px", StringComparison.OrdinalIgnoreCase)) && n > 0)
                    return n;
            }
            return attr;
        }

        /// <summary>"1px solid #ccc": width and colour; false when there is no visible border.</summary>
        private static bool ParseBorder(string css, out int width, out Color? color)
        {
            width = 0; color = null;
            if (string.IsNullOrEmpty(css) || css == "undefined") return false;
            bool none = false;
            foreach (string tok in css.Split(new char[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries))
            {
                string t = tok.ToLowerInvariant();
                if (t == "none" || t == "hidden") none = true;
                else if (t == "thin") width = 1;
                else if (t == "medium") width = 2;
                else if (t == "thick") width = 3;
                else if (t == "solid" || t == "dashed" || t == "dotted" || t == "double" || t == "groove" || t == "ridge" || t == "inset" || t == "outset") { }
                else if (t[0] >= '0' && t[0] <= '9') width = ParseInt(t, 0);
                else { Color? c = TryColor(tok); if (c.HasValue) color = c; }
            }
            if (none || width <= 0) { width = 0; return false; }
            return true;
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

        private Font ButtonFont
        {
            get { if (btnFont == null) btnFont = new Font("Segoe UI", 9f, FontStyle.Regular, GraphicsUnit.Point); return btnFont; }
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
            rootItems.Clear();
            allTables.Clear();
            decos.Clear();
            hits.Clear();
            foreach (Font f in fonts.Values) f.Dispose();
            fonts.Clear();
            foreach (Image im in images.Values) im.Dispose();
            images.Clear();
            failed.Clear();
            requested.Clear();
        }

        // ---------------------------------------------------------------- page

        private Table NewTable(LunaPage p, int i, LunaElement el)
        {
            Table t = new Table();
            t.Index = i;
            t.Depth = el.ListDepth;
            t.Border = ParseInt(p.GetAttr(i, "border"), 0);
            t.Padding = ParseInt(p.GetAttr(i, "cellpadding"), 1);
            t.Spacing = ParseInt(p.GetAttr(i, "cellspacing"), 2);

            int bw; Color? bc;
            if (ParseBorder(p.GetCss(i, "border"), out bw, out bc)) { t.Border = bw; if (bc.HasValue) t.BorderColor = bc.Value; }
            string collapse = p.GetCss(i, "border-collapse");
            if (collapse.Equals("collapse", StringComparison.OrdinalIgnoreCase)) { t.Collapse = true; t.Spacing = 0; }
            else
            {
                string bs = p.GetCss(i, "border-spacing");
                if (bs.Length > 0) t.Spacing = ParseInt(bs, t.Spacing);
            }

            t.WidthSpec = p.GetCss(i, "width");
            if (t.WidthSpec.Length == 0) t.WidthSpec = p.GetAttr(i, "width");
            t.Place = p.GetAttr(i, "align").ToLowerInvariant();
            t.Back = TryColor(p.GetCss(i, "background-color")) ?? TryColor(p.GetAttr(i, "bgcolor"));
            t.Fore = TryColor(p.GetCss(i, "color"));
            return t;
        }

        private Row NewRow(LunaPage p, int i)
        {
            Row r = new Row();
            r.Index = i;
            r.Back = TryColor(p.GetCss(i, "background-color")) ?? TryColor(p.GetAttr(i, "bgcolor"));
            r.Fore = TryColor(p.GetCss(i, "color"));
            r.Align = TryAlign(p.GetCss(i, "text-align")) ?? TryAlign(p.GetAttr(i, "align"));
            string v = p.GetCss(i, "vertical-align");
            r.Valign = v.Length > 0 ? v.ToLowerInvariant() : p.GetAttr(i, "valign").ToLowerInvariant();
            return r;
        }

        private Cell NewCell(LunaPage p, int i, LunaElement el, Table t, Row row)
        {
            Cell c = new Cell();
            c.Index = i;
            c.BaseDepth = el.ListDepth;
            c.Header = el.Tag == "th";
            c.ColSpan = Math.Max(1, Math.Min(100, ParseInt(p.GetAttr(i, "colspan"), 1)));
            c.RowSpan = Math.Max(1, Math.Min(100, ParseInt(p.GetAttr(i, "rowspan"), 1)));
            c.Back = TryColor(p.GetCss(i, "background-color")) ?? TryColor(p.GetAttr(i, "bgcolor"));
            c.Fore = TryColor(p.GetCss(i, "color"));
            c.Align = TryAlign(p.GetCss(i, "text-align")) ?? TryAlign(p.GetAttr(i, "align")) ?? row.Align;
            if (c.Align == null && c.Header) c.Align = StringAlignment.Center;

            string v = p.GetCss(i, "vertical-align");
            if (v.Length == 0) v = p.GetAttr(i, "valign");
            if (v.Length == 0) v = row.Valign;
            c.Valign = v.Length > 0 ? v.ToLowerInvariant() : "middle";

            c.WidthSpec = p.GetCss(i, "width");
            if (c.WidthSpec.Length == 0) c.WidthSpec = p.GetAttr(i, "width");
            c.HeightSpec = Dim(p.GetCss(i, "height"), ParseInt(p.GetAttr(i, "height"), 0));

            string pad = p.GetCss(i, "padding");
            c.Pad = pad.Length > 0 ? ParseInt(pad, t.Padding) : t.Padding;

            int bw; Color? bc;
            if (ParseBorder(p.GetCss(i, "border"), out bw, out bc)) { c.Border = bw; if (bc.HasValue) c.BorderColor = bc.Value; }
            else if (t.Border > 0) { c.Border = 1; c.BorderColor = t.BorderColor; }
            return c;
        }

        /// <summary>Copy the page's elements and resolved styles. Call on the UI thread when no load is running.</summary>
        public void SetPage(LunaPage p)
        {
            lock (qlock) { generation++; queue.Clear(); }
            ClearBoxes();
            ResetInput();
            bg = Css(p.BackgroundColor, Color.White);

            // where the next box goes: the page, or a table cell / caption
            List<object> cur = rootItems;
            Stack<TableCtx> stack = new Stack<TableCtx>();
            Table curTable = null; Row curRow = null; Cell curCell = null;
            Color? curFore = null; StringAlignment? curAlign = null;
            Action<Box> add = b => { boxes.Add(b); cur.Add(b); };

            int n = p.ElementCount;
            for (int i = 0; i < n; i++)
            {
                LunaElement el = p.GetElement(i);
                if (el == null) continue;

                // ---- table structure ----
                if (el.Tag == "table")
                {
                    Table t = NewTable(p, i, el);
                    cur.Add(t);
                    allTables.Add(t);
                    stack.Push(new TableCtx { Table = curTable, Row = curRow, Cell = curCell, Items = cur, Fore = curFore, Align = curAlign });
                    curTable = t; curRow = null; curCell = null;
                    if (t.Fore.HasValue) curFore = t.Fore;
                    continue;
                }
                if (el.Tag == "/table")
                {
                    if (stack.Count > 0)
                    {
                        TableCtx c = stack.Pop();
                        curTable = c.Table; curRow = c.Row; curCell = c.Cell; cur = c.Items; curFore = c.Fore; curAlign = c.Align;
                    }
                    continue;
                }
                if (curTable != null && el.Tag == "tr")
                {
                    curRow = NewRow(p, i);
                    curTable.Rows.Add(curRow);
                    curCell = null;
                    cur = stack.Peek().Items;
                    continue;
                }
                if (curTable != null && (el.Tag == "td" || el.Tag == "th"))
                {
                    if (curRow == null) { curRow = NewRow(p, i); curTable.Rows.Add(curRow); }
                    Cell c = NewCell(p, i, el, curTable, curRow);
                    curRow.Cells.Add(c);
                    curTable.Cells.Add(c);
                    curCell = c;
                    cur = c.Items;
                    curFore = c.Fore ?? curRow.Fore ?? curTable.Fore ?? stack.Peek().Fore;
                    curAlign = c.Align;
                    continue;
                }
                if (curTable != null && el.Tag == "caption")
                {
                    cur = curTable.Caption;
                    curAlign = StringAlignment.Center;
                    continue;
                }
                if (curTable != null && el.Tag == "/caption")
                {
                    cur = curCell != null ? curCell.Items : stack.Peek().Items;
                    curAlign = curCell != null ? curCell.Align : (StringAlignment?)null;
                    continue;
                }

                // ---- content ----
                if (el.Tag == "br") { add(new Box { Kind = 1, Index = i, Tag = "br" }); continue; }
                if (el.Tag == "hr") { add(new Box { Kind = 2, Index = i, Tag = "hr", ListDepth = el.ListDepth }); continue; }

                if (el.Tag == "button")
                {
                    bool disabled = (el.Layout & LunaElement.LayoutDisabled) != 0;
                    string link = el.Url.Length > 0 && !disabled ? el.Url : null;
                    add(new Box
                    {
                        Kind = 5,
                        Index = i,
                        Tag = "button",
                        Text = el.Text,
                        Link = link,
                        Target = link != null ? p.ResolveUrl(link) : null,
                        Disabled = disabled,
                        Block = (el.Layout & LunaElement.LayoutNewLine) != 0,
                        Inline = true,
                        ListDepth = el.ListDepth,
                        Marker = el.ListMarker,
                        Number = el.ListNumber,
                        Back = TryColor(p.GetCss(i, "background-color")),
                        Color = Css(p.GetProp(i, "color"), Color.Black),
                        W = Dim(p.GetProp(i, "width"), ParseInt(p.GetAttr(i, "width"), 0)),
                        H = Dim(p.GetProp(i, "height"), ParseInt(p.GetAttr(i, "height"), 0))
                    });
                    continue;
                }

                if (el.Tag == "img" || el.Tag == "video")
                {
                    if (el.Url.Length == 0) continue;
                    add(new Box
                    {
                        Kind = el.Tag == "img" ? 3 : 4,
                        Index = i,
                        Tag = el.Tag,
                        Url = el.Url,
                        Text = el.Text,
                        Link = el.Link,
                        Target = el.Link.Length > 0 ? p.ResolveUrl(el.Link) : null,
                        ListDepth = el.ListDepth,
                        Marker = el.ListMarker,
                        Number = el.ListNumber,
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

                Color? own = TryColor(p.GetProp(i, "color"));
                StringAlignment? ownAlign = TryAlign(p.GetCss(i, "text-align"));
                bool newLine = (el.Layout & LunaElement.LayoutNewLine) != 0;
                bool sameLine = (el.Layout & LunaElement.LayoutInline) != 0;
                bool continues = (el.Layout & LunaElement.LayoutContinues) != 0;
                add(new Box
                {
                    Kind = 0,
                    ListDepth = el.ListDepth,
                    Marker = el.ListMarker,
                    Number = el.ListNumber,
                    Index = i,
                    Tag = el.Tag,
                    Text = el.Text,
                    Link = el.IsLink ? el.Url : null,
                    Target = el.IsLink ? p.ResolveUrl(el.Url) : null,
                    Id = el.Id,
                    Block = (ti.IsBlock || newLine) && !sameLine,     // first element of a list item starts a line; text after </a> continues it
                    Inline = ti.IsInline || sameLine || continues,      // "See <a>this</a>": the line goes on after a block tag
                    Font = GetFont(ti.PointSize, fs),
                    // own colour, then the table cell's colour, then link blue or black
                    Color = own ?? (el.IsLink ? Color.Blue : (curFore ?? Color.Black)),
                    Align = ownAlign ?? curAlign ?? StringAlignment.Near
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
            decos.Clear();
            foreach (Box b in boxes) b.Chars = null;          // character rectangles are layout dependent
            foreach (Table t in allTables) t.MetricsReady = false;
            int cw = ClientSize.Width;
            lastWidth = cw;

            float maxRight;
            float bottom;
            using (Graphics g = CreateGraphics())
            using (StringFormat sf = MakeFormat())
                bottom = LayoutItems(g, sf, rootItems, PageMargin, 40, cw - 2 * PageMargin, 0, false, out maxRight);

            // wide tables scroll sideways
            int wantW = maxRight + PageMargin > cw ? (int)Math.Ceiling(maxRight) + PageMargin : 0;
            Size want = new Size(wantW, (int)bottom + 100);
            if (AutoScrollMinSize != want) AutoScrollMinSize = want;

            foreach (Box b in boxes) if (b.Kind == 4) PlacePlayer(b);
        }

        /// <summary>
        /// Lays a list of boxes and tables out as a column inside [left, left + width] starting at top and returns the
        /// bottom edge. maxRight receives the right-most edge used. dry = only measure: no hit areas, no decorations.
        /// baseDepth is the list depth at the start of the container, so list indents are relative to it.
        /// </summary>
        private float LayoutItems(Graphics g, StringFormat sf, List<object> items, float left, float top, float width,
                                  int baseDepth, bool dry, out float maxRight)
        {
            float right = left + width;
            float x = left, y = top, lineH = 0;
            bool atStart = true;                  // nothing placed on the current line yet
            maxRight = left;

            foreach (object o in items)
            {
                Table table = o as Table;
                if (table != null)
                {
                    if (!atStart) { y += lineH + 10; lineH = 0; }
                    float tl = left + Math.Max(0, table.Depth - baseDepth) * ListIndent;
                    float tr;
                    y = LayoutTable(g, sf, table, tl, y, Math.Max(50, right - tl), dry, out tr) + 10;
                    maxRight = Math.Max(maxRight, tr);
                    atStart = true; lineH = 0;
                    continue;
                }

                Box b = (Box)o;
                float bl = left + Math.Max(0, b.ListDepth - baseDepth) * ListIndent;     // list items are indented
                b.Left = (int)bl;

                switch (b.Kind)
                {
                    case 1:
                        y += lineH > 0 ? lineH : 20; lineH = 0; atStart = true;
                        break;

                    case 2:
                        y += lineH + 5;
                        b.Rect = new RectangleF(bl, y + 10, Math.Max(0, right - bl), 0);
                        y += 25; lineH = 0; atStart = true;
                        break;

                    case 0:
                    {
                        if (b.Block && !atStart) { y += lineH + 10; lineH = 0; atStart = true; }     // same gap as after a block
                        if (atStart) x = bl;
                        sf.Alignment = dry ? StringAlignment.Near : b.Align;
                        float avail = Math.Max(50, right - x);
                        SizeF sz = g.MeasureString(b.Text, b.Font, new SizeF(avail, Huge), sf);
                        b.Rect = new RectangleF(x, y, avail, sz.Height + 2);

                        float w = Math.Min(sz.Width, avail);
                        float ix = b.Align == StringAlignment.Center ? x + (avail - w) / 2 :
                                   b.Align == StringAlignment.Far ? x + avail - w : x;
                        b.Ink = new RectangleF(dry ? x : ix, y, w, sz.Height);
                        maxRight = Math.Max(maxRight, x + w);

                        if (!dry && b.IsLink) AddTextHits(g, sf, b);
                        if (b.Inline)
                        {
                            x += (float)Math.Ceiling(sz.Width) + 4;
                            lineH = Math.Max(lineH, (float)Math.Ceiling(sz.Height));
                            atStart = false;
                        }
                        else
                        {
                            y += (float)Math.Ceiling(sz.Height) + 10;
                            lineH = 0; atStart = true;
                        }
                        break;
                    }

                    case 5:     // button: sits in the line like a word
                    {
                        if (b.Block && !atStart) { y += lineH + 10; lineH = 0; atStart = true; }
                        Size bs = ButtonSize(g, b);
                        if (!atStart && x + bs.Width > right && !dry) { y += lineH + 4; lineH = 0; atStart = true; }
                        if (atStart) x = bl;
                        b.Rect = new RectangleF(x, y, bs.Width, bs.Height);
                        b.Ink = b.Rect;
                        if (!dry && b.IsLink) hits.Add(new Hit { Rect = b.Rect, Box = b });
                        x += bs.Width + 6;
                        lineH = Math.Max(lineH, bs.Height);
                        atStart = false;
                        maxRight = Math.Max(maxRight, x - 6);
                        break;
                    }

                    default:    // 3 image, 4 video: always on their own line
                    {
                        if (!atStart) { y += lineH + 10; lineH = 0; }
                        int availW = (int)Math.Max(50, right - bl);
                        Size sz = b.Kind == 3 ? ImageSize(b, availW, g) : VideoSize(b, availW);
                        b.Rect = new RectangleF(bl, y, sz.Width, sz.Height);
                        if (!dry && b.IsLink) hits.Add(new Hit { Rect = b.Rect, Box = b });
                        maxRight = Math.Max(maxRight, bl + sz.Width);
                        y += sz.Height + 10;
                        lineH = 0; atStart = true;
                        break;
                    }
                }
            }
            return atStart ? y : y + lineH;
        }

        // ---------------------------------------------------------------- tables

        private static float SpecWidth(string spec, float avail)       // px or %, -1 when not given
        {
            if (string.IsNullOrEmpty(spec) || spec == "undefined") return -1;
            string s = spec.Trim();
            int v = ParseInt(s, -1);
            if (v < 0) return -1;
            return s.EndsWith("%") ? avail * v / 100f : v;
        }

        private static float PctOf(string spec)
        {
            if (string.IsNullOrEmpty(spec)) return 0;
            string s = spec.Trim();
            return s.EndsWith("%") ? ParseInt(s, 0) : 0;
        }

        // Assigns grid positions (colspan/rowspan) and measures the narrowest and the natural width of every column.
        private void EnsureMetrics(Graphics g, StringFormat sf, Table t)
        {
            if (t.MetricsReady) return;
            t.MetricsReady = true;

            HashSet<long> occ = new HashSet<long>();
            int cols = 0, rows = t.Rows.Count;
            for (int r = 0; r < t.Rows.Count; r++)
            {
                int c = 0;
                foreach (Cell cell in t.Rows[r].Cells)
                {
                    while (occ.Contains((long)r * 1000 + c)) c++;
                    cell.Row = r; cell.Col = c;
                    cell.RowSpan = Math.Min(cell.RowSpan, t.Rows.Count - r);
                    for (int rr = 0; rr < cell.RowSpan; rr++)
                        for (int cc = 0; cc < cell.ColSpan; cc++) occ.Add((long)(r + rr) * 1000 + c + cc);
                    c += cell.ColSpan;
                    cols = Math.Max(cols, cell.Col + cell.ColSpan);
                }
            }
            t.Cols = cols; t.RowCount = rows;
            t.ColMin = new float[cols]; t.ColMax = new float[cols];
            t.ColFixed = new float[cols]; t.ColPct = new float[cols];

            foreach (Cell cell in t.Cells)
            {
                float extra = 2 * (cell.Pad + cell.Border);
                float mr;
                LayoutItems(g, sf, cell.Items, 0, 0, Huge, cell.BaseDepth, true, out mr);
                cell.MaxW = mr + extra;
                cell.MinW = Math.Min(MinContent(g, sf, cell.Items) + extra, cell.MaxW);

                float spec = SpecWidth(cell.WidthSpec, 0);
                float pct = PctOf(cell.WidthSpec);
                if (spec >= 0 && pct == 0)                             // fixed pixel width
                {
                    float w = Math.Max(cell.MinW, spec);
                    cell.MinW = cell.MaxW = w;
                    if (cell.ColSpan == 1) t.ColFixed[cell.Col] = Math.Max(t.ColFixed[cell.Col], w);
                }
                else if (pct > 0 && cell.ColSpan == 1) t.ColPct[cell.Col] = Math.Max(t.ColPct[cell.Col], pct);

                if (cell.ColSpan == 1)
                {
                    t.ColMin[cell.Col] = Math.Max(t.ColMin[cell.Col], cell.MinW);
                    t.ColMax[cell.Col] = Math.Max(t.ColMax[cell.Col], cell.MaxW);
                }
            }

            // cells that span columns widen the columns they cover when they need more room
            foreach (Cell cell in t.Cells)
            {
                if (cell.ColSpan == 1) continue;
                float sMin = 0, sMax = 0;
                for (int c = cell.Col; c < cell.Col + cell.ColSpan; c++) { sMin += t.ColMin[c]; sMax += t.ColMax[c]; }
                float gap = t.Spacing * (cell.ColSpan - 1);
                if (cell.MinW > sMin + gap) for (int c = cell.Col; c < cell.Col + cell.ColSpan; c++) t.ColMin[c] += (cell.MinW - sMin - gap) / cell.ColSpan;
                if (cell.MaxW > sMax + gap) for (int c = cell.Col; c < cell.Col + cell.ColSpan; c++) t.ColMax[c] += (cell.MaxW - sMax - gap) / cell.ColSpan;
            }
            for (int c = 0; c < cols; c++) if (t.ColMax[c] < t.ColMin[c]) t.ColMax[c] = t.ColMin[c];
        }

        // the narrowest width of a column of items: the longest word, the widest image or button
        private float MinContent(Graphics g, StringFormat sf, List<object> items)
        {
            float m = 0;
            foreach (object o in items)
            {
                Table t = o as Table;
                if (t != null)
                {
                    EnsureMetrics(g, sf, t);
                    float s = 2 * t.Border + t.Spacing * (t.Cols + 1);
                    for (int c = 0; c < t.Cols; c++) s += t.ColMin[c];
                    m = Math.Max(m, s);
                    continue;
                }
                Box b = (Box)o;
                if (b.Kind == 0)
                {
                    foreach (string word in b.Text.Split(' '))
                    {
                        if (word.Length == 0) continue;
                        m = Math.Max(m, g.MeasureString(word, b.Font, new SizeF(Huge, Huge), sf).Width);
                    }
                }
                else if (b.Kind == 3) m = Math.Max(m, ImageSize(b, 100000, g).Width);
                else if (b.Kind == 4) m = Math.Max(m, 160);
                else if (b.Kind == 5) m = Math.Max(m, ButtonSize(g, b).Width);
            }
            return m;
        }

        private float LayoutTable(Graphics g, StringFormat sf, Table t, float x, float y, float availW, bool dry, out float right)
        {
            EnsureMetrics(g, sf, t);
            int cols = t.Cols, rows = t.RowCount;
            right = x;
            if (cols == 0 || rows == 0)      // a table without cells only contributes its caption
            {
                if (t.Caption.Count == 0) return y;
                float mr0;
                float cb = LayoutItems(g, sf, t.Caption, x, y, availW, t.Depth, dry, out mr0);
                right = mr0;
                return cb;
            }

            float spacing = t.Spacing, bw = t.Border;
            float overhead = 2 * bw + spacing * (cols + 1);

            // column widths: the natural width when it fits, otherwise shrink towards the narrowest
            float[] min = (float[])t.ColMin.Clone(), max = (float[])t.ColMax.Clone();
            float spec = SpecWidth(t.WidthSpec, availW);
            float totalMax0 = 0; for (int c = 0; c < cols; c++) totalMax0 += max[c];
            float tw = spec >= 0 ? Math.Min(spec, availW) : Math.Min(availW, totalMax0 + overhead);
            float inner = tw - overhead;
            for (int c = 0; c < cols; c++)
            {
                if (t.ColPct[c] > 0) { float pw = Math.Max(min[c], inner * t.ColPct[c] / 100f); min[c] = max[c] = pw; }
            }
            float totalMin = 0, totalMax = 0;
            for (int c = 0; c < cols; c++) { totalMin += min[c]; totalMax += max[c]; }
            float[] w = new float[cols];
            if (inner >= totalMax)
            {
                float extra = spec >= 0 ? inner - totalMax : 0;       // an explicit table width stretches the columns
                for (int c = 0; c < cols; c++) w[c] = max[c] + (totalMax > 0 ? extra * max[c] / totalMax : extra / cols);
            }
            else if (inner <= totalMin) Array.Copy(min, w, cols);
            else
            {
                float f = (inner - totalMin) / (totalMax - totalMin);
                for (int c = 0; c < cols; c++) w[c] = min[c] + (max[c] - min[c]) * f;
            }
            float usedW = overhead; for (int c = 0; c < cols; c++) usedW += w[c];
            tw = usedW;

            // table placement (align attribute)
            if (t.Place == "center") x += Math.Max(0, (availW - tw) / 2);
            else if (t.Place == "right") x += Math.Max(0, availW - tw);
            right = x + tw;

            // caption above the table, as wide as the table
            float top = y;
            if (t.Caption.Count > 0)
            {
                float mrc;
                float cb = LayoutItems(g, sf, t.Caption, x, y, tw, t.Depth, dry, out mrc);
                top = cb;
            }

            float[] colX = new float[cols];
            float cx = x + bw + spacing;
            for (int c = 0; c < cols; c++) { colX[c] = cx; cx += w[c] + spacing; }

            // row heights from the cell contents at their final widths
            float[] rowH = new float[rows];
            Dictionary<Cell, float> need = new Dictionary<Cell, float>();
            foreach (Cell cell in t.Cells)
            {
                float cwid = SpanWidth(w, cell.Col, cell.ColSpan, spacing);
                float pad = cell.Pad + cell.Border;
                float mr;
                float ch = LayoutItems(g, sf, cell.Items, 0, 0, Math.Max(10, cwid - 2 * pad), cell.BaseDepth, true, out mr);
                cell.ContentH = ch;
                float h = Math.Max(ch + 2 * pad, cell.HeightSpec);
                need[cell] = h;
                if (cell.RowSpan == 1) rowH[cell.Row] = Math.Max(rowH[cell.Row], h);
            }
            foreach (Cell cell in t.Cells)
            {
                if (cell.RowSpan == 1) continue;
                float have = spacing * (cell.RowSpan - 1);
                for (int r = cell.Row; r < cell.Row + cell.RowSpan; r++) have += rowH[r];
                if (need[cell] > have) rowH[cell.Row + cell.RowSpan - 1] += need[cell] - have;
            }

            float[] rowY = new float[rows];
            float ry = top + bw + spacing;
            for (int r = 0; r < rows; r++) { rowY[r] = ry; ry += rowH[r] + spacing; }
            float bodyH = ry + bw - spacing - top;
            float tableBottom = top + bodyH;
            if (dry) return tableBottom;

            // decorations, outermost first so the text lands on top of them
            if (t.Back.HasValue || bw > 0)
                decos.Add(new Deco { Rect = new RectangleF(x, top, tw, bodyH), Fill = t.Back, BorderW = (int)bw, BorderColor = t.BorderColor });
            for (int r = 0; r < rows; r++)
            {
                if (t.Rows[r].Back.HasValue)
                    decos.Add(new Deco { Rect = new RectangleF(colX[0], rowY[r], tw - 2 * bw - 2 * spacing, rowH[r]), Fill = t.Rows[r].Back });
            }

            foreach (Cell cell in t.Cells)
            {
                float cwid = SpanWidth(w, cell.Col, cell.ColSpan, spacing);
                float chgt = 0;
                for (int r = cell.Row; r < cell.Row + cell.RowSpan; r++) chgt += rowH[r];
                chgt += spacing * (cell.RowSpan - 1);
                RectangleF rect = new RectangleF(colX[cell.Col], rowY[cell.Row], cwid, chgt);

                if (cell.Back.HasValue || cell.Border > 0)
                    decos.Add(new Deco { Rect = rect, Fill = cell.Back, BorderW = cell.Border, BorderColor = cell.BorderColor, Centered = t.Collapse });

                float pad = cell.Pad + cell.Border;
                float space = chgt - 2 * pad - cell.ContentH;
                float off = cell.Valign == "top" ? 0 : (cell.Valign == "bottom" ? Math.Max(0, space) : Math.Max(0, space / 2));
                float mr;
                LayoutItems(g, sf, cell.Items, rect.X + pad, rect.Y + pad + off, Math.Max(10, cwid - 2 * pad), cell.BaseDepth, false, out mr);
            }
            return tableBottom;
        }

        private static float SpanWidth(float[] w, int col, int span, float spacing)
        {
            float s = spacing * (span - 1);
            for (int c = col; c < col + span && c < w.Length; c++) s += w[c];
            return s;
        }

        // ---------------------------------------------------------------- buttons

        private Size ButtonSize(Graphics g, Box b)
        {
            using (StringFormat f = StringFormat.GenericTypographic.Clone() as StringFormat)
            {
                SizeF ts = g.MeasureString(b.Text.Length == 0 ? " " : b.Text, ButtonFont, new SizeF(Huge, Huge), f);
                int w = Math.Max(30, (int)Math.Ceiling(ts.Width) + 24);
                int h = (int)Math.Ceiling(ts.Height) + 10;
                if (b.W > 0) w = b.W;
                if (b.H > 0) h = b.H;
                return new Size(w, h);
            }
        }

        private static GraphicsPath RoundRect(RectangleF r, float radius)
        {
            float d = radius * 2;
            GraphicsPath path = new GraphicsPath();
            path.AddArc(r.X, r.Y, d, d, 180, 90);
            path.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            path.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
            path.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            path.CloseFigure();
            return path;
        }

        private void PaintButton(Graphics g, Box b, StringFormat center)
        {
            bool hot = hoverBtn == b && !b.Disabled;
            bool down = pressedBtn == b && hot;
            RectangleF r = new RectangleF(b.Rect.X, b.Rect.Y, b.Rect.Width - 1, b.Rect.Height - 1);

            Color c1, c2, border;
            if (b.Disabled) { c1 = Color.FromArgb(239, 239, 239); c2 = Color.FromArgb(239, 239, 239); border = Color.FromArgb(190, 190, 190); }
            else if (b.Back.HasValue)
            {
                c1 = c2 = b.Back.Value;
                if (down) c1 = c2 = ControlPaint.Dark(c1, 0.1f); else if (hot) c1 = c2 = ControlPaint.Light(c1, 0.2f);
                border = Color.FromArgb(118, 118, 118);
            }
            else if (down) { c1 = Color.FromArgb(208, 208, 208); c2 = Color.FromArgb(200, 200, 200); border = Color.FromArgb(70, 70, 70); }
            else if (hot) { c1 = Color.FromArgb(236, 244, 252); c2 = Color.FromArgb(214, 232, 248); border = Color.FromArgb(0, 120, 215); }
            else { c1 = Color.FromArgb(246, 246, 246); c2 = Color.FromArgb(226, 226, 226); border = Color.FromArgb(118, 118, 118); }

            using (GraphicsPath path = RoundRect(r, 3))
            {
                using (Brush fill = new LinearGradientBrush(r, c1, c2, 90f)) g.FillPath(fill, path);
                using (Pen pen = new Pen(border)) g.DrawPath(pen, path);
            }

            RectangleF tr = r;
            if (down) tr.Offset(1, 1);
            using (Brush tb = new SolidBrush(b.Disabled ? Color.FromArgb(130, 130, 130) : b.Color))
                g.DrawString(b.Text, ButtonFont, tb, tr, center);
        }

        // Hit areas follow the real text, line by line. A rectangle as wide as the layout box would also
        // cover the links that follow on the same line, and the first link in the list would win every click.
        private void AddTextHits(Graphics g, StringFormat sf, Box b)
        {
            int added = 0;
            try
            {
                using (StringFormat mf = (StringFormat)sf.Clone())
                {
                    mf.SetMeasurableCharacterRanges(new CharacterRange[] { new CharacterRange(0, b.Text.Length) });
                    Region[] regions = g.MeasureCharacterRanges(b.Text, b.Font, b.Rect, mf);
                    try
                    {
                        if (regions.Length > 0)
                        {
                            using (Matrix m = new Matrix())
                            {
                                foreach (RectangleF r in regions[0].GetRegionScans(m))
                                {
                                    if (r.Width < 1 || r.Height < 1) continue;
                                    hits.Add(new Hit { Rect = r, Box = b });
                                    added++;
                                }
                            }
                        }
                    }
                    finally { foreach (Region r in regions) r.Dispose(); }
                }
            }
            catch (Exception) { }
            if (added == 0) hits.Add(new Hit { Rect = b.Rect, Box = b });   // fallback: whole box
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

            // only what is inside the invalidated area (document coordinates)
            RectangleF vis = new RectangleF(e.ClipRectangle.X - AutoScrollPosition.X, e.ClipRectangle.Y - AutoScrollPosition.Y,
                                            e.ClipRectangle.Width, e.ClipRectangle.Height);

            foreach (Deco d in decos)               // table, row and cell backgrounds and borders
            {
                RectangleF dr = d.Rect;
                dr.Inflate(2, 2);
                if (!dr.IntersectsWith(vis)) continue;
                if (d.Fill.HasValue) using (Brush fb = new SolidBrush(d.Fill.Value)) g.FillRectangle(fb, d.Rect);
                if (d.BorderW > 0)
                    using (Pen pen = new Pen(d.BorderColor, d.BorderW))
                    {
                        if (d.Centered) g.DrawRectangle(pen, d.Rect.X, d.Rect.Y, d.Rect.Width, d.Rect.Height);
                        else
                        {
                            float h = d.BorderW / 2f;
                            g.DrawRectangle(pen, d.Rect.X + h, d.Rect.Y + h, d.Rect.Width - d.BorderW, d.Rect.Height - d.BorderW);
                        }
                    }
            }

            using (StringFormat sf = MakeFormat())
            using (StringFormat center = new StringFormat())
            {
                center.Alignment = StringAlignment.Center;
                center.LineAlignment = StringAlignment.Center;

                for (int i = 0; i < boxes.Count; i++)
                {
                    Box b = boxes[i];
                    switch (b.Kind)
                    {
                        case 0:
                            if (!b.Rect.IntersectsWith(vis)) break;
                            PaintMarker(g, b);
                            PaintSelection(g, i, b);
                            sf.Alignment = b.Align;
                            using (Brush br = new SolidBrush(b.Color)) g.DrawString(b.Text, b.Font, br, b.Rect, sf);
                            break;

                        case 2:
                            if (b.Rect.Y < vis.Top - 1 || b.Rect.Y > vis.Bottom + 1) break;
                            using (Pen pen = new Pen(Color.FromArgb(204, 204, 204)))
                                g.DrawLine(pen, b.Rect.X, b.Rect.Y, b.Rect.Right, b.Rect.Y);
                            break;

                        case 3:
                        {
                            if (!b.Rect.IntersectsWith(vis)) break;
                            PaintMarker(g, b);
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

                        case 5:
                            if (!b.Rect.IntersectsWith(vis)) break;
                            PaintMarker(g, b);
                            PaintButton(g, b, center);
                            break;
                        // video is a child control (NyxPlayer), nothing to draw here
                    }
                }
            }

            PaintInspectHighlight(g);
            g.ResetTransform();
            PaintOverlay(g);                 // link bubble, in window coordinates
        }

        // List markers are drawn as shapes (disc, ring, square) or as a number, to the left of the item.
        private void PaintMarker(Graphics g, Box b)
        {
            if (b.Marker == 0) return;
            float cy = b.Kind == 0 ? b.Rect.Y + b.Font.Height / 2f : b.Rect.Y + 8;
            float mx = b.Left - 16;
            Color c = b.Kind == 0 ? b.Color : Color.Black;
            using (Brush br = new SolidBrush(c))
            using (Pen pen = new Pen(c))
            {
                switch (b.Marker)
                {
                    case 1: g.FillEllipse(br, mx, cy - 3, 6, 6); break;
                    case 2: g.DrawEllipse(pen, mx, cy - 3, 6, 6); break;
                    case 3: g.FillRectangle(br, mx, cy - 3, 6, 6); break;
                    case 4:
                        using (StringFormat f = new StringFormat(StringFormat.GenericTypographic))
                        {
                            f.Alignment = StringAlignment.Far;
                            Font fnt = b.Kind == 0 ? b.Font : Font;
                            g.DrawString(b.Number + ".", fnt, br, new RectangleF(b.Left - 46, b.Rect.Y, 40, fnt.Height + 2), f);
                        }
                        break;
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

        // GDI+ reads PNG, JPEG, GIF (first frame), BMP and TIFF; WebP goes through libwebp; SVG is not supported
        private static Image Decode(byte[] data)
        {
            if (LunaEngine.IsWebP(data)) return LunaEngine.DecodeWebP(data);    // GDI+ cannot read WebP; null when it fails
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

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                lock (qlock) { generation++; queue.Clear(); }
                ClearBoxes();
                if (btnFont != null) btnFont.Dispose();
                if (menu != null) menu.Dispose();
            }
            base.Dispose(disposing);
        }
    }
}
