using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Threading;
using RenderLab.Editor.Configuration;
using RenderLab.Editor.Interop;
using RenderLab.Editor.Services;
using RenderLab.Editor.Views;

namespace RenderLab.Editor.Diagnostics;

/// <summary>Wall-clock integration run; streams bounded samples instead of retaining every frame.</summary>
internal sealed class WorkbenchSoak
{
    private readonly MainWindow window;
    private readonly NativeViewportService service;
    private readonly EditorOptions options;
    private readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromSeconds(1) };
    private readonly Stopwatch startup = Stopwatch.StartNew(), run = new();
    private readonly Dictionary<string, string> hashes;
    private readonly StreamWriter samples;
    private readonly List<object> monitors = [];
    private bool initialized, finished;
    private int lastCycle = -1, lastPanel = -1, lastLayout = -1, monitorIndex = -1;
    private ulong frames, session, previousFrame, dropped, previousDropped, maxNative;
    private long maxManaged, maxPrivate;
    private int maximumHistory, maximumRows, records;

    internal WorkbenchSoak(MainWindow window, NativeViewportService service, EditorOptions options)
    {
        this.window = window; this.service = service; this.options = options;
        hashes = HashSources();
        samples = new StreamWriter(Path.Combine(options.Output, "soak-samples.jsonl"), false, new System.Text.UTF8Encoding(false)) { AutoFlush = true };
        timer.Tick += (_, _) => Tick(); timer.Start();
        service.Failed += Fail;
    }
    private Dictionary<string, string> HashSources() => Directory.EnumerateFiles(Path.GetDirectoryName(options.Project)!, "*", SearchOption.AllDirectories)
        .Where(path => path.EndsWith(".scene") || path.EndsWith(".material") || path.EndsWith(".rlproject"))
        .Where(path => !path.Contains(Path.DirectorySeparatorChar + "Library" + Path.DirectorySeparatorChar) && !path.Contains(Path.DirectorySeparatorChar + "UserSettings" + Path.DirectorySeparatorChar))
        .ToDictionary(path => path, path => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))));
    private void Require(bool value, string reason) { if (!value) { throw new InvalidOperationException(reason); } }
    private void Tick()
    {
        if (finished) { return; }
        try
        {
            if (service.Configuration.Ready == 0 || service.Configuration.Status != 2 ||
                service.Stats.SessionGeneration != service.Session || service.Stats.ConfigRevision != service.Configuration.AppliedId)
            {
                Require(initialized ? run.Elapsed.TotalSeconds < options.SoakSeconds + 90 : startup.Elapsed.TotalSeconds < 90, "Soak initialization/configuration timed out.");
                return;
            }
            if (!initialized)
            {
                initialized = true;
                var config = service.EditableConfig; config.Continuous = 1; config.TargetFps = 30; config.Submission = 3;
                service.Submit(config); window.Docks.Show("Profiler");
                run.Start();
                return;
            }
            var seconds = run.Elapsed.TotalSeconds;
            var stats = service.Stats;
            if (session != service.Session) { session = service.Session; previousFrame = previousDropped = 0; }
            if (stats.SessionGeneration == session)
            {
                frames += stats.Frames >= previousFrame ? stats.Frames - previousFrame : 0; previousFrame = stats.Frames;
                dropped += stats.DroppedSamples >= previousDropped ? stats.DroppedSamples - previousDropped : 0; previousDropped = stats.DroppedSamples;
            }
            Require(service.DeviceSwitch.Current.Backend == options.Backend, "Soak changed its assigned API.");
            Require(service.CloseReports.All(report => report.RemainingBytes == 0 && report.ValidationErrors == 0 && report.WorkerFailed == 0), "A soak session failed clean retirement.");
            var managed = GC.GetTotalMemory(false);
            using var process = Process.GetCurrentProcess();
            var native = stats.NativeBytes; var privateBytes = process.PrivateMemorySize64;
            maxNative = Math.Max(maxNative, native); maxManaged = Math.Max(maxManaged, managed); maxPrivate = Math.Max(maxPrivate, privateBytes);
            var rows = Program.Logging!.Buffer.Snapshot();
            maximumRows = Math.Max(maximumRows, rows.Length); maximumHistory = Math.Max(maximumHistory, window.ProfilerModel.HistoryCount);
            Require(rows.Length <= 10000 && rows.Sum(row => (long)row.Bytes) <= 8 * 1024 * 1024, "Log projection exceeded its budget.");
            Require(window.ProfilerModel.HistoryCount <= 64 && window.ProfilerModel.HistoryBytes <= 8 * 1024 * 1024 && service.Timings.Snapshot().Length <= 240, "Diagnostic history exceeded its budget.");
            Require(native <= 2048ul * 1024 * 1024 && managed < 512L * 1024 * 1024, "Soak native/managed allocation exceeded its explicit budget.");
            if ((int)seconds % 5 == 0 || seconds >= options.SoakSeconds)
            {
                ++records;
                var sample = new
                {
                    seconds,
                    backend = options.Backend,
                    session,
                    frames,
                    currentFrame = stats.Frames,
                    config = stats.ConfigRevision,
                    native,
                    managed,
                    privateBytes,
                    dropped,
                    history = window.ProfilerModel.HistoryCount,
                    historyBytes = window.ProfilerModel.HistoryBytes,
                    logs = rows.Length,
                    logBytes = rows.Sum(row => (long)row.Bytes),
                    width = stats.Width,
                    height = stats.Height,
                    dpi = stats.Dpi,
                    activity = stats.Activity,
                    pipeline = stats.Pipeline,
                    nativeLogDropped = service.NativeLogDropped
                };
                var json = JsonSerializer.Serialize(sample);
                samples.WriteLine(json);
                File.WriteAllText(Path.Combine(options.Output, "soak-progress.json"), json + "\n");
            }
            if (seconds >= options.SoakSeconds) { Finish(stats); return; }
            var cycle = (int)(seconds / 300);
            if (cycle != lastCycle)
            {
                lastCycle = cycle;
                var config = service.EditableConfig;
                config.Pipeline = (uint)(cycle % 3 + 1); config.View = 0; config.Profiling = cycle % 2 == 0 ? 1u : 0u;
                config.MemoryAlias = cycle % 2 == 0 ? 1u : 0u; config.RecordingThreads = cycle % 2 == 0 ? 4u : 1u;
                config.Flags = cycle % 2 == 0 ? config.Flags | 32u : config.Flags & ~32u;
                service.Submit(config);
            }
            var panel = (int)(seconds / 45);
            if (panel != lastPanel)
            {
                lastPanel = panel;
                window.Docks.Show(new[] { "Profiler", "Render Graph", "Log" }[panel % 3]);
            }
            var layout = (int)(seconds / 600);
            if (layout != lastLayout)
            {
                lastLayout = layout;
                if (layout > 0) { window.ResetLayout(true); }
            }
            // Move between actual connected monitor work areas; do not synthesize DPI values.
            var desiredMonitor = (int)(seconds / 10);
            if (desiredMonitor < window.Screens.All.Count && desiredMonitor != monitorIndex)
            {
                if (monitorIndex >= 0) { RecordMonitor(stats); }
                monitorIndex = desiredMonitor;
                var area = window.Screens.All[monitorIndex].WorkingArea;
                window.Position = new PixelPoint(area.X + 30, area.Y + 30);
            }

        }
        catch (Exception error) { Fail(error); }
    }
    private void RecordMonitor(ViewportStats stats)
    {
        var pixels = Math.Abs(stats.Width - window.Viewport.Bounds.Width * window.RenderScaling) <= 2 &&
            Math.Abs(stats.Height - window.Viewport.Bounds.Height * window.RenderScaling) <= 2;
        Require(pixels, "Monitor transition broke native DIP-to-pixel dimensions.");
        monitors.Add(new
        {
            index = monitorIndex,
            bounds = window.Screens.All[monitorIndex].Bounds.ToString(),
            dpi = stats.Dpi,
            scaling = window.RenderScaling,
            width = stats.Width,
            height = stats.Height,
            pixelsMatched = pixels
        });
    }
    private void Finish(ViewportStats stats)
    {
        if (monitorIndex >= 0) { RecordMonitor(stats); }
        Require(HashSources().OrderBy(pair => pair.Key).SequenceEqual(hashes.OrderBy(pair => pair.Key)), "Soak modified a source Scene/Material/Project.");
        Require(frames > 0 && records > 0, "Soak did not collect rendering samples.");
        window.Viewport.CloseViewport();
        Require(service.CloseReports.All(report => report.RemainingBytes == 0 && report.ValidationErrors == 0 && report.WorkerFailed == 0), "Final soak shutdown was not clean.");
        finished = true; timer.Stop(); samples.Dispose(); service.Failed -= Fail;
        File.WriteAllText(Path.Combine(options.Output, "soak.json"), JsonSerializer.Serialize(new
        {
            passed = true,
            backend = options.Backend,
            elapsedSeconds = run.Elapsed.TotalSeconds,
            requestedSeconds = options.SoakSeconds,
            frames,
            records,
            dropped,
            maxNative,
            maxManaged,
            maxPrivate,
            maximumRows,
            maximumHistory,
            closeReports = service.CloseReports,
            sourceHashes = hashes,
            monitors,
            monitorCount = window.Screens.All.Count,
            boundary = "Actual connected monitors only; physical input and visual inspection are recorded separately."
        }, EditorDiagnostics.JsonOptions));
        ((IClassicDesktopStyleApplicationLifetime)Application.Current!.ApplicationLifetime!).Shutdown(0);
    }
    private void Fail(Exception error)
    {
        if (finished) { return; }
        finished = true; timer.Stop(); samples.Dispose(); service.Failed -= Fail;
        EditorDiagnostics.WriteError(options, error);
        File.WriteAllText(Path.Combine(options.Output, "soak-failure.txt"), error.ToString());
        ((IClassicDesktopStyleApplicationLifetime)Application.Current!.ApplicationLifetime!).Shutdown(1);
    }
}
