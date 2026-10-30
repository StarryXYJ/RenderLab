using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text;
using System.Diagnostics;
using Serilog.Events;
using Serilog.Parsing;
using Avalonia.Threading;
using RenderLab.Editor.Configuration;
using RenderLab.Editor.Diagnostics;
using RenderLab.Editor.Interop;

namespace RenderLab.Editor.Services;

/// <summary>Owns the UI side of one session; rendering and GPU waits belong to its native worker.</summary>
public sealed class NativeViewportService : IDisposable
{
    private readonly EditorOptions options;
    private int logSequence;
    private float[]? savedCamera;
    private long lastLogPoll;
    private ulong lastNativeSequence;
    internal ulong NativeLogDropped { get; private set; }
    private readonly ViewportStats[] completedFrames = new ViewportStats[512];
    internal FrameTimingWindow Timings { get; } = new();
    internal uint AbiVersion { get; private set; }
    internal ulong Session { get; private set; }
    internal nint Child { get; private set; }
    internal ViewportStats Stats { get; private set; }
    internal uint Pipeline { get; private set; } = 2;
    internal float Exposure { get; private set; }
    internal bool IsPaused { get; set; }
    internal List<CloseReport> CloseReports { get; } = [];
    internal event Action<ViewportStats>? StatsUpdated;
    internal event Action<ViewportStats>? FrameMeasured;
    internal event Action<Exception>? Failed;
    internal event Action? RecreateRequested;

    public NativeViewportService(EditorOptions options)
    {
        this.options = options;
        Pipeline = options.Pipeline;
    }

    internal nint Create(nint parent)
    {
        Dispatcher.UIThread.VerifyAccess();
        if (Session != 0)
        {
            throw new InvalidOperationException("The viewport service is already attached to a host.");
        }
        AbiVersion = NativeMethods.AbiVersion();
        if (AbiVersion != 5 || Marshal.SizeOf<ViewportStats>() != 232 || Marshal.SizeOf<CloseReport>() != 24)
        {
            throw new InvalidOperationException("Managed/native bridge ABI mismatch.");
        }
        NativeMethods.Check(NativeMethods.Create(parent, options.Backend == "dx12" ? 0u : 1u,
            options.Validation ? 1u : 0u, options.Profiling ? 1u : 0u,
            options.TargetFps, options.BenchmarkSeconds > 0 ? 1u : 0u,
            options.Project, options.Shaders, Path.Combine(options.Output, $"native-{++logSequence}.log"),
            out var session, out var child));
        Session = session;
        lastNativeSequence = 0;
        Timings.Reset();
        Child = child;
        try
        {
            Configure(Pipeline, Exposure);
            if (savedCamera is not null) { NativeMethods.Check(NativeMethods.Camera(Session, savedCamera, 1)); }
        }
        catch
        {
            Close(Session);
            throw;
        }
        return Child;
    }

    internal void Configure(uint pipeline, float exposure, bool reset = false)
    {
        Dispatcher.UIThread.VerifyAccess();
        if (Session != 0)
        {
            NativeMethods.Check(NativeMethods.Configure(Session, pipeline, exposure, reset ? 1u : 0u));
        }
        Pipeline = pipeline;
        Exposure = exposure;
    }

    internal void Poll(bool hidden)
    {
        Dispatcher.UIThread.VerifyAccess();
        NativeMethods.Check(NativeMethods.Update(Session, IsPaused || hidden ? 1u : 0u));
        NativeMethods.Check(NativeMethods.ReadFrames(Session, completedFrames, (uint)completedFrames.Length, out var count));
        for (var i = 0; i < count; ++i)
        {
            var frame = completedFrames[i];
            Timings.Observe(frame, frame.FrameTimeNs / 1e9);
            FrameMeasured?.Invoke(frame);
        }
        NativeMethods.Check(NativeMethods.GetStats(Session, out var stats));
        Stats = stats;
        if (Stopwatch.GetElapsedTime(lastLogPoll).TotalMilliseconds >= 250)
        {
            lastLogPoll = Stopwatch.GetTimestamp();
            DrainLogs(false);
        }
        StatsUpdated?.Invoke(stats);
    }

    internal void RequestRecreate() => RecreateRequested?.Invoke();

    internal void ReportFailure(Exception error)
    {
        EditorDiagnostics.WriteError(options, error);
        Failed?.Invoke(error);
    }

    internal void Close(ulong expectedSession)
    {
        Dispatcher.UIThread.VerifyAccess();
        if (Session == 0 || Session != expectedSession)
        {
            return;
        }
        var pose = new float[7];
        if (NativeMethods.Camera(Session, pose, 0) == 0) { savedCamera = pose; }
        NativeMethods.Check(NativeMethods.Stop(Session, out var report));
        DrainLogs(true);
        NativeMethods.Check(NativeMethods.Destroy(Session, out _));
        Session = 0;
        Child = 0;
        CloseReports.Add(report);
        File.WriteAllText(Path.Combine(options.Output, "close-reports.json"),
            JsonSerializer.Serialize(CloseReports, EditorDiagnostics.JsonOptions));
    }

    public void Dispose() => Close(Session);

    private void DrainLogs(bool all)
    {
        for (var batch = 0; batch < (all ? 40 : 1); ++batch)
        {
            var status = NativeMethods.ReadLogs(Session, null, 0, out var required);
            if (status != 5) { NativeMethods.Check(status); }
            if (required > 4 * 1024 * 1024) { throw new InvalidOperationException("Native log batch exceeds budget."); }
            var bytes = new byte[required];
            NativeMethods.Check(NativeMethods.ReadLogs(Session, bytes, required, out _));
            using var json = JsonDocument.Parse(Encoding.UTF8.GetString(bytes, 0, (int)required - 1));
            NativeLogDropped = json.RootElement.GetProperty("dropped").GetUInt64();
            var events = json.RootElement.GetProperty("events");
            foreach (var entry in events.EnumerateArray())
            {
                var sequence = entry.GetProperty("sequence").GetUInt64();
                if (sequence <= lastNativeSequence) { continue; }
                lastNativeSequence = sequence;
                var level = entry.GetProperty("level").GetUInt32() switch { 1 => LogEventLevel.Warning, 2 => LogEventLevel.Error, _ => LogEventLevel.Information };
                var logEvent = new LogEvent(DateTimeOffset.FromUnixTimeMilliseconds(entry.GetProperty("time").GetInt64()), level, null,
                    new MessageTemplateParser().Parse("{NativeMessage}"), [
                        new LogEventProperty("NativeMessage", new ScalarValue(entry.GetProperty("message").GetString())),
                        new LogEventProperty("SourceContext", new ScalarValue("Native." + options.Backend)),
                        new LogEventProperty("Origin", new ScalarValue("Native")),
                        new LogEventProperty("SessionGeneration", new ScalarValue(Session)),
                        new LogEventProperty("NativeSequence", new ScalarValue(sequence)),
                        new LogEventProperty("Truncated", new ScalarValue(entry.GetProperty("truncated").GetBoolean()))]);
                Program.Logging?.Logger.Write(logEvent);
            }
            if (events.GetArrayLength() < 128) { break; }
        }
    }
}
