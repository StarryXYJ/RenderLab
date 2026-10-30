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
    private bool nativeLogFailed;
    internal ulong NativeLogDropped { get; private set; }
    internal ViewportConfigState Configuration { get; private set; }
    internal ViewportConfig Defaults { get; private set; }
    internal string Capabilities { get; private set; } = "";
    internal string ConfigurationError { get; private set; } = "";
    internal event Action? ConfigurationUpdated;
    private ViewportConfig? savedConfiguration;
    private ulong requestSequence;
    private bool restoreConfiguration;
    private bool retiring, viewportHidden;
    private string? switchFailure;
    private CancellationTokenSource? switchCancellation;
    private DeviceSelection creatingDevice;
    internal DeviceSwitchTransaction DeviceSwitch { get; }
    internal string UserSettingsPath => options.Smoke || options.BenchmarkSeconds > 0 ? options.Output :
        Path.Combine(Path.GetDirectoryName(options.Project)!, "UserSettings", "Editor");
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
        creatingDevice = new(options.Backend, options.Validation);
        DeviceSwitch = new(creatingDevice);
        Pipeline = options.Pipeline;
    }

    internal nint Create(nint parent)
    {
        Dispatcher.UIThread.VerifyAccess();
        if (Session != 0)
        {
            // Dock can materialize the replacement presenter before detaching a cached one.
            // Retire the old owner first; its later close is rejected by session generation.
            Close(Session);
        }
        AbiVersion = NativeMethods.AbiVersion();
        if (AbiVersion != 8 || Marshal.SizeOf<ViewportStats>() != 256 || Marshal.SizeOf<CloseReport>() != 24 ||
            Marshal.SizeOf<ViewportConfig>() != 112 || Marshal.SizeOf<ViewportConfigState>() != 272)
        {
            throw new InvalidOperationException("Managed/native bridge ABI mismatch.");
        }
        NativeMethods.Check(NativeMethods.Create(parent, creatingDevice.Backend == "dx12" ? 0u : 1u,
            creatingDevice.Validation ? 1u : 0u, options.Profiling ? 1u : 0u,
            options.TargetFps, options.BenchmarkSeconds > 0 ? 1u : 0u,
            options.Project, options.Shaders, Path.Combine(options.Output, $"native-{++logSequence}.log"),
            out var session, out var child));
        Session = session;
        retiring = false;
        Configuration = default;
        Capabilities = "";
        restoreConfiguration = savedConfiguration is not null;
        lastNativeSequence = 0;
        nativeLogFailed = false;
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
            if (Configuration.Ready != 0 && !reset)
            {
                var config = EditableConfig;
                config.Pipeline = pipeline; config.Exposure = exposure;
                Submit(config);
            }
            else
            {
                NativeMethods.Check(NativeMethods.Configure(Session, pipeline, exposure, reset ? 1u : 0u));
                if (Configuration.Ready != 0) { ReadConfiguration(); }
            }
        }
        Pipeline = pipeline;
        Exposure = exposure;
    }

    internal void Poll(bool hidden)
    {
        Dispatcher.UIThread.VerifyAccess();
        viewportHidden = hidden;
        if (retiring) { return; }
        NativeMethods.Check(NativeMethods.Update(Session, (IsPaused && !DeviceSwitch.Busy) || hidden ? 1u : 0u));
        NativeMethods.Check(NativeMethods.ReadFrames(Session, completedFrames, (uint)completedFrames.Length, out var count));
        for (var i = 0; i < count; ++i)
        {
            var frame = completedFrames[i];
            Timings.Observe(frame, frame.FrameTimeNs / 1e9);
            FrameMeasured?.Invoke(frame);
        }
        NativeMethods.Check(NativeMethods.GetStats(Session, out var stats));
        Stats = stats;
        ReadConfiguration();
        if (Stopwatch.GetElapsedTime(lastLogPoll).TotalMilliseconds >= 250)
        {
            lastLogPoll = Stopwatch.GetTimestamp();
            if (!nativeLogFailed)
            {
                try { DrainLogs(false); }
                catch (Exception error) { nativeLogFailed = true; Program.Logging?.Logger.Warning(error, "Native log projection disabled; original file remains available"); }
            }
        }
        StatsUpdated?.Invoke(stats);
    }

    internal void RequestRecreate() => RecreateRequested?.Invoke();

    internal void ReportFailure(Exception error)
    {
        if (DeviceSwitch.Busy) { switchFailure = error.Message; return; }
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
        NativeMethods.Check(NativeMethods.Stop(Session, out var report));
        if (NativeMethods.GetConfig(Session, out var finalConfiguration, out _) == 0) { Configuration = finalConfiguration; }
        if (Configuration.Ready != 0) { savedConfiguration = EditableConfig; }
        if (NativeMethods.Camera(Session, pose, 0) == 0) { savedCamera = pose; }
        try { DrainLogs(true); }
        catch (Exception error) { Program.Logging?.Logger.Warning(error, "Final native log drain failed; original file remains available"); }
        NativeMethods.Check(NativeMethods.Destroy(Session, out _));
        Session = 0;
        Child = 0;
        var state = Configuration; state.Ready = 0;
        Configuration = state;
        ConfigurationUpdated?.Invoke();
        CloseReports.Add(report);
        File.WriteAllText(Path.Combine(options.Output, "close-reports.json"),
            JsonSerializer.Serialize(CloseReports, EditorDiagnostics.JsonOptions));
    }

    public void Dispose() => Close(Session);

    internal void CancelSwitch() => switchCancellation?.Cancel();
    internal void RequestSnapshot()
    {
        if (Session != 0 && !retiring) { NativeMethods.Check(NativeMethods.RequestSnapshot(Session)); }
    }
    internal string? ReadSnapshot()
    {
        if (Session == 0 || retiring) { return null; }
        NativeMethods.Check(NativeMethods.AcquireSnapshot(Session, out var snapshot, out var size));
        if (snapshot == 0) { return null; }
        try
        {
            if (size > 4 * 1024 * 1024 || size == 0) { throw new InvalidDataException("Snapshot exceeds budget."); }
            var bytes = new byte[size];
            NativeMethods.Check(NativeMethods.CopySnapshot(Session, snapshot, bytes, size));
            return Encoding.UTF8.GetString(bytes, 0, (int)size - 1);
        }
        finally { NativeMethods.Check(NativeMethods.ReleaseSnapshot(Session, snapshot)); }
    }
    internal string ExportSnapshot(string name, string json)
    {
        var directory = Path.Combine(options.Output, "snapshots");
        Directory.CreateDirectory(directory);
        var path = Path.Combine(directory, name + ".json");
        File.WriteAllText(path, json + "\n");
        return path;
    }
    internal async Task SwitchDeviceAsync(string backend, bool validation)
    {
        Dispatcher.UIThread.VerifyAccess();
        if (backend is not ("dx12" or "vulkan")) { throw new ArgumentException("Unknown graphics API."); }
        if (DeviceSwitch.Busy) { return; }
        switchCancellation?.Dispose();
        switchCancellation = new CancellationTokenSource();
        var token = switchCancellation.Token;
        ViewportConfig? preservedConfig = null;
        float[]? preservedPose = null;
        var captured = false;
        await DeviceSwitch.RunAsync(new(backend, validation), async target =>
        {
            if (Session != 0)
            {
                retiring = true;
                DeviceSwitch.Report("Retiring " + creatingDevice.Backend);
                NativeMethods.ReleaseCapture();
                NativeMethods.Check(NativeMethods.BeginStop(Session));
                var retiringSession = Session;
                while (true)
                {
                    token.ThrowIfCancellationRequested();
                    if (Session != retiringSession) { throw new OperationCanceledException("Viewport was replaced during retirement."); }
                    NativeMethods.Check(NativeMethods.IsRetired(retiringSession, out var retired));
                    if (retired != 0) { break; }
                    await Task.Delay(10, token);
                }
                Close(Session);
            }
            token.ThrowIfCancellationRequested();
            if (!captured) { preservedConfig = savedConfiguration; preservedPose = savedCamera; captured = true; }
            savedConfiguration = preservedConfig; savedCamera = preservedPose;
            creatingDevice = target; switchFailure = null;
            DeviceSwitch.Report("Creating " + target.Backend);
            Program.Logging?.Logger.Information("Creating device {Backend} with validation {Validation}", target.Backend, target.Validation);
            RecreateRequested?.Invoke();
            var expectedSession = Session;
            var activeWait = TimeSpan.Zero;
            var previous = Stopwatch.GetTimestamp();
            while (true)
            {
                await Task.Delay(10, token);
                if (Session != expectedSession) { throw new OperationCanceledException("Viewport was replaced during device creation."); }
                var now = Stopwatch.GetTimestamp();
                if (!viewportHidden && Stats.Activity is not (3 or 4)) { activeWait += Stopwatch.GetElapsedTime(previous, now); }
                previous = now;
                if (switchFailure is not null) { throw new InvalidOperationException(switchFailure); }
                if (Configuration.Status == 3) { throw new InvalidOperationException(ConfigurationError); }
                if (Session != 0 && !restoreConfiguration && Configuration.Status == 2 && Stats.Frames > 0) { break; }
                if (activeWait > TimeSpan.FromSeconds(45)) { throw new TimeoutException("Waiting for the configured first frame."); }
            }
        }, token);
        if (DeviceSwitch.Failed && Session != 0)
        {
            retiring = true;
            NativeMethods.Check(NativeMethods.BeginStop(Session));
            while (true)
            {
                NativeMethods.Check(NativeMethods.IsRetired(Session, out var retired));
                if (retired != 0) { break; }
                await Task.Delay(10, token);
            }
            Close(Session);
        }
        if (DeviceSwitch.Error.Length > 0) { Program.Logging?.Logger.Error("Device switch: {Error}", DeviceSwitch.Error); }
    }

    internal ViewportConfig EditableConfig => Configuration.Status == 3 && Configuration.Applied.Size != 0 ? Configuration.Applied : Configuration.Requested;
    internal void Submit(ViewportConfig config)
    {
        Dispatcher.UIThread.VerifyAccess();
        if (Session == 0 || Configuration.Ready == 0) { throw new InvalidOperationException("Viewport is not ready for configuration."); }
        config.Size = 112; config.Version = 1;
        config.RequestId = Math.Max(requestSequence, Configuration.RequestedId) + 1;
        requestSequence = config.RequestId;
        NativeMethods.Check(NativeMethods.ConfigureV2(Session, in config));
        var state = Configuration;
        state.SupersededId = state.Status == 1 ? state.RequestedId : state.SupersededId;
        state.Requested = config; state.RequestedId = config.RequestId; state.Status = 1;
        Configuration = state; ConfigurationError = "";
        Pipeline = config.Pipeline; Exposure = config.Exposure;
        Program.Logging?.Logger.Information("Requested configuration {RequestId} for {SessionGeneration}", config.RequestId, Session);
        ConfigurationUpdated?.Invoke();
    }
    internal void SingleStep() => NativeMethods.Check(NativeMethods.Step(Session));
    internal void ResetConfiguration() => Submit(Defaults);
    internal void SavePreset()
    {
        if (Configuration.Ready == 0) { throw new InvalidOperationException("Viewport is not ready."); }
        Directory.CreateDirectory(UserSettingsPath);
        File.WriteAllText(Path.Combine(UserSettingsPath, "render-preset.json"), JsonSerializer.Serialize(EditableConfig, EditorDiagnostics.JsonOptions) + "\n");
    }
    internal void LoadPreset()
    {
        var path = Path.Combine(UserSettingsPath, "render-preset.json");
        if (new FileInfo(path).Length > 16384) { throw new InvalidDataException("Preset exceeds budget."); }
        var config = JsonSerializer.Deserialize<ViewportConfig>(File.ReadAllText(path), EditorDiagnostics.JsonOptions);
        if (config.Version != 1 || config.Size != 112) { throw new InvalidDataException("Unsupported preset schema."); }
        Submit(config);
    }
    private string ReadConfigText(uint kind)
    {
        NativeMethods.ConfigText(Session, kind, null, 0, out var size);
        for (var attempt = 0; attempt < 3; ++attempt)
        {
            if (size > 4 * 1024 * 1024) { throw new InvalidDataException("Configuration snapshot exceeds budget."); }
            var bytes = new byte[size];
            var status = NativeMethods.ConfigText(Session, kind, bytes, size, out size);
            if (status == 5) { continue; }
            NativeMethods.Check(status);
            return Encoding.UTF8.GetString(bytes, 0, (int)size - 1);
        }
        throw new InvalidOperationException("Configuration snapshot changed repeatedly during copy.");
    }
    private void ReadConfiguration()
    {
        NativeMethods.Check(NativeMethods.GetConfig(Session, out var state, out var defaults));
        var changed = state.RequestedId != Configuration.RequestedId || state.AppliedId != Configuration.AppliedId ||
            state.Status != Configuration.Status || state.Ready != Configuration.Ready;
        Configuration = state; Defaults = defaults;
        requestSequence = Math.Max(requestSequence, state.RequestedId);
        if (state.Ready != 0 && Capabilities.Length == 0) { Capabilities = ReadConfigText(0); changed = true; }
        if (restoreConfiguration && state.Ready != 0)
        {
            restoreConfiguration = false;
            Submit(savedConfiguration!.Value);
            return;
        }
        if (changed)
        {
            ConfigurationError = state.Status == 3 ? ReadConfigText(1) : "";
            if (state.Status == 3 && state.Applied.Size != 0) { Pipeline = state.Applied.Pipeline; Exposure = state.Applied.Exposure; }
            ConfigurationUpdated?.Invoke();
        }
    }

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
                        new LogEventProperty("SourceContext", new ScalarValue("Native." + creatingDevice.Backend)),
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
