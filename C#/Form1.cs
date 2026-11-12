using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Runtime.InteropServices;
using Microsoft.Win32;
using Microsoft.Web.WebView2.WinForms;

namespace WindowsFormsApp1
{
    public partial class Form1 : Form
    {
        public Form1()
        {
            InitializeComponent();
            Load += Form1_Load;
            FormClosed += (s, e) => SystemEvents.UserPreferenceChanged -= OnUserPreferenceChanged;
            textBox1.KeyDown += textBox1_KeyDown;

            buttonBack.Click += (s, e) => { var w = Current; if (w != null && w.CanGoBack) w.GoBack(); };
            buttonForward.Click += (s, e) => { var w = Current; if (w != null && w.CanGoForward) w.GoForward(); };
            buttonRefresh.Click += (s, e) => { var w = Current; if (w != null && w.CoreWebView2 != null) w.Reload(); };
            buttonCloseTab.Click += (s, e) => CloseTab(tabControl1.SelectedIndex);

            foreach (Button b in new Button[] { buttonBack, buttonForward, buttonRefresh, buttonCloseTab })
            {
                b.TabStop = false;
                b.GotFocus += (s, e) => { var w = Current; if (w != null) w.Focus(); };
            }

            tabControl1.TabPages.Clear();
            tabControl1.TabPages.Add(new TabPage("+"));

            tabControl1.Selecting += (s, e) =>
            {
                if (e.TabPage != null && e.TabPage.Tag == null)
                {
                    e.Cancel = true;
                    BeginInvoke(new Action(() => AddTab(Home)));
                }
            };

            tabControl1.SelectedIndexChanged += (s, e) =>
            {
                var wv = Current;
                if (wv == null || wv.CoreWebView2 == null) return;
                textBox1.Text = wv.CoreWebView2.Source;
                UpdateNavButtons();
            };

            tabControl1.MouseUp += (s, e) =>
            {
                if (e.Button != MouseButtons.Middle) return;
                for (int i = 0; i < tabControl1.TabPages.Count - 1; i++)
                    if (tabControl1.GetTabRect(i).Contains(e.Location)) { CloseTab(i); break; }
            };
        }

        private void Form1_Load(object sender, EventArgs e)
        {
            ApplyTheme();
            SystemEvents.UserPreferenceChanged += OnUserPreferenceChanged;
            AddTab(Home);
        }

        private const string Home = "https://noai.duckduckgo.com";

        private WebView2 Current
        {
            get
            {
                var p = tabControl1.SelectedTab;
                return p == null ? null : p.Tag as WebView2;
            }
        }

        private async void AddTab(string url)
        {
            var wv = new WebView2 { Dock = DockStyle.Fill };
            var page = new TabPage("New Tab") { Tag = wv };
            page.Controls.Add(wv);

            tabControl1.TabPages.Insert(tabControl1.TabPages.Count - 1, page);
            tabControl1.SelectedTab = page;

            await wv.EnsureCoreWebView2Async();

            wv.CoreWebView2.SourceChanged += (s, e) =>
            {
                if (wv == Current) textBox1.Text = wv.CoreWebView2.Source;
            };
            wv.CoreWebView2.DocumentTitleChanged += (s, e) =>
            {
                string t = wv.CoreWebView2.DocumentTitle;
                page.Text = t.Length > 20 ? t.Substring(0, 20) : t;
            };
            wv.CoreWebView2.HistoryChanged += (s, e) =>
            {
                if (wv == Current) UpdateNavButtons();
            };
            wv.CoreWebView2.NewWindowRequested += (s, e) =>
            {
                e.Handled = true;
                AddTab(e.Uri);
            };

            wv.CoreWebView2.Navigate(url);
        }

        private void CloseTab(int i)
        {
            if (tabControl1.TabPages.Count <= 2) { Close(); return; }

            var page = tabControl1.TabPages[i];
            if (tabControl1.SelectedTab == page)
                tabControl1.SelectedIndex = i > 0 ? i - 1 : 1;

            tabControl1.TabPages.Remove(page);
            var wv = page.Tag as WebView2;
            if (wv != null) wv.Dispose();
            page.Dispose();
        }

        // TextBox -> WebView2 (manual typing, press Enter)
        private void textBox1_KeyDown(object sender, KeyEventArgs e)
        {
            if (e.KeyCode != Keys.Enter) return;
            e.SuppressKeyPress = true;

            var wv = Current;
            if (wv == null || wv.CoreWebView2 == null) return;

            string input = textBox1.Text.Trim();
            if (input.Length == 0) return;

            wv.CoreWebView2.Navigate(ResolveInput(input));
        }

        private static string ResolveInput(string input)
        {
            // Already has a scheme (http://, https://, file://, etc.)
            if (input.Contains("://"))
            {
                Uri u;
                if (Uri.TryCreate(input, UriKind.Absolute, out u))
                    return u.AbsoluteUri;
            }
            else if (!input.Contains(" "))
            {
                // Looks like a host: has a dot (example.com, example.com/path),
                // or is localhost, or is an IPv4 address with optional port
                string host = input.Split('/', '?', '#')[0];
                bool looksLikeHost =
                    host.Contains(".") ||
                    host.StartsWith("localhost", StringComparison.OrdinalIgnoreCase);

                Uri u;
                if (looksLikeHost && Uri.TryCreate("https://" + input, UriKind.Absolute, out u))
                    return u.AbsoluteUri;
            }

            // Everything else is a search query
            return "https://noai.duckduckgo.com/?q=" + Uri.EscapeDataString(input) + "&ia=web";
        }
        [DllImport("dwmapi.dll")]
        private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);

        private static bool IsDarkMode()
        {
            object v = Registry.GetValue(
                @"HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize",
                "AppsUseLightTheme", 1);
            return v is int && (int)v == 0;
        }

        private void OnUserPreferenceChanged(object sender, UserPreferenceChangedEventArgs e)
        {
            if (e.Category == UserPreferenceCategory.General)
                BeginInvoke(new Action(ApplyTheme));
        }

        private void ApplyTheme()
        {
            bool dark = IsDarkMode();

            int on = dark ? 1 : 0;
            if (DwmSetWindowAttribute(Handle, 20, ref on, 4) != 0)
                DwmSetWindowAttribute(Handle, 19, ref on, 4);

            Color back = dark ? Color.FromArgb(32, 32, 32) : SystemColors.Control;
            Color fore = dark ? Color.White : SystemColors.ControlText;
            Color box = dark ? Color.FromArgb(45, 45, 45) : SystemColors.Window;
            Color border = dark ? Color.FromArgb(85, 85, 85) : Color.FromArgb(173, 173, 173);
            Color hover = dark ? Color.FromArgb(62, 62, 62) : Color.FromArgb(229, 241, 251);
            Color pressed = dark ? Color.FromArgb(80, 80, 80) : Color.FromArgb(204, 228, 247);

            ColorControls(this, back, fore, box, border, hover, pressed);

            if (Current != null && Current.CoreWebView2 != null)
                UpdateNavButtons();

            bool d = dark;
            tabControl1.StripBack = back;
            tabControl1.TabBack = d ? Color.FromArgb(45, 45, 45) : SystemColors.ControlLight;
            tabControl1.SelBack = d ? Color.FromArgb(70, 70, 70) : SystemColors.Window;
            tabControl1.TabFore = fore;
            tabControl1.Invalidate();
            foreach (TabPage p in tabControl1.TabPages) p.BackColor = back;
        }

        private static void ColorControls(Control c, Color back, Color fore, Color box,
                                          Color border, Color hover, Color pressed)
        {
            if (c is WebView2) return;

            if (c is ButtonBase)
            {
                var b = (ButtonBase)c;
                b.FlatStyle = FlatStyle.Flat;
                b.BackColor = box;
                b.ForeColor = fore;
                b.FlatAppearance.BorderColor = border;
                b.FlatAppearance.BorderSize = 1;
                b.FlatAppearance.MouseOverBackColor = hover;
                b.FlatAppearance.MouseDownBackColor = pressed;
            }
            else
            {
                c.BackColor = c is TextBoxBase ? box : back;
                c.ForeColor = fore;
            }

            foreach (Control child in c.Controls)
                ColorControls(child, back, fore, box, border, hover, pressed);
        }
        private void UpdateNavButtons()
        {
            var wv = Current;
            if (wv == null) return;

            bool dark = IsDarkMode();
            Color fore = dark ? Color.White : SystemColors.ControlText;
            Color dim = dark ? Color.FromArgb(120, 120, 120) : SystemColors.GrayText;
            Color box = dark ? Color.FromArgb(45, 45, 45) : SystemColors.Window;
            Color hover = dark ? Color.FromArgb(62, 62, 62) : Color.FromArgb(229, 241, 251);
            Color pressed = dark ? Color.FromArgb(80, 80, 80) : Color.FromArgb(204, 228, 247);

            SetNavButton(buttonBack, wv.CanGoBack, fore, dim, box, hover, pressed);
            SetNavButton(buttonForward, wv.CanGoForward, fore, dim, box, hover, pressed);
        }

        private static void SetNavButton(Button b, bool active, Color fore, Color dim,
                                         Color box, Color hover, Color pressed)
        {
            b.ForeColor = active ? fore : dim;
            b.FlatAppearance.MouseOverBackColor = active ? hover : box;
            b.FlatAppearance.MouseDownBackColor = active ? pressed : box;
        }
    }
    public class ThemedTabControl : TabControl
    {
        public Color StripBack = SystemColors.Control;
        public Color TabBack = SystemColors.ControlLight;
        public Color SelBack = SystemColors.Window;
        public Color TabFore = SystemColors.ControlText;

        public ThemedTabControl()
        {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
                     ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            ItemSize = new Size(150, 26);
            SizeMode = TabSizeMode.Fixed;
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            using (var br = new SolidBrush(StripBack))
                e.Graphics.FillRectangle(br, ClientRectangle);

            for (int i = 0; i < TabCount; i++)
            {
                Rectangle r = GetTabRect(i);
                bool sel = i == SelectedIndex;

                using (var br = new SolidBrush(sel ? SelBack : TabBack))
                    e.Graphics.FillRectangle(br, r.X, r.Y, r.Width - 1, r.Height);

                TextRenderer.DrawText(e.Graphics, TabPages[i].Text, Font, r, TabFore,
                    TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter |
                    TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine);
            }
        }
    }
}
