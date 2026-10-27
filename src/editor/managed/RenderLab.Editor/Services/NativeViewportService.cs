using System.Runtime.InteropServices;
using System.Text.Json;
using Avalonia.Threading;
using RenderLab.Editor.Configuration;
using RenderLab.Editor.Diagnostics;
using RenderLab.Editor.Interop;

namespace RenderLab.Editor.Services;

/// <summary>Owns one viewport session on the UI STA; contains no Avalonia controls.</summary>
public sealed class NativeViewportService : IDisposable
{
    private readonly EditorOptions options;
    private int logSequence;
    internal uint AbiVersion { get; private set; }
    internal ulong Session { get; private set; }
    internal nint Child { get; private set; }
    internal ViewportStats Stats { get; private set; }
    internal uint Pipeline { get; private set; } = 2;
    internal float Exposure { get; private set; }
    internal bool IsPaused { get; set; }
    internal List<CloseReport> CloseReports { get; } = [];
    internal event Action<ViewportStats>? FrameCompleted;
    internal event Action<Exception>? Failed;
    internal event Action? RecreateRequested;

    public NativeViewportService(EditorOptions options) => this.options = options;

    internal nint Create(nint parent)
    {
        Dispatcher.UIThread.VerifyAccess();
        if (Session != 0)
        {
            throw new InvalidOperationException("The viewport service is already attached to a host.");
        }
        AbiVersion = NativeMethods.AbiVersion();
        if (AbiVersion != 1 || Marshal.SizeOf<ViewportStats>() != 80 || Marshal.SizeOf<CloseReport>() != 24)
        {
            throw new InvalidOperationException("Managed/native bridge ABI mismatch.");
        }
        NativeMethods.Check(NativeMethods.Create(parent, options.Backend == "dx12" ? 0u : 1u,
            options.Project, options.Shaders, Path.Combine(options.Output, $"native-{++logSequence}.log"),
            out var session, out var child));
        Session = session;
        Child = child;
        try
        {
            Configure(Pipeline, Exposure);
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

    internal void Tick(float deltaSeconds, bool hidden)
    {
        Dispatcher.UIThread.VerifyAccess();
        NativeMethods.Check(NativeMethods.Tick(Session, deltaSeconds, IsPaused || hidden ? 1u : 0u));
        NativeMethods.Check(NativeMethods.GetStats(Session, out var stats));
        Stats = stats;
        FrameCompleted?.Invoke(stats);
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
        NativeMethods.Check(NativeMethods.Destroy(Session, out var report));
        Session = 0;
        Child = 0;
        CloseReports.Add(report);
        File.WriteAllText(Path.Combine(options.Output, "close-reports.json"),
            JsonSerializer.Serialize(CloseReports, EditorDiagnostics.JsonOptions));
    }

    public void Dispose() => Close(Session);
}
