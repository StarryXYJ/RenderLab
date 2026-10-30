using NUnit.Framework;
using RenderLab.Editor.Diagnostics;
using RenderLab.Editor.Interop;

namespace RenderLab.Editor.Tests;

[TestFixture]
public sealed class FrameTimingTests
{
    [Test]
    public void NewConfigDoesNotReuseOldGpuTimingOrFrameDistribution()
    {
        var window = new FrameTimingWindow();
        var first = Frame(1); first.SessionGeneration = 11; first.ConfigRevision = 2;
        window.Observe(first, 0);
        var next = Frame(2); next.SessionGeneration = 11; next.ConfigRevision = 3;
        next.GpuFrame = 1; next.GpuConfigRevision = 2; next.GpuNs = 5000000; next.ProfilingEnabled = 1;
        window.Observe(next, .016);
        Assert.That(window.Summarize().Samples, Is.EqualTo(1));
        Assert.That(window.Latest!.ConfigRevision, Is.EqualTo(3));
        Assert.That(window.Latest.GpuMs, Is.Null);
    }
    private static ViewportStats Frame(ulong frame, ulong cpu = 1_000_000) => new()
    {
        Frames = frame,
        TickNs = cpu,
        RenderNs = cpu
    };

    [Test]
    public void IntervalsAndNearestRankP95UseActualFrameTimes()
    {
        var window = new FrameTimingWindow();
        window.Observe(Frame(1), 0);
        window.Observe(Frame(2, 2_000_000), .01);
        window.Observe(Frame(3, 3_000_000), .03);
        var result = window.Summarize();
        Assert.Multiple(() =>
        {
            Assert.That(result.Fps, Is.EqualTo(1000 / 15.0).Within(.001));
            Assert.That(result.FrameP95Ms, Is.EqualTo(20).Within(.001));
            Assert.That(result.NativeP95Ms, Is.EqualTo(3));
            Assert.That(result.GpuP95Ms, Is.Null);
        });
    }

    [Test]
    public void PausesDoNotBecomeLongFramesOrAdditionalSamples()
    {
        var window = new FrameTimingWindow();
        window.Observe(Frame(1), 0);
        window.Observe(Frame(2), .01);
        window.Observe(Frame(2), 10);
        window.Observe(Frame(3), 20);
        Assert.Multiple(() =>
        {
            Assert.That(window.Summarize().Samples, Is.EqualTo(3));
            Assert.That(window.Summarize().Fps, Is.EqualTo(100).Within(.001));
            Assert.That(window.Latest!.IntervalMs, Is.Null);
        });
    }

    [Test]
    public void NewSessionClearsOldTimings()
    {
        var window = new FrameTimingWindow();
        window.Observe(Frame(30, 90_000_000), 100);
        window.Observe(Frame(1), 101);
        Assert.That(window.Summarize().Samples, Is.EqualTo(1));
        Assert.That(window.Summarize().NativeP95Ms, Is.EqualTo(1));
    }

    [Test]
    public void RollingWindowEvictsOldSpikesAndPreservesGpuSourceFrame()
    {
        var window = new FrameTimingWindow(2);
        window.Observe(Frame(1, 100_000_000), 0);
        window.Observe(Frame(2), .01);
        var stats = Frame(3);
        stats.GpuFrame = 1;
        stats.GpuNs = 2_000_000;
        window.Observe(stats, .02);
        Assert.Multiple(() =>
        {
            Assert.That(window.Summarize().Samples, Is.EqualTo(2));
            Assert.That(window.Summarize().NativeP95Ms, Is.EqualTo(1));
            Assert.That(window.Latest!.GpuFrame, Is.EqualTo(1));
            Assert.That(window.Summarize().GpuP95Ms, Is.EqualTo(2));
        });
    }

    [Test]
    public void UiRefreshRemainsTimeLimitedAcrossPauseAndResume()
    {
        var refresh = new RefreshGate();
        var count = 0;
        for (var tick = 0; tick < 100; ++tick)
        {
            if (refresh.TryPublish(tick * .01))
            {
                ++count;
            }
        }
        Assert.That(count, Is.EqualTo(4));
    }

    [Test]
    public void NativeIntervalsSurviveBatchedUiDeliveryAndExcludeIdleGaps()
    {
        var window = new FrameTimingWindow();
        var stats = Frame(1);
        stats.FrameTimeNs = 1_000_000_000;
        window.Observe(stats, 100);
        stats.Frames = 2;
        stats.FrameTimeNs += 16_000_000;
        stats.FrameIntervalNs = 16_000_000;
        window.Observe(stats, 100);
        stats.Frames = 3;
        stats.FrameTimeNs += 10_000_000_000;
        stats.FrameIntervalNs = 0;
        window.Observe(stats, 110);
        Assert.Multiple(() =>
        {
            Assert.That(window.Summarize().Fps, Is.EqualTo(62.5).Within(.001));
            Assert.That(window.Summarize().FrameP95Ms, Is.EqualTo(16));
            Assert.That(window.Latest!.IntervalMs, Is.Null);
        });
    }
}
