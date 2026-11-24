using System;
using System.Collections.Generic;
using System.Drawing;
using System.Text;
using System.Windows.Forms;

namespace WindowsFormsApp1
{
    // Mouse, selection, copy, link bubble, context menu and inspect element highlight.
    public partial class LunaCanvas
    {
        private struct Pos
        {
            public int Box, Ch;     // index into boxes, caret offset in that box's text
            public Pos(int box, int ch) { Box = box; Ch = ch; }
            public static int Compare(Pos a, Pos b) { return a.Box != b.Box ? a.Box.CompareTo(b.Box) : a.Ch.CompareTo(b.Ch); }
        }

        private Pos anchor, focus;
        private bool hasSel, pressed, selecting, dragged;
        private Point downPoint, mousePos;
        private Hit downHit;
        private string hoverUrl;
        private Rectangle lastOverlay = Rectangle.Empty;
        private Box inspected;

        private ContextMenuStrip menu;
        private ToolStripMenuItem miCopy, miSelectAll, miCopyLink, miInspect;
        private Box ctxBox;
        private string ctxTarget;

        /// <summary>Element index (or -1 when nothing is under the pointer) chosen with "Inspect element".</summary>
        public event Action<int> InspectRequested;
        /// <summary>Ctrl+Shift+I or F12.</summary>
        public event Action InspectToggled;

        private void InitInput()
        {
            menu = new ContextMenuStrip();
            miCopy = new ToolStripMenuItem("Copy");
            miCopy.ShortcutKeyDisplayString = "Ctrl+C";
            miCopy.Click += (s, e) => CopySelection();
            miSelectAll = new ToolStripMenuItem("Select all");
            miSelectAll.ShortcutKeyDisplayString = "Ctrl+A";
            miSelectAll.Click += (s, e) => SelectAll();
            miCopyLink = new ToolStripMenuItem("Copy link address");
            miCopyLink.Click += (s, e) => SetClipboard(ctxTarget);
            miInspect = new ToolStripMenuItem("Inspect element");
            miInspect.ShortcutKeyDisplayString = "Ctrl+Shift+I";
            miInspect.Click += (s, e) => RequestInspect(ctxBox);

            menu.Items.Add(miCopy);
            menu.Items.Add(miSelectAll);
            menu.Items.Add(new ToolStripSeparator());
            menu.Items.Add(miCopyLink);
            menu.Items.Add(new ToolStripSeparator());
            menu.Items.Add(miInspect);
            menu.Opening += (s, e) =>
            {
                Point p = PointToClient(Cursor.Position);
                Hit h = FindHit(p);
                ctxTarget = h != null ? h.Box.Target : null;
                ctxBox = PickBox(ToDoc(p));
                miCopy.Enabled = hasSel;
                miSelectAll.Enabled = FirstTextBox() >= 0;
                miCopyLink.Enabled = !string.IsNullOrEmpty(ctxTarget);
            };
            ContextMenuStrip = menu;
        }

        private void ResetInput()
        {
            hasSel = false; pressed = false; selecting = false; dragged = false;
            downHit = null; hoverUrl = null; lastOverlay = Rectangle.Empty; inspected = null;
            ctxBox = null; ctxTarget = null;
        }

        private PointF ToDoc(Point client)
        {
            return new PointF(client.X - AutoScrollPosition.X, client.Y - AutoScrollPosition.Y);
        }

        private Hit FindHit(Point client)
        {
            PointF d = ToDoc(client);
            foreach (Hit h in hits) if (h.Rect.Contains(d)) return h;
            return null;
        }

        // ---------------------------------------------------------------- character geometry

        // One rectangle per character in document coordinates. Measured 32 characters at a time (GDI+ limit).
        private RectangleF[] GetChars(Box b)
        {
            if (b.Chars != null) return b.Chars;
            int n = b.Text.Length;
            RectangleF[] rects = new RectangleF[n];
            try
            {
                using (Graphics g = CreateGraphics())
                using (StringFormat sf = MakeFormat())
                {
                    sf.Alignment = b.Align;
                    sf.FormatFlags |= StringFormatFlags.MeasureTrailingSpaces;
                    for (int start = 0; start < n; start += 32)
                    {
                        int cnt = Math.Min(32, n - start);
                        CharacterRange[] ranges = new CharacterRange[cnt];
                        for (int i = 0; i < cnt; i++) ranges[i] = new CharacterRange(start + i, 1);
                        sf.SetMeasurableCharacterRanges(ranges);
                        Region[] rs = g.MeasureCharacterRanges(b.Text, b.Font, b.Rect, sf);
                        for (int i = 0; i < rs.Length && i < cnt; i++)
                        {
                            rects[start + i] = rs[i].GetBounds(g);
                            rs[i].Dispose();
                        }
                    }
                }
            }
            catch (Exception) { }
            b.Chars = rects;
            return rects;
        }

        // caret position (0..length) nearest to the point, with a cost (distance) for comparing boxes
        private int NearestCaret(Box b, PointF p, out float cost)
        {
            RectangleF[] cr = GetChars(b);
            int bestI = -1;
            float best = float.MaxValue;
            for (int i = 0; i < cr.Length; i++)
            {
                RectangleF r = cr[i];
                if (r.Width <= 0 && r.Height <= 0) continue;
                float dy = p.Y < r.Top ? r.Top - p.Y : (p.Y > r.Bottom ? p.Y - r.Bottom : 0);
                float dx = p.X < r.Left ? r.Left - p.X : (p.X > r.Right ? p.X - r.Right : 0);
                float c = dy * 1000f + dx;
                if (c < best) { best = c; bestI = i; }
            }
            cost = best;
            if (bestI < 0) { cost = float.MaxValue; return 0; }
            RectangleF rr = cr[bestI];
            return p.X > rr.Left + rr.Width / 2 ? bestI + 1 : bestI;
        }

        private bool PosFromPoint(PointF p, out Pos pos)
        {
            pos = new Pos(0, 0);
            int best = -1, bestCaret = 0;
            float bestCost = float.MaxValue;
            bool any = false;

            for (int i = 0; i < boxes.Count; i++)
            {
                Box b = boxes[i];
                if (b.Kind != 0) continue;
                if (b.Rect.Bottom < p.Y - 30 || b.Rect.Top > p.Y + 30) continue;
                any = true;
                float cost;
                int caret = NearestCaret(b, p, out cost);
                if (cost < bestCost) { bestCost = cost; best = i; bestCaret = caret; }
            }

            if (!any)   // between blocks, or above/below everything: use the closest text box
            {
                float bd = float.MaxValue;
                int bi = -1;
                for (int i = 0; i < boxes.Count; i++)
                {
                    Box b = boxes[i];
                    if (b.Kind != 0) continue;
                    float d = p.Y < b.Rect.Top ? b.Rect.Top - p.Y : p.Y - b.Rect.Bottom;
                    if (d < bd) { bd = d; bi = i; }
                }
                if (bi < 0) return false;
                float cost;
                bestCaret = NearestCaret(boxes[bi], p, out cost);
                best = bi;
            }

            if (best < 0) return false;
            pos = new Pos(best, bestCaret);
            return true;
        }

        private int FirstTextBox() { for (int i = 0; i < boxes.Count; i++) if (boxes[i].Kind == 0) return i; return -1; }
        private int LastTextBox() { for (int i = boxes.Count - 1; i >= 0; i--) if (boxes[i].Kind == 0) return i; return -1; }

        // ---------------------------------------------------------------- selection and copy

        private void Ordered(out Pos a, out Pos b)
        {
            a = anchor; b = focus;
            if (Pos.Compare(a, b) > 0) { Pos t = a; a = b; b = t; }
        }

        public void SelectAll()
        {
            int f = FirstTextBox(), l = LastTextBox();
            if (f < 0) return;
            anchor = new Pos(f, 0);
            focus = new Pos(l, boxes[l].Text.Length);
            hasSel = true;
            Invalidate();
        }

        private static bool IsWordChar(char c) { return char.IsLetterOrDigit(c) || c == '_'; }

        private void SelectWordAt(Pos p)
        {
            string t = boxes[p.Box].Text;
            if (t.Length == 0) return;
            int i = Math.Min(p.Ch, t.Length - 1);
            int s = i, e = i + 1;
            if (IsWordChar(t[i]))
            {
                while (s > 0 && IsWordChar(t[s - 1])) s--;
                while (e < t.Length && IsWordChar(t[e])) e++;
            }
            anchor = new Pos(p.Box, s);
            focus = new Pos(p.Box, e);
            hasSel = true;
            Invalidate();
        }

        private void SelectBoxAt(Pos p)
        {
            anchor = new Pos(p.Box, 0);
            focus = new Pos(p.Box, boxes[p.Box].Text.Length);
            hasSel = true;
            Invalidate();
        }

        /// <summary>The selected text, with line breaks between blocks and spaces between runs on one line.</summary>
        public string SelectedText()
        {
            if (!hasSel) return "";
            Pos a, b;
            Ordered(out a, out b);
            StringBuilder sb = new StringBuilder();
            for (int i = a.Box; i <= b.Box && i < boxes.Count; i++)
            {
                Box bx = boxes[i];
                if (bx.Kind == 1 || bx.Kind == 2)
                {
                    if (sb.Length > 0 && sb[sb.Length - 1] != '\n') sb.Append('\n');
                    continue;
                }
                if (bx.Kind != 0) continue;
                int s = i == a.Box ? a.Ch : 0;
                int e = i == b.Box ? b.Ch : bx.Text.Length;
                if (e <= s) continue;
                if (sb.Length > 0)
                {
                    char last = sb[sb.Length - 1];
                    if (bx.Block) { if (last != '\n') sb.Append('\n'); }
                    else if (!char.IsWhiteSpace(last)) sb.Append(' ');
                }
                sb.Append(bx.Text, s, e - s);
            }
            return sb.ToString();
        }

        public void CopySelection()
        {
            string t = SelectedText();
            if (t.Length > 0) SetClipboard(t);
        }

        private static void SetClipboard(string text)
        {
            if (string.IsNullOrEmpty(text)) return;
            try { Clipboard.SetText(text); }
            catch (Exception) { }      // the clipboard can be locked by another program
        }

        private void PaintSelection(Graphics g, int i, Box b)
        {
            if (!hasSel) return;
            Pos a, f;
            Ordered(out a, out f);
            if (i < a.Box || i > f.Box) return;
            int s = i == a.Box ? a.Ch : 0;
            int e = i == f.Box ? f.Ch : b.Text.Length;
            if (e <= s) return;

            RectangleF[] cr = GetChars(b);
            using (Brush br = new SolidBrush(Color.FromArgb(110, 51, 144, 255)))
            {
                RectangleF run = RectangleF.Empty;
                bool open = false;
                for (int k = s; k < e && k < cr.Length; k++)
                {
                    RectangleF r = cr[k];
                    if (r.Width <= 0 && r.Height <= 0) continue;
                    if (open && Math.Abs(r.Top - run.Top) < 3) run = RectangleF.Union(run, r);   // same line: one band
                    else { if (open) g.FillRectangle(br, run); run = r; open = true; }
                }
                if (open) g.FillRectangle(br, run);
            }
        }

        // ---------------------------------------------------------------- mouse

        protected override void OnMouseDown(MouseEventArgs e)
        {
            base.OnMouseDown(e);
            Focus();
            if (e.Button != MouseButtons.Left) return;

            pressed = true; dragged = false; selecting = false;
            downPoint = e.Location;
            downHit = FindHit(e.Location);

            Pos pos;
            bool ok = PosFromPoint(ToDoc(e.Location), out pos);
            if (e.Clicks == 2 && ok) { SelectWordAt(pos); downHit = null; return; }
            if (e.Clicks >= 3 && ok) { SelectBoxAt(pos); downHit = null; return; }

            bool had = hasSel;
            hasSel = false;
            if (ok) { anchor = pos; focus = pos; selecting = true; }
            if (had) Invalidate();
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e);
            mousePos = e.Location;

            if (pressed && selecting)
            {
                if (!dragged && (Math.Abs(e.X - downPoint.X) > 3 || Math.Abs(e.Y - downPoint.Y) > 3)) dragged = true;
                if (dragged)
                {
                    Pos pos;
                    if (PosFromPoint(ToDoc(e.Location), out pos))
                    {
                        focus = pos;
                        hasSel = Pos.Compare(anchor, focus) != 0;
                    }
                    int dy = e.Y < 0 ? e.Y : (e.Y > ClientSize.Height ? e.Y - ClientSize.Height : 0);
                    if (dy != 0)    // dragging past the edge scrolls
                    {
                        int step = Math.Max(-60, Math.Min(60, dy));
                        AutoScrollPosition = new Point(-AutoScrollPosition.X, -AutoScrollPosition.Y + step);
                    }
                    Invalidate();
                }
            }
            UpdateHover(e.Location);
        }

        protected override void OnMouseUp(MouseEventArgs e)
        {
            base.OnMouseUp(e);
            if (e.Button != MouseButtons.Left) return;
            bool was = pressed;
            pressed = false; selecting = false;
            if (!was || dragged) return;                 // a drag selects text, it does not follow links

            Hit h = FindHit(e.Location);
            if (h != null && downHit != null && h.Box.Link == downHit.Box.Link && LinkClicked != null)
                LinkClicked(h.Box.Link);                 // pressed and released on the same link
        }

        protected override void OnMouseLeave(EventArgs e)
        {
            base.OnMouseLeave(e);
            hoverUrl = null;
            Cursor = Cursors.Default;
            RefreshOverlay();
        }

        protected override void OnMouseWheel(MouseEventArgs e)
        {
            base.OnMouseWheel(e);
            UpdateHover(PointToClient(Cursor.Position));
        }

        protected override void OnScroll(ScrollEventArgs se)
        {
            base.OnScroll(se);
            UpdateHover(PointToClient(Cursor.Position));
        }

        private bool OverText(Point client)
        {
            PointF d = ToDoc(client);
            foreach (Box b in boxes)
                if (b.Kind == 0 && b.Ink.Contains(d)) return true;
            return false;
        }

        private void UpdateHover(Point client)
        {
            mousePos = client;
            Hit h = FindHit(client);
            hoverUrl = h != null ? h.Box.Target : null;
            Cursor = h != null ? Cursors.Hand : (OverText(client) ? Cursors.IBeam : Cursors.Default);
            RefreshOverlay();
        }

        // ---------------------------------------------------------------- link bubble

        // Bottom-left corner; jumps to the bottom-right when the pointer is over it.
        private Rectangle ComputeOverlay(Graphics g, string url)
        {
            if (url == null) return Rectangle.Empty;
            int maxW = Math.Max(200, (int)(ClientSize.Width * 0.6));
            using (StringFormat f = new StringFormat(StringFormatFlags.NoWrap))
            {
                SizeF sz = g.MeasureString(url, Font, 10000, f);
                int w = Math.Min(maxW, (int)Math.Ceiling(sz.Width) + 14);
                int h = (int)Math.Ceiling(sz.Height) + 8;
                int y = ClientSize.Height - h;
                Rectangle left = new Rectangle(0, y, w, h);
                if (left.Contains(mousePos)) return new Rectangle(ClientSize.Width - w, y, w, h);
                return left;
            }
        }

        private void RefreshOverlay()
        {
            Rectangle now;
            using (Graphics g = CreateGraphics()) now = ComputeOverlay(g, hoverUrl);
            if (now == lastOverlay) return;
            if (!lastOverlay.IsEmpty) Invalidate(lastOverlay);
            if (!now.IsEmpty) Invalidate(now);
            lastOverlay = now;
        }

        private void PaintOverlay(Graphics g)
        {
            if (hoverUrl == null) { lastOverlay = Rectangle.Empty; return; }
            Rectangle r = ComputeOverlay(g, hoverUrl);
            lastOverlay = r;
            using (Brush back = new SolidBrush(Color.FromArgb(241, 243, 244))) g.FillRectangle(back, r);
            using (Pen pen = new Pen(Color.FromArgb(173, 176, 180))) g.DrawRectangle(pen, r.X, r.Y, r.Width - 1, r.Height - 1);
            using (StringFormat f = new StringFormat())
            {
                f.FormatFlags = StringFormatFlags.NoWrap;
                f.Trimming = StringTrimming.EllipsisPath;
                f.LineAlignment = StringAlignment.Center;
                RectangleF tr = new RectangleF(r.X + 7, r.Y, r.Width - 14, r.Height);
                using (Brush tb = new SolidBrush(Color.FromArgb(32, 33, 36))) g.DrawString(hoverUrl, Font, tb, tr, f);
            }
        }

        // ---------------------------------------------------------------- inspect element

        // image/video by rectangle, text by the area it covers
        private Box PickBox(PointF d)
        {
            for (int i = boxes.Count - 1; i >= 0; i--)
            {
                Box b = boxes[i];
                if ((b.Kind == 3 || b.Kind == 4) && b.Rect.Contains(d)) return b;
            }
            foreach (Box b in boxes)
            {
                if (b.Kind != 0) continue;
                RectangleF r = b.Ink;
                r.Inflate(2, 2);
                if (r.Contains(d)) return b;
            }
            return null;
        }

        private void RequestInspect(Box b)
        {
            inspected = b;
            Invalidate();
            if (InspectRequested != null) InspectRequested(b != null ? b.Index : -1);
        }

        /// <summary>Outline the element with this index (-1 clears it).</summary>
        public void HighlightElement(int elementIndex)
        {
            inspected = null;
            if (elementIndex >= 0)
                foreach (Box b in boxes) if (b.Index == elementIndex) { inspected = b; break; }
            Invalidate();
        }

        private List<RectangleF> LineRects(Box b)
        {
            List<RectangleF> list = new List<RectangleF>();
            if (b.Kind == 0)
            {
                RectangleF[] cr = GetChars(b);
                RectangleF run = RectangleF.Empty;
                bool open = false;
                foreach (RectangleF r in cr)
                {
                    if (r.Width <= 0 && r.Height <= 0) continue;
                    if (open && Math.Abs(r.Top - run.Top) < 3) run = RectangleF.Union(run, r);
                    else { if (open) list.Add(run); run = r; open = true; }
                }
                if (open) list.Add(run);
            }
            if (list.Count == 0) list.Add(b.Kind == 0 ? b.Ink : b.Rect);
            return list;
        }

        private void PaintInspectHighlight(Graphics g)
        {
            if (inspected == null) return;
            List<RectangleF> rects = LineRects(inspected);
            RectangleF all = rects[0];
            using (Brush fill = new SolidBrush(Color.FromArgb(70, 111, 168, 220)))
            using (Pen pen = new Pen(Color.FromArgb(220, 66, 133, 244)))
            {
                foreach (RectangleF r in rects)
                {
                    g.FillRectangle(fill, r);
                    g.DrawRectangle(pen, r.X, r.Y, r.Width, r.Height);
                    all = RectangleF.Union(all, r);
                }
            }

            // label above the element: <tag>  width x height
            string label = "<" + inspected.Tag + ">  " + (int)all.Width + " x " + (int)all.Height;
            using (Font lf = new Font("Segoe UI", 8f))
            {
                SizeF ls = g.MeasureString(label, lf);
                float ly = all.Top - ls.Height - 4 < 0 ? all.Bottom + 2 : all.Top - ls.Height - 4;
                using (Brush lb = new SolidBrush(Color.FromArgb(235, 40, 44, 52))) g.FillRectangle(lb, all.Left, ly, ls.Width + 6, ls.Height + 2);
                g.DrawString(label, lf, Brushes.White, all.Left + 3, ly + 1);
            }
        }

        // ---------------------------------------------------------------- keys and fragments

        protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
        {
            if (keyData == (Keys.Control | Keys.C) || keyData == (Keys.Control | Keys.Insert)) { CopySelection(); return true; }
            if (keyData == (Keys.Control | Keys.A)) { SelectAll(); return true; }
            if (keyData == (Keys.Control | Keys.Shift | Keys.I) || keyData == Keys.F12)
            {
                if (InspectToggled != null) InspectToggled();
                return true;
            }
            return base.ProcessCmdKey(ref msg, keyData);
        }

        /// <summary>#fragment links: scroll to the element with that id (or to the top for "" and "top").</summary>
        public void ScrollToFragment(string fragment)
        {
            try { fragment = Uri.UnescapeDataString(fragment); } catch (Exception) { }
            int y = -1;
            if (fragment.Length > 0)
            {
                foreach (Box b in boxes)
                    if (b.Kind == 0 && b.Id == fragment) { y = Math.Max(0, (int)b.Rect.Y - 10); break; }
            }
            if (y < 0 && (fragment.Length == 0 || fragment.Equals("top", StringComparison.OrdinalIgnoreCase))) y = 0;
            if (y < 0) return;
            AutoScrollPosition = new Point(0, y);
            Invalidate();
        }
    }
}
