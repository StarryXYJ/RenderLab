using RenderLab.Editor.Interop;

namespace RenderLab.Editor.Diagnostics;

internal sealed record FrameSample(ulong Frame, double? IntervalMs, double NativeMs,
    double FenceWaitMs, double AcquireMs, double RenderMs, double PresentMs, double ResizeMs,
    double DiagnosticsMs, double GraphCompileMs, double GraphPrepareMs, double GraphRecordMs,
    double? GpuMs, ulong GpuFrame, double ScenePrepareMs, ulong SessionGeneration, ulong ConfigRevision, ulong GpuConfigRevision);

internal sealed record FrameSummary(int Samples, double Fps, double FrameP95Ms,
    double NativeP95Ms, double FenceWaitP95Ms, double AcquireP95Ms, double RenderP95Ms,
    double PresentP95Ms, double? GpuP95Ms, double ScenePrepareP95Ms);

/// <summary>Bounded active-frame timings. Pauses and new sessions break interval continuity.</summary>
internal sealed class FrameTimingWindow(int capacity = 240)
{
    private readonly Queue<FrameSample> samples = new();
    private ulong previousFrame;
    private double? previousTime;
    private ulong sessionGeneration, configRevision;
    internal FrameSample? Latest { get; private set; }

    internal void Reset()
    {
        samples.Clear();
        previousFrame = 0;
        previousTime = null;
        Latest = null;
    }

    internal void Observe(ViewportStats stats, double seconds)
    {
        if ((sessionGeneration != 0 && stats.SessionGeneration != sessionGeneration) ||
            (configRevision != 0 && stats.ConfigRevision != configRevision))
        {
            Reset();
        }
        sessionGeneration = stats.SessionGeneration; configRevision = stats.ConfigRevision;
        if (stats.Frames < previousFrame)
        {
            Reset();
        }
        if (stats.Frames == previousFrame)
        {
            previousTime = null;
            return;
        }
        double? interval = previousTime is { } previous && seconds > previous
            ? (seconds - previous) * 1000 : null;
        if (stats.FrameTimeNs > 0)
        {
            interval = stats.FrameIntervalNs > 0 ? stats.FrameIntervalNs / 1e6 : null;
        }
        previousTime = seconds;
        previousFrame = stats.Frames;
        Latest = new FrameSample(stats.Frames, interval, stats.TickNs / 1e6,
            stats.FenceWaitNs / 1e6, stats.AcquireNs / 1e6, stats.RenderNs / 1e6,
            stats.PresentNs / 1e6, stats.ResizeNs / 1e6, stats.DiagnosticsNs / 1e6,
            stats.GraphCompileNs / 1e6, stats.GraphPrepareNs / 1e6, stats.GraphRecordNs / 1e6,
            stats.GpuFrame > 0 && stats.GpuConfigRevision == stats.ConfigRevision &&
                (stats.ProfilingEnabled != 0 || stats.ConfigRevision == 0) ? stats.GpuNs / 1e6 : null,
            stats.GpuFrame, stats.ScenePrepareNs / 1e6, stats.SessionGeneration, stats.ConfigRevision, stats.GpuConfigRevision);
        samples.Enqueue(Latest);
        while (samples.Count > capacity)
        {
            samples.Dequeue();
        }
    }

    internal FrameSummary Summarize() => Summarize(samples);

    internal static FrameSummary Summarize(IEnumerable<FrameSample> source)
    {
        var frames = source.ToArray();
        var intervals = frames.Where(s => s.IntervalMs.HasValue).Select(s => s.IntervalMs!.Value).ToArray();
        var gpu = frames.Where(s => s.GpuMs.HasValue).Select(s => s.GpuMs!.Value).ToArray();
        return new FrameSummary(frames.Length, intervals.Length > 0 ? 1000 / intervals.Average() : 0,
            P95(intervals), P95(frames.Select(s => s.NativeMs)), P95(frames.Select(s => s.FenceWaitMs)),
            P95(frames.Select(s => s.AcquireMs)), P95(frames.Select(s => s.RenderMs)),
            P95(frames.Select(s => s.PresentMs)), gpu.Length > 0 ? P95(gpu) : null,
            P95(frames.Select(s => s.ScenePrepareMs)));
    }

    private static double P95(IEnumerable<double> source)
    {
        var values = source.Order().ToArray();
        return values.Length > 0 ? values[(int)Math.Ceiling(values.Length * .95) - 1] : 0;
    }
}

/// <summary>Monotonic-time UI refresh gate, independent of cumulative renderer counters.</summary>
internal sealed class RefreshGate(double seconds = .25)
{
    private double last = double.NegativeInfinity;
    internal bool TryPublish(double now)
    {
        if (now >= last && now - last < seconds)
        {
            return false;
        }
        last = now;
        return true;
    }
}
