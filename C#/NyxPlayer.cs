// NyxPlayer.cs - C# port of npl.py (Qt NyxPlayer). Video surface plus play/pause/replay button and a seek slider.
// Uses the WPF MediaElement through ElementHost, so it plays what the Windows media engine can decode
// (MP4/H.264, WMV, AVI and similar; not WebM).
using System;
using System.Diagnostics;
using System.Drawing;
using System.Windows.Forms;
using System.Windows.Forms.Integration;

namespace WindowsFormsApp1
{
    public sealed class NyxPlayer : UserControl
    {
        private static readonly Color BarBack = Color.FromArgb(0x1a, 0x1d, 0x23);
        private static readonly Color BtnFore = Color.FromArgb(0x00, 0xff, 0x7f);
        private static readonly Color BtnBorder = Color.FromArgb(0x2d, 0x5a, 0x27);

        private readonly ElementHost host;
        private readonly System.Windows.Controls.MediaElement media;
        private readonly Button playBtn;
        private readonly TrackBar slider;
        private readonly Timer timer;
        private readonly ToolTip tip = new ToolTip();
        private bool playing, ended, failed, seeking, loaded;
        private Uri pending;

        public NyxPlayer()
        {
            BackColor = Color.Black;

            media = new System.Windows.Controls.MediaElement();
            media.LoadedBehavior = System.Windows.Controls.MediaState.Manual;
            media.UnloadedBehavior = System.Windows.Controls.MediaState.Manual;
            media.Stretch = System.Windows.Media.Stretch.Uniform;
            media.Volume = 0.7;                       // not muted by default
            media.Loaded += (s, e) => { loaded = true; StartOpen(); };   // the media pipeline only exists once the element is in a window
            media.MediaOpened += (s, e) => OnOpened();
            media.MediaEnded += (s, e) => OnEnded();
            media.MediaFailed += (s, e) => Fail(e.ErrorException != null ? e.ErrorException.Message : "Unknown media error");

            host = new ElementHost();
            host.Dock = DockStyle.Fill;
            host.Child = media;

            Panel bar = new Panel();
            bar.Dock = DockStyle.Bottom;
            bar.Height = 28;
            bar.BackColor = BarBack;

            playBtn = new Button();
            playBtn.Text = "Play";
            playBtn.Dock = DockStyle.Left;
            playBtn.Width = 70;
            playBtn.FlatStyle = FlatStyle.Flat;
            playBtn.BackColor = BarBack;
            playBtn.ForeColor = BtnFore;
            playBtn.FlatAppearance.BorderColor = BtnBorder;
            playBtn.TabStop = false;
            playBtn.Click += (s, e) => TogglePlay();

            slider = new TrackBar();
            slider.Dock = DockStyle.Fill;
            slider.AutoSize = false;
            slider.Minimum = 0;
            slider.Maximum = 1;
            slider.TickStyle = TickStyle.None;
            slider.BackColor = BarBack;
            slider.Scroll += (s, e) => SetPosition(slider.Value);
            slider.MouseDown += (s, e) => seeking = true;
            slider.MouseUp += (s, e) => seeking = false;

            bar.Controls.Add(slider);      // Fill first, then the Left button
            bar.Controls.Add(playBtn);
            Controls.Add(host);            // Fill first, then the Bottom bar
            Controls.Add(bar);

            timer = new Timer();
            timer.Interval = 250;
            timer.Tick += (s, e) => UpdateSlider();
            timer.Start();
        }

        public void Open(string url)
        {
            Uri u;
            if (!Uri.TryCreate(url, UriKind.Absolute, out u)) { Fail("Invalid video URL"); return; }
            pending = u;
            if (loaded) StartOpen();       // otherwise the Loaded event starts it
        }

        private void StartOpen()
        {
            if (pending == null || failed) return;
            Uri u = pending;
            pending = null;
            try
            {
                Debug.WriteLine("NyxPlayer Loading: " + u);
                media.Source = u;
                media.Pause();             // load the media and show the first frame without playing
            }
            catch (Exception ex) { Fail(ex.Message); }
        }

        private void TogglePlay()
        {
            if (failed) return;
            try { Toggle(); }
            catch (Exception ex) { Fail(ex.Message); }
        }

        private void Toggle()
        {
            if (ended)
            {
                media.Position = TimeSpan.Zero;     // finished: restart from the beginning
                media.Play();
                ended = false; playing = true;
                playBtn.Text = "Pause";
            }
            else if (playing)
            {
                media.Pause();
                playing = false;
                playBtn.Text = "Play";
            }
            else
            {
                media.Play();
                playing = true;
                playBtn.Text = "Pause";
            }
        }

        private void SetPosition(int ms)
        {
            if (failed) return;
            try { media.Position = TimeSpan.FromMilliseconds(ms); }   // scrubbing
            catch (Exception ex) { Fail(ex.Message); }
        }

        private void UpdateSlider()
        {
            if (seeking || failed) return;
            try
            {
                int v = (int)Math.Max(0, Math.Min(slider.Maximum, media.Position.TotalMilliseconds));
                if (v != slider.Value) slider.Value = v;
            }
            catch (Exception) { }
        }

        private void OnOpened()
        {
            Debug.WriteLine("NyxPlayer Status: Loaded");
            if (media.NaturalDuration.HasTimeSpan)
            {
                double ms = media.NaturalDuration.TimeSpan.TotalMilliseconds;
                slider.Maximum = (int)Math.Max(1, Math.Min(int.MaxValue, ms));   // slider range is the video length
            }
        }

        private void OnEnded()
        {
            Debug.WriteLine("NyxPlayer Status: EndOfMedia");
            ended = true; playing = false;
            playBtn.Text = "Replay";
        }

        private void Fail(string message)
        {
            Debug.WriteLine("NyxPlayer Error: " + message);
            failed = true; playing = false;
            playBtn.Text = "Error";
            tip.SetToolTip(playBtn, message);
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                timer.Stop();
                timer.Dispose();
                try { media.Close(); } catch (Exception) { }
                tip.Dispose();
            }
            base.Dispose(disposing);
        }
    }
}