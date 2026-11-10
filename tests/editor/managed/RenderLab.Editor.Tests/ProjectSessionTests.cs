using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging.Abstractions;
using NUnit.Framework;
using RenderLab.Editor.Configuration;
using RenderLab.Editor.Services;
using RenderLab.Editor.ViewModels;

namespace RenderLab.Editor.Tests;

public class ProjectSessionTests
{
    [Test]
    public void EachProjectOwnsSeparateScopedServicesAndDeviceSelection()
    {
        var options = new EditorOptions("dx12", "initial", "shaders", "output", false, false, true, 2, 0, 0);
        using var provider = new ServiceCollection()
            .AddSingleton(new StartupOptions(options))
            .AddScoped<ProjectContext>()
            .AddScoped(p => p.GetRequiredService<ProjectContext>().Options)
            .AddScoped<NativeViewportService>()
            .AddScoped<RenderSettingsViewModel>()
            .AddScoped<MainWindowViewModel>()
            .AddSingleton<ProjectSessionFactory>()
            .BuildServiceProvider(new ServiceProviderOptions { ValidateScopes = true });
        var factory = provider.GetRequiredService<ProjectSessionFactory>();
        using var first = factory.Create("first");
        using var second = factory.Create("second", new DeviceSelection("vulkan", true));
        Assert.Multiple(() =>
        {
            Assert.That(first.Options.Project, Is.EqualTo("first"));
            Assert.That(second.Options.Project, Is.EqualTo("second"));
            Assert.That(second.Options.Backend, Is.EqualTo("vulkan"));
            Assert.That(second.Options.Validation, Is.True);
            Assert.That(first.Viewport, Is.Not.SameAs(second.Viewport));
            Assert.That(first.ViewModel, Is.Not.SameAs(second.ViewModel));
        });
    }

    [Test]
    public async Task FailedTargetReopensPreviousProjectOnce()
    {
        var attempts = new List<string>();
        var result = await ProjectSwitchTransaction.RunAsync("bad", "old", (path, _) =>
        {
            attempts.Add(path);
            return path == "bad" ? Task.FromException(new InvalidOperationException("load failed")) : Task.CompletedTask;
        }, CancellationToken.None);
        Assert.That(attempts, Is.EqualTo(new[] { "bad", "old" }));
        Assert.That(result.TargetError!.Message, Is.EqualTo("load failed"));
        Assert.That(result.Restored, Is.True);
        Assert.That(result.RollbackError, Is.Null);
    }

    [Test]
    public async Task DoubleFailureIsReportedAndCancellationDoesNotRollBack()
    {
        var result = await ProjectSwitchTransaction.RunAsync("bad", "old",
            (path, _) => Task.FromException(new InvalidOperationException(path)), CancellationToken.None);
        Assert.That(result.Restored, Is.False);
        Assert.That(result.RollbackError!.Message, Is.EqualTo("old"));
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        var attempts = 0;
        Assert.CatchAsync<OperationCanceledException>(async () => await ProjectSwitchTransaction.RunAsync("bad", "old", (_, token) =>
        {
            ++attempts;
            return Task.FromCanceled(token);
        }, cancellation.Token));
        Assert.That(attempts, Is.EqualTo(1));
    }

    [Test]
    public void RecentProjectsPersistDeduplicateAndBoundEntries()
    {
        var directory = Path.Combine(Path.GetTempPath(), "renderlab-recent-" + Guid.NewGuid().ToString("N"));
        var path = Path.Combine(directory, "recent.json");
        try
        {
            var recent = new RecentProjects(NullLogger<RecentProjects>.Instance, path);
            var projects = Enumerable.Range(0, 12).Select(i => Path.Combine(directory, $"{i}.rlproject")).ToArray();
            foreach (var project in projects) { recent.Add(project); }
            recent.Add(projects[5].ToUpperInvariant());
            var restored = new RecentProjects(NullLogger<RecentProjects>.Instance, path);
            Assert.That(restored.Items, Has.Count.EqualTo(10));
            Assert.That(restored.Items[0], Is.EqualTo(projects[5].ToUpperInvariant()));
            Assert.That(restored.Items.Count(p => string.Equals(p, projects[5], StringComparison.OrdinalIgnoreCase)), Is.EqualTo(1));
            restored.Remove(projects[5]);
            Assert.That(restored.Items, Has.Count.EqualTo(9));
            restored.Clear();
            Assert.That(new RecentProjects(NullLogger<RecentProjects>.Instance, path).Items, Is.Empty);
        }
        finally { if (Directory.Exists(directory)) { Directory.Delete(directory, true); } }
    }
}
