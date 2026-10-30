using NUnit.Framework;
using RenderLab.Editor.Services;

namespace RenderLab.Editor.Tests;

public class DeviceSwitchTests
{
    [Test]
    public void CancelledSwitchCannotPublishOrStartRollback()
    {
        var initial = new DeviceSelection("dx12", true);
        var controller = new DeviceSwitchTransaction(initial);
        var attempts = 0;
        Assert.CatchAsync<OperationCanceledException>(async () => await controller.RunAsync(new("vulkan", true), _ =>
        {
            ++attempts;
            return Task.FromCanceled(new CancellationToken(true));
        }, CancellationToken.None));
        Assert.That(controller.Current, Is.EqualTo(initial));
        Assert.That(controller.Busy, Is.False);
        Assert.That(attempts, Is.EqualTo(1));
        controller.ObserveReady(new("vulkan", false));
        Assert.That(controller.Current, Is.EqualTo(new DeviceSelection("vulkan", false)),
            "A dock-created replacement must report the device that actually produced its confirmed frame.");
    }
    [Test]
    public async Task FailedTargetRollsBackExactlyOnce()
    {
        var original = new DeviceSelection("dx12", true);
        var controller = new DeviceSwitchTransaction(original);
        var attempts = new List<DeviceSelection>();
        await controller.RunAsync(new("vulkan", false), selection =>
        {
            attempts.Add(selection);
            return selection.Backend == "vulkan" ? Task.FromException(new Exception("first frame failed")) : Task.CompletedTask;
        }, CancellationToken.None);
        Assert.That(attempts.Select(a => a.Backend), Is.EqualTo(new[] { "vulkan", "dx12" }));
        Assert.That(controller.Current, Is.EqualTo(original));
        Assert.That(controller.Failed, Is.False);
        Assert.That(controller.Error, Does.Contain("first frame failed"));
    }

    [Test]
    public async Task DoubleFailureStopsUntilExplicitRetry()
    {
        var controller = new DeviceSwitchTransaction(new("dx12", true));
        var attempts = 0;
        await controller.RunAsync(new("vulkan", true), _ =>
        {
            ++attempts;
            return Task.FromException(new Exception("device unavailable"));
        }, CancellationToken.None);
        Assert.That(attempts, Is.EqualTo(2));
        Assert.That(controller.Failed, Is.True);
        Assert.That(controller.Busy, Is.False);
        Assert.That(controller.Error, Does.Contain("Target:").And.Contain("Rollback:"));
        await controller.RunAsync(new("vulkan", false), _ => Task.CompletedTask, CancellationToken.None);
        Assert.That(controller.Failed, Is.False);
        Assert.That(controller.Current, Is.EqualTo(new DeviceSelection("vulkan", false)));
    }
}
