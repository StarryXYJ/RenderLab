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
using RenderLab.Editor.Docking;

namespace RenderLab.Editor.Diagnostics;

internal sealed class SmokeTest
{
    private readonly MainWindow window;
    private readonly EditorOptions options;
    private readonly List<string> checks = [];
    private readonly Stopwatch elapsed = Stopwatch.StartNew();
    private readonly DispatcherTimer watchdog = new() { Interval = TimeSpan.FromSeconds(120) };
    private int stage, ticks, resizeBurstTicks;
    private int acknowledgedPauseStage = -1;
    private ulong oldHandle, savedFrames, savedResizes, savedPaused, savedInput;
    private bool completed;
    private RenderViewportHost? detachedHost;
    private readonly float[] savedPose = new float[7];

    internal SmokeTest(MainWindow window, EditorOptions options)
    {
        this.window = window;
        this.options = options;
        window.StatsUpdated += Update;
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

    private bool PixelsMatch(ViewportStats stats) =>
        Math.Abs(stats.Width - window.Viewport.Bounds.Width * window.RenderScaling) <= 2 &&
        Math.Abs(stats.Height - window.Viewport.Bounds.Height * window.RenderScaling) <= 2;

    private void CheckPixels(ViewportStats stats) => Require(PixelsMatch(stats),
        "Native client size does not match Avalonia DIP-to-pixel layout.");

    private void Update(ViewportStats stats)
    {
        if (completed)
        {
            return;
        }
        try
        {
            Require(elapsed.Elapsed < TimeSpan.FromSeconds(120), "Viewport smoke timed out.");
            if (stage == 17 && resizeBurstTicks < 6)
            {
                ++resizeBurstTicks;
                window.Width += 10;
                return;
            }
            if (++ticks < 8 || stats.Frames < 1 || stats.Ready == 0)
            {
                return;
            }
            // Configuration now crosses a mailbox. First-use shader/PSO work may take
            // longer than eight UI heartbeats; wait for the published render result.
            if ((stage == 9 && stats.Pipeline != 1) ||
                (stage == 10 && (stats.Pipeline != 3 || stats.ReflectionSamples == 0)) ||
                (stage == 12 && (stats.Pipeline != 3 || stats.ReflectionSamples == 0 ||
                    (stats.ProfilingEnabled != 0 && stats.GpuFrame == 0))) ||
                (stage == 13 && (stats.Pipeline != 1 || stats.Activity != 2)))
            {
                return;
            }
            if ((stage is 1 or 17) && !PixelsMatch(stats))
            {
                return;
            }
            if (stage is 5 or 7 or 11 && acknowledgedPauseStage != stage)
            {
                if (stats.Activity != 3)
                {
                    return;
                }
                // Freeze the reference after the render owner acknowledges suspension.
                // A UI snapshot sampled before the command may already lag an in-flight frame.
                acknowledgedPauseStage = stage;
                savedFrames = stats.Frames;
                ticks = 0;
                return;
            }
            ticks = 0;
            var host = window.Viewport;
            var root = window.TryGetPlatformHandle()!.Handle;
            switch (stage++)
            {
                case 0:
                    Require(window.Docks.IsOpen("Scene") && window.Docks.IsOpen("Settings") &&
                        !window.Docks.IsOpen("Log") && !window.Docks.IsOpen("Render Graph") && !window.Docks.IsOpen("Profiler"),
                        "Default layout did not contain exactly the two requested panels.");
                    Require(stats.UiThreadId != 0 && stats.RenderThreadId != 0 &&
                        stats.UiThreadId != stats.RenderThreadId, "Runtime did not move to a dedicated render owner.");
                    Require(stats.DroppedSamples == 0, "Completed frame samples were dropped.");
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
                    Require(stats.PausedTicks > savedPaused && stats.Frames == savedFrames,
                        "Minimized window continued rendering.");
                    window.WindowState = WindowState.Normal;
                    break;
                case 6:
                    Require(stats.Frames > savedFrames, "Restored window did not resume rendering.");
                    checks.Add("minimize pauses rendering and restore resumes");
                    savedFrames = stats.Frames;
                    savedPaused = stats.PausedTicks;
                    host.IsVisible = false;
                    break;
                case 7:
                    Require(stats.PausedTicks > savedPaused && stats.Frames == savedFrames,
                        "Hidden viewport continued rendering.");
                    host.IsVisible = true;
                    break;
                case 8:
                    Require(stats.Frames > savedFrames, "Shown viewport did not resume.");
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
                    Require(stats.PausedTicks > savedPaused && stats.Frames == savedFrames &&
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
                    window.PipelinePicker.SelectedIndex = 0;
                    break;
                case 13:
                    savedFrames = stats.Frames;
                    break;
                case 14:
                    Require(stats.Frames == savedFrames && stats.Activity == 2,
                        "Static raster viewport kept rendering.");
                    checks.Add("static raster viewport idles without additional presents");
                    window.ExposureSlider.Value = .75;
                    break;
                case 15:
                    Require(stats.Frames > savedFrames, "Exposure edit did not invalidate the static viewport.");
                    window.PipelinePicker.SelectedIndex = 2;
                    break;
                case 16:
                    if (stats.Pipeline != 3 || stats.ReflectionSamples < 2)
                    {
                        --stage;
                        break;
                    }
                    checks.Add("raster edits redraw and ray tracing continues progressive accumulation");
                    savedResizes = stats.Resizes;
                    break;
                case 17:
                    CheckPixels(stats);
                    Require(stats.Resizes > savedResizes && stats.Resizes <= savedResizes + 2,
                        "Consecutive resize updates were not coalesced.");
                    checks.Add("six consecutive UI resize updates coalesced into at most two swapchain rebuilds");
                    NativeMethods.Check(NativeMethods.Camera(host.Session, savedPose, 0));
                    oldHandle = host.Session;
                    window.Docks.FloatDockable(window.Docks.Panel("Scene"));
                    break;
                case 18:
                    Require(host.Session != oldHandle && NativeMethods.GetStats(oldHandle, out _) == 2,
                        "Floating Scene did not retire the old native host.");
                    var floatingPose = new float[7];
                    NativeMethods.Check(NativeMethods.Camera(host.Session, floatingPose, 0));
                    Require(floatingPose.Zip(savedPose).All(p => Math.Abs(p.First - p.Second) < .001f),
                        "Floating Scene lost the navigation camera.");
                    window.Docks.Show("Log"); window.Docks.Show("Log");
                    Require(WorkbenchDockFactory.Walk(window.Docks.Root).Count(p => p.Id == "Log") == 1,
                        "Opening the same Dock twice created duplicate panels.");
                    checks.Add("Scene floating recreates the session and preserves camera; menu activation is single-instance");
                    window.ResetLayout(false);
                    break;
                case 19:
                    Require(window.Docks.IsOpen("Scene") && !window.Docks.IsOpen("Log"), "Default reset retained diagnostic docks.");
                    var restoredPose = new float[7];
                    NativeMethods.Check(NativeMethods.Camera(host.Session, restoredPose, 0));
                    Require(restoredPose.Zip(savedPose).All(p => Math.Abs(p.First - p.Second) < .001f), "Dock reset lost camera pose.");
                    Require(Program.Logging!.Buffer.Snapshot().Any(r => r.Source.StartsWith("Native.") && r.Message.Contains("Render owner thread")),
                        "Native structured log mirror never reached the Log buffer.");
                    checks.Add("Scene docks back through layout reset; native log batches are collected without managed callbacks");
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
        Require(window.CloseReports.Count >= 4 && window.CloseReports.All(r => r.RemainingBytes == 0 && r.ValidationErrors == 0 && r.WorkerFailed == 0),
            "Final native shutdown did not release all resources cleanly.");
        NativeMethods.Check(NativeMethods.Create(window.TryGetPlatformHandle()!.Handle,
            options.Backend == "dx12" ? 0u : 1u, options.Validation ? 1u : 0u,
            options.Profiling ? 1u : 0u, options.TargetFps, 0,
            options.Project, options.Shaders, Path.Combine(options.Output, "startup-close.log"),
            out var startupSession, out _));
        NativeMethods.Check(NativeMethods.Destroy(startupSession, out var startupClose));
        Require(startupClose.RemainingBytes == 0 && startupClose.ValidationErrors == 0 && startupClose.WorkerFailed == 0,
            "Closing during render startup leaked or failed validation.");
        checks.Add("immediate close during asynchronous render startup");
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
            startupClose,
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
