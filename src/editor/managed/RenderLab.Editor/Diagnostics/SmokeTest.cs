using System.Diagnostics;
using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Threading;
using RenderLab.Editor.Controls;
using RenderLab.Editor.Views;
using RenderLab.Editor.Configuration;
using RenderLab.Editor.Interop;

namespace RenderLab.Editor.Diagnostics;

internal sealed class SmokeTest
{
    private readonly MainWindow window;
    private readonly EditorOptions options;
    private readonly List<string> checks = [];
    private readonly Stopwatch elapsed = Stopwatch.StartNew();
    private readonly DispatcherTimer watchdog = new() { Interval = TimeSpan.FromSeconds(120) };
    private int stage, ticks;
    private ulong oldHandle, savedFrames, savedResizes, savedPaused, savedInput;
    private bool completed;
    private RenderViewportHost? detachedHost;

    internal SmokeTest(MainWindow window, EditorOptions options)
    {
        this.window = window;
        this.options = options;
        window.FrameCompleted += Update;
        watchdog.Tick += (_, _) => Fail(new TimeoutException("Viewport smoke stopped making progress."));
        watchdog.Start();
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }

    private void CheckPixels(ViewportStats stats)
    {
        Require(Math.Abs(stats.Width - window.Viewport.Bounds.Width * window.RenderScaling) <= 2 &&
            Math.Abs(stats.Height - window.Viewport.Bounds.Height * window.RenderScaling) <= 2,
            "Native client size does not match Avalonia DIP-to-pixel layout.");
    }

    private void Update(ViewportStats stats)
    {
        if (completed)
        {
            return;
        }
        try
        {
            Require(elapsed.Elapsed < TimeSpan.FromSeconds(120), "Viewport smoke timed out.");
            if (++ticks < 8 || stats.Frames < 8)
            {
                return;
            }
            ticks = 0;
            var host = window.Viewport;
            var root = window.TryGetPlatformHandle()!.Handle;
            switch (stage++)
            {
                case 0:
                    Require(stats.ValidationEnabled == (options.Validation ? 1u : 0u) &&
                        stats.ProfilingEnabled == (options.Profiling ? 1u : 0u), "Creation settings were not applied.");
                    Require(NativeMethods.IsChild(root, host.Child), "Viewport is not embedded in the Avalonia window.");
                    CheckPixels(stats);
                    Require(NativeMethods.GetStats(ulong.MaxValue, out _) == 2, "Invalid handle accepted.");
                    var small = new byte[] { 0x5A };
                    Require(NativeMethods.LastError(small, 1, out var required) == 5 && required > 1 && small[0] == 0x5A,
                        "Error buffer was truncated or overwritten.");
                    Require(Task.Run(() => NativeMethods.GetStats(host.Session, out _)).GetAwaiter().GetResult() == 3,
                        "Wrong-thread access was accepted.");
                    Require(NativeMethods.Configure(host.Session, 99, 0, 0) == 1, "Invalid pipeline accepted.");
                    window.ViewModel.SelectedPipelineIndex = 99;
                    Require(window.ViewModel.SelectedPipelineIndex == 1 && window.ViewModel.ErrorText.Length > 0,
                        "Rejected renderer settings did not restore the view model selection.");
                    window.ViewModel.Exposure = .25;
                    window.ViewModel.Exposure = 0;
                    checks.Add("embedded HWND, current DPI pixel layout, invalid handles, error buffer, owner thread, invalid settings");
                    savedResizes = stats.Resizes;
                    window.Width = 1060;
                    window.Height = 700;
                    break;
                case 1:
                    Require(stats.Resizes > savedResizes, "Window resize did not reach swapchain.");
                    CheckPixels(stats);
                    checks.Add("resize and DIP-to-pixel client dimensions");
                    savedInput = stats.InputFrames;
                    NativeMethods.SetFocus(host.Child);
                    NativeMethods.SendMessage(host.Child, 0x0100, 'W', 0);
                    break;
                case 2:
                    Require(stats.InputFrames > savedInput && stats.PressedKeys > 0, "Focused camera input was not consumed.");
                    NativeMethods.SendMessage(host.Child, 0x0204, 2, 0);
                    break;
                case 3:
                    Require(stats.Captured != 0, "Right mouse capture failed.");
                    NativeMethods.SetFocus(root);
                    window.FocusBox.Focus();
                    break;
                case 4:
                    Require(stats.Focused == 0 && stats.Captured == 0 && stats.PressedKeys == 0,
                        "Focus loss did not release capture and keys.");
                    checks.Add("camera input, right mouse capture and managed-focus release");
                    savedFrames = stats.Frames;
                    savedPaused = stats.PausedTicks;
                    window.WindowState = WindowState.Minimized;
                    break;
                case 5:
                    Require(stats.PausedTicks > savedPaused && stats.Frames <= savedFrames + 1,
                        "Minimized window continued rendering.");
                    window.WindowState = WindowState.Normal;
                    break;
                case 6:
                    Require(stats.Frames > savedFrames + 1, "Restored window did not resume rendering.");
                    checks.Add("minimize pauses rendering and restore resumes");
                    savedFrames = stats.Frames;
                    savedPaused = stats.PausedTicks;
                    host.IsVisible = false;
                    break;
                case 7:
                    Require(stats.PausedTicks > savedPaused && stats.Frames <= savedFrames + 1,
                        "Hidden viewport continued rendering.");
                    host.IsVisible = true;
                    break;
                case 8:
                    Require(stats.Frames > savedFrames + 1, "Shown viewport did not resume.");
                    checks.Add("hide/show viewport");
                    window.PipelinePicker.SelectedIndex = 0;
                    window.ExposureSlider.Value = .5;
                    Require(window.ResetButton.Command != null, "Reset command binding is missing.");
                    window.ResetButton.Command!.Execute(null);
                    break;
                case 9:
                    Require(stats.Pipeline == 1 && window.ViewModel.SelectedPipelineIndex == 0 &&
                        Math.Abs(window.ViewModel.Exposure - .5) < .001,
                        "Compiled pipeline/exposure bindings did not update the view model and renderer.");
                    window.PipelinePicker.SelectedIndex = 2;
                    break;
                case 10:
                    Require(stats.Pipeline == 3 && stats.ReflectionSamples > 0, "Ray tracing did not present.");
                    checks.Add("DI services, compiled two-way pipeline/exposure bindings, generated reset command and three pipeline presentation");
                    savedFrames = stats.Frames;
                    savedPaused = stats.PausedTicks;
                    Require(window.PauseButton.Command != null, "Pause command binding is missing.");
                    window.PauseButton.Command!.Execute(null);
                    break;
                case 11:
                    Require(stats.PausedTicks > savedPaused && stats.Frames <= savedFrames + 1 &&
                        window.ViewModel.IsPaused && window.PauseButton.Content?.ToString() == "Resume viewport",
                        "Pause command or generated property notification did not update rendering and UI.");
                    window.PauseButton.Command!.Execute(null);
                    checks.Add("generated pause command and pause-label notification");
                    oldHandle = host.Session;
                    detachedHost = host;
                    Require(window.RecreateButton.Command != null, "Recreate command binding is missing.");
                    window.RecreateButton.Command!.Execute(null);
                    break;
                case 12:
                    Require(stats.TickNs > 0 && stats.RenderNs > 0 &&
                        (stats.ProfilingEnabled == 0 || (stats.GpuFrame > 0 && stats.GpuNs > 0)),
                        "CPU/GPU frame timings were not collected.");
                    Require(host.Session != oldHandle && NativeMethods.GetStats(oldHandle, out _) == 2,
                        "Destroyed session generation was reused.");
                    Require(window.CloseReports.Count == 1 && window.CloseReports[0].RemainingBytes == 0 &&
                        window.CloseReports[0].ValidationErrors == 0, "First viewport shutdown leaked or failed validation.");
                    Require(stats.Pipeline == 3 && stats.ReflectionSamples > 0 && !window.ViewModel.IsPaused,
                        "Viewport settings were lost during host recreation.");
                    detachedHost!.CloseViewport();
                    Require(NativeMethods.GetStats(host.Session, out _) == 0,
                        "A repeated detached-host close destroyed the replacement session.");
                    checks.Add("detach/recreate, stale generation rejection, GPU-before-HWND shutdown");
                    Finish(stats);
                    break;
            }
        }
        catch (Exception error)
        {
            Fail(error);
        }
    }

    private void Finish(ViewportStats stats)
    {
        window.Viewport.CloseViewport();
        Require(window.CloseReports.Count == 2 && window.CloseReports.All(r => r.RemainingBytes == 0 && r.ValidationErrors == 0),
            "Final native shutdown did not release all resources cleanly.");
        completed = true;
        watchdog.Stop();
        File.WriteAllText(Path.Combine(options.Output, "smoke.json"), JsonSerializer.Serialize(new
        {
            backend = options.Backend,
            avalonia = "12.0.5",
            passed = true,
            checks,
            stats,
            closeReports = window.CloseReports,
            dpiBoundary = "Current monitor scale verified; cross-monitor DPI transition not tested."
        }, EditorDiagnostics.JsonOptions));
        ((IClassicDesktopStyleApplicationLifetime)Application.Current!.ApplicationLifetime!).Shutdown(0);
    }

    internal void Fail(Exception error)
    {
        if (completed)
        {
            return;
        }
        completed = true;
        watchdog.Stop();
        File.WriteAllText(Path.Combine(options.Output, "smoke-failure.txt"), error.ToString());
        Dispatcher.UIThread.Post(() => ((IClassicDesktopStyleApplicationLifetime)Application.Current!.ApplicationLifetime!).Shutdown(1));
    }
}
