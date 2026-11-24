using System;
using System.Drawing;
using System.Threading.Tasks;
using System.Windows.Forms;
using Tethys.Luna;

namespace WindowsFormsApp1
{
    /// <summary>Inspect element panel: a read-only text view with a close button.</summary>
    internal sealed class LunaInspector : Panel
    {
        private readonly TextBox text = new TextBox();
        public event Action CloseClicked;

        public LunaInspector()
        {
            Height = 240;
            BackColor = Color.FromArgb(30, 30, 30);

            Panel header = new Panel();
            header.Dock = DockStyle.Top;
            header.Height = 24;
            header.BackColor = Color.FromArgb(45, 45, 48);

            Label title = new Label();
            title.Text = "Inspect Element";
            title.ForeColor = Color.FromArgb(220, 220, 220);
            title.Dock = DockStyle.Fill;
            title.TextAlign = ContentAlignment.MiddleLeft;
            title.Padding = new Padding(6, 0, 0, 0);

            Button close = new Button();
            close.Text = "Close";
            close.Dock = DockStyle.Right;
            close.Width = 60;
            close.FlatStyle = FlatStyle.Flat;
            close.ForeColor = Color.FromArgb(220, 220, 220);
            close.BackColor = Color.FromArgb(45, 45, 48);
            close.FlatAppearance.BorderSize = 0;
            close.TabStop = false;
            close.Click += (s, e) => { if (CloseClicked != null) CloseClicked(); };

            header.Controls.Add(title);     // Fill first, then the Right button
            header.Controls.Add(close);

            text.Multiline = true;
            text.ReadOnly = true;
            text.WordWrap = false;
            text.ScrollBars = ScrollBars.Both;
            text.BorderStyle = BorderStyle.None;
            text.BackColor = Color.FromArgb(30, 30, 30);
            text.ForeColor = Color.FromArgb(212, 212, 212);
            text.Font = new Font("Consolas", 9f);
            text.Dock = DockStyle.Fill;

            Controls.Add(text);             // Fill first, then the Top header
            Controls.Add(header);
        }

        public void SetText(string s)
        {
            text.Text = (s ?? "").Replace("\r\n", "\n").Replace("\n", "\r\n");
            text.SelectionStart = 0;
            text.ScrollToCaret();
        }
    }

    /// <summary>
    /// The Luna page view: a LunaCanvas (the page) with the inspect element panel under it.
    /// Same API as before, so the form does not change.
    /// </summary>
    public class LunaView : Panel
    {
        private readonly LunaCanvas canvas;
        private readonly LunaInspector inspector;
        private int lastElement = -1;

        /// <summary>Report for one element index; null while the page is loading. Set by LunaTab.</summary>
        public Func<int, string> InspectProvider;
        /// <summary>Stylesheet report for the page; null while the page is loading. Set by LunaTab.</summary>
        public Func<string> PageReportProvider;

        public event Action<string> LinkClicked
        {
            add { canvas.LinkClicked += value; }
            remove { canvas.LinkClicked -= value; }
        }

        public LunaView(LunaEngine engine)
        {
            canvas = new LunaCanvas(engine);
            canvas.Dock = DockStyle.Fill;

            inspector = new LunaInspector();
            inspector.Dock = DockStyle.Bottom;
            inspector.Visible = false;
            inspector.CloseClicked += HideInspector;

            canvas.InspectRequested += idx => { lastElement = idx; ShowInspector(idx); };
            canvas.InspectToggled += () =>
            {
                if (inspector.Visible) HideInspector(); else ShowInspector(lastElement);
            };

            Controls.Add(canvas);       // Fill first, then the Bottom panel
            Controls.Add(inspector);
        }

        public void SetPage(LunaPage p)
        {
            canvas.SetPage(p);
            lastElement = -1;
            if (inspector.Visible) ShowInspector(-1);
        }

        public void ScrollToFragment(string fragment) { canvas.ScrollToFragment(fragment); }
        public void CopySelection() { canvas.CopySelection(); }
        public void SelectAll() { canvas.SelectAll(); }
        public string SelectedText { get { return canvas.SelectedText(); } }

        /// <summary>Gives the focus to the page surface (keyboard shortcuts such as Ctrl+C go there).</summary>
        public new bool Focus() { return canvas.Focus(); }

        private void ShowInspector(int elementIndex)
        {
            string el = null;
            if (elementIndex >= 0 && InspectProvider != null) el = InspectProvider(elementIndex);
            string page = PageReportProvider != null ? PageReportProvider() : null;

            string head;
            if (elementIndex < 0) head = "No element selected. Right-click an element and choose Inspect element.\n";
            else head = el ?? "The page is still loading.\n";

            inspector.SetText(head + "\n------------------------------------------------\n" + (page ?? ""));
            inspector.Visible = true;
            canvas.HighlightElement(elementIndex);
        }

        private void HideInspector()
        {
            inspector.Visible = false;
            canvas.HighlightElement(-1);
        }
    }

    /// <summary>One Luna tab: a LunaPage plus its LunaView. Loads run on a worker thread.</summary>
    public sealed class LunaTab : IDisposable
    {
        public readonly LunaView View;
        private readonly LunaPage page;
        private bool busy, disposeWanted;
        private Func<int> pending;                // last navigation requested while a load was running

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
            View.LinkClicked += href =>
            {
                if (href.StartsWith("#")) { View.ScrollToFragment(href.Substring(1)); return; }   // same page, no reload
                Run(() => page.ClickLink(href));
            };
            View.InspectProvider = index => busy ? null : page.Inspect(index);
            View.PageReportProvider = () => busy ? null : page.StyleReport;
        }

        public void Load(string url) { Run(() => page.Load(url)); }
        public void Back() { Run(() => page.Back()); }
        public void Forward() { Run(() => page.Forward()); }
        public void Refresh() { Run(() => page.Refresh()); }

        private async void Run(Func<int> action)
        {
            if (disposeWanted) return;
            if (busy) { pending = action; return; }      // the last click wins and runs when this load finishes
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

            Func<int> next = pending;
            pending = null;
            if (next != null) Run(next);
        }

        public void Dispose()
        {
            if (busy) disposeWanted = true;     // the worker still uses the native page
            else page.Dispose();
            View.Dispose();
        }
    }
}
