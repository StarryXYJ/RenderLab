using System.Text.Json;
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Threading;
using RenderLab.Editor.Configuration;
using RenderLab.Editor.Interop;
using RenderLab.Editor.Services;
using RenderLab.Editor.Views;

namespace RenderLab.Editor.Diagnostics;

/// <summary>Fixed-camera window measurement with warmup; never performs per-frame file I/O.</summary>
internal sealed class ViewportBenchmark
{
    private readonly EditorOptions options;
    private readonly NativeViewportService viewport;
    private readonly MainWindow window;
    private readonly List<FrameSample> samples = [];
    private readonly DispatcherTimer watchdog;
    private bool finished;
    private ulong previousFrame;
    private ulong? firstFrameTime;

    internal ViewportBenchmark(MainWindow window, NativeViewportService viewport, EditorOptions options)
    {
        this.window = window;
        this.viewport = viewport;
        this.options = options;
        viewport.FrameMeasured += Update;
        viewport.Failed += Fail;
        watchdog = new DispatcherTimer { Interval = TimeSpan.FromSeconds(options.WarmupSeconds + options.BenchmarkSeconds + 30) };
        watchdog.Tick += (_, _) => Fail(new TimeoutException("Viewport benchmark timed out."));
        watchdog.Start();
    }

    private void Update(ViewportStats stats)
    {
        if (finished || stats.Frames == previousFrame)
        {
            return;
        }
        previousFrame = stats.Frames;
        firstFrameTime ??= stats.FrameTimeNs;
        var seconds = (stats.FrameTimeNs - firstFrameTime.Value) / 1e9;
        if (seconds < options.WarmupSeconds)
        {
            return;
        }
        if (viewport.Timings.Latest is { } sample)
        {
            samples.Add(sample);
        }
        if (seconds < options.WarmupSeconds + options.BenchmarkSeconds)
        {
            return;
        }
        finished = true;
        watchdog.Stop();
        Dispatcher.UIThread.Post(() => Finish(stats));
    }

    private void Finish(ViewportStats stats)
    {
        window.Viewport.CloseViewport();
        File.WriteAllText(Path.Combine(options.Output, "frames.json"), JsonSerializer.Serialize(samples, EditorDiagnostics.JsonOptions));
        File.WriteAllText(Path.Combine(options.Output, "performance.json"), JsonSerializer.Serialize(new
        {
            options,
            stats,
            summary = FrameTimingWindow.Summarize(samples),
            viewport.CloseReports,
            scope = "Native Present-return intervals from a bounded frame queue; GPU is the sum of completed Graph pass timings, not scanout latency."
        }, EditorDiagnostics.JsonOptions));
        ((IClassicDesktopStyleApplicationLifetime)Application.Current!.ApplicationLifetime!).Shutdown(0);
    }

    private void Fail(Exception error)
    {
        if (finished)
        {
            return;
        }
        finished = true;
        watchdog.Stop();
        EditorDiagnostics.WriteError(options, error);
        ((IClassicDesktopStyleApplicationLifetime)Application.Current!.ApplicationLifetime!).Shutdown(1);
    }
}
