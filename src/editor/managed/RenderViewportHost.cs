using System.Diagnostics;
using System.Runtime.InteropServices;
using Avalonia.Controls;
using Avalonia.Platform;
using Avalonia.Threading;

namespace RenderLab.Editor.Host;

internal sealed class RenderViewportHost : NativeControlHost
{
    private readonly HostOptions options;
    private readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromMilliseconds(16) };
    private readonly Stopwatch clock = Stopwatch.StartNew();
    private double previous;
    private uint pipeline = 2;
    private float exposure;
    private bool disposed;
    private static int logSequence;
    internal ulong Session { get; private set; }
    internal nint Child { get; private set; }
    internal ViewportStats Stats { get; private set; }
    internal bool Paused { get; set; }
    internal event Action<ViewportStats>? FrameCompleted;
    internal event Action<Exception>? Failed;
    internal event Action<CloseReport>? NativeClosed;

    internal RenderViewportHost(HostOptions options)
    {
        this.options = options;
        timer.Tick += (_, _) => Tick();
    }

    protected override IPlatformHandle CreateNativeControlCore(IPlatformHandle parent)
    {
        if (!OperatingSystem.IsWindows() || parent.HandleDescriptor != "HWND")
        {
            throw new PlatformNotSupportedException("This viewport prototype requires a Windows HWND host.");
        }
        if (NativeMethods.AbiVersion() != 1 || Marshal.SizeOf<ViewportStats>() != 80 || Marshal.SizeOf<CloseReport>() != 24)
        {
            throw new InvalidOperationException("Managed/native bridge ABI mismatch.");
        }
        NativeMethods.Check(NativeMethods.Create(parent.Handle, options.Backend == "dx12" ? 0u : 1u,
            options.Project, options.Shaders, Path.Combine(options.Output, $"native-{++logSequence}.log"),
            out var session, out var child));
        Session = session;
        Child = child;
        disposed = false;
        try
        {
            Configure(pipeline, exposure);
        }
        catch
        {
            CloseViewport();
            throw;
        }
        previous = clock.Elapsed.TotalSeconds;
        timer.Start();
        return new PlatformHandle(child, "HWND");
    }

    protected override void DestroyNativeControlCore(IPlatformHandle control)
    {
        CloseViewport();
    }

    internal void Configure(uint selectedPipeline, float selectedExposure, bool reset = false)
    {
        Dispatcher.UIThread.VerifyAccess();
        if (Session != 0)
        {
            NativeMethods.Check(NativeMethods.Configure(Session, selectedPipeline, selectedExposure, reset ? 1u : 0u));
        }
        pipeline = selectedPipeline;
        exposure = selectedExposure;
    }

    private void Tick()
    {
        try
        {
            var now = clock.Elapsed.TotalSeconds;
            NativeMethods.Check(NativeMethods.Tick(Session, (float)Math.Clamp(now - previous, 0, .1), Paused || !IsVisible ? 1u : 0u));
            previous = now;
            NativeMethods.Check(NativeMethods.GetStats(Session, out var stats));
            Stats = stats;
            FrameCompleted?.Invoke(stats);
        }
        catch (Exception error)
        {
            timer.Stop();
            Failed?.Invoke(error);
        }
    }

    internal void CloseViewport()
    {
        Dispatcher.UIThread.VerifyAccess();
        timer.Stop();
        if (disposed || Session == 0)
        {
            return;
        }
        NativeMethods.Check(NativeMethods.Destroy(Session, out var report));
        disposed = true;
        Session = 0;
        Child = 0;
        NativeClosed?.Invoke(report);
    }
}
