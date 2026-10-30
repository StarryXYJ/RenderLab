using System.Runtime.InteropServices;
using System.Text;

namespace RenderLab.Editor.Interop;

[StructLayout(LayoutKind.Sequential)]
internal struct ViewportStats
{
    public ulong Frames, Resizes, PausedTicks, InputFrames, NativeBytes, PeakBytes;
    public uint Width, Height, Dpi, ReflectionSamples, Focused, Captured, PressedKeys, Pipeline;
    public ulong TickNs, FenceWaitNs, AcquireNs, RenderNs, PresentNs, ResizeNs;
    public ulong DiagnosticsNs, GraphCompileNs, GraphPrepareNs, GraphRecordNs, GpuNs, GpuFrame;
    public uint ValidationEnabled, ProfilingEnabled;
    public ulong ScenePrepareNs;
    public ulong FrameIntervalNs, FrameTimeNs, DroppedSamples;
    public uint Activity, Ready, UiThreadId, RenderThreadId;
    public ulong ConfigRevision, GpuConfigRevision, SessionGeneration;
}

[StructLayout(LayoutKind.Sequential)]
internal struct CloseReport
{
    public ulong RemainingBytes, PeakBytes;
    public uint ValidationErrors, WorkerFailed;
}

internal static class NativeMethods
{
    private const string Library = "renderlab_editor_bridge";

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "rl_editor_abi_version")]
    internal static extern uint AbiVersion();
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "rl_viewport_configure_v2")]
    internal static extern int ConfigureV2(ulong session, in ViewportConfig config);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "rl_viewport_get_config")]
    internal static extern int GetConfig(ulong session, out ViewportConfigState state, out ViewportConfig defaults);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "rl_viewport_config_text")]
    internal static extern int ConfigText(ulong session, uint kind, [Out] byte[]? buffer, uint capacity, out uint required);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "rl_viewport_step")]
    internal static extern int Step(ulong session);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "rl_viewport_stop")]
    internal static extern int Stop(ulong session, out CloseReport report);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "rl_viewport_read_logs")]
    internal static extern int ReadLogs(ulong session, [Out] byte[]? buffer, uint capacity, out uint required);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "rl_viewport_camera")]
    internal static extern int Camera(ulong session, [In, Out] float[] pose, uint write);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "rl_viewport_create")]
    internal static extern int Create(nint parent, uint backend, uint validation, uint profiling,
        uint targetFps, uint continuous,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string project,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string shaders,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string log,
        out ulong session, out nint child);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "rl_viewport_update")]
    internal static extern int Update(ulong session, uint paused);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "rl_viewport_configure")]
    internal static extern int Configure(ulong session, uint pipeline, float exposure, uint reset);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "rl_viewport_get_stats")]
    internal static extern int GetStats(ulong session, out ViewportStats stats);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "rl_viewport_read_frames")]
    internal static extern int ReadFrames(ulong session, [Out] ViewportStats[] frames, uint capacity, out uint count);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "rl_viewport_destroy")]
    internal static extern int Destroy(ulong session, out CloseReport report);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "rl_editor_last_error")]
    internal static extern int LastError([Out] byte[]? buffer, uint capacity, out uint required);

    internal static string Error()
    {
        LastError(null, 0, out var size);
        var buffer = new byte[size];
        LastError(buffer, size, out _);
        return Encoding.UTF8.GetString(buffer, 0, Math.Max(0, buffer.Length - 1));
    }

    internal static void Check(int status)
    {
        if (status != 0)
        {
            throw new InvalidOperationException($"Native viewport ({status}): {Error()}");
        }
    }

    [DllImport("user32.dll")]
    internal static extern nint SetFocus(nint window);
    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool ReleaseCapture();

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool IsChild(nint parent, nint child);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    internal static extern nint SendMessage(nint window, uint message, nuint w, nint l);
}
