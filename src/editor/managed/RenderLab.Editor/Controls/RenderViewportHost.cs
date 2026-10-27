using System.Diagnostics;
using Avalonia.Controls;
using Avalonia.Platform;
using Avalonia.Threading;
using RenderLab.Editor.Services;

namespace RenderLab.Editor.Controls;

/// <summary>Adapts Avalonia's HWND attachment and timer to the viewport service.</summary>
internal sealed class RenderViewportHost : NativeControlHost
{
    private readonly NativeViewportService viewport;
    private readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromMilliseconds(16) };
    private readonly Stopwatch clock = Stopwatch.StartNew();
    private double previous;
    internal ulong Session { get; private set; }
    internal nint Child { get; private set; }

    internal RenderViewportHost(NativeViewportService viewport)
    {
        this.viewport = viewport;
        timer.Tick += (_, _) => Tick();
    }

    protected override IPlatformHandle CreateNativeControlCore(IPlatformHandle parent)
    {
        if (!OperatingSystem.IsWindows() || parent.HandleDescriptor != "HWND")
        {
            throw new PlatformNotSupportedException("This viewport requires a Windows HWND host.");
        }
        var child = viewport.Create(parent.Handle);
        Session = viewport.Session;
        Child = child;
        previous = clock.Elapsed.TotalSeconds;
        timer.Start();
        return new PlatformHandle(child, "HWND");
    }

    protected override void DestroyNativeControlCore(IPlatformHandle control) => CloseViewport();

    private void Tick()
    {
        if (Session == 0)
        {
            return;
        }
        try
        {
            var now = clock.Elapsed.TotalSeconds;
            var delta = (float)Math.Clamp(now - previous, 0, .1);
            previous = now;
            viewport.Tick(delta, !IsVisible);
        }
        catch (Exception error)
        {
            timer.Stop();
            viewport.ReportFailure(error);
        }
    }

    internal void CloseViewport()
    {
        Dispatcher.UIThread.VerifyAccess();
        timer.Stop();
        viewport.Close(Session);
        Session = 0;
        Child = 0;
    }
}
