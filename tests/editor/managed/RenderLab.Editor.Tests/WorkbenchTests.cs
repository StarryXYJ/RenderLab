using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Headless.NUnit;
using Microsoft.Extensions.Logging.Abstractions;
using NUnit.Framework;
using RenderLab.Editor.Diagnostics;
using RenderLab.Editor.Docking;
using RenderLab.Editor.ViewModels;
using Serilog;

[assembly: AvaloniaTestApplication(typeof(RenderLab.Editor.Tests.TestApp))]
namespace RenderLab.Editor.Tests;

public static class TestApp
{
    public static AppBuilder BuildAvaloniaApp() => AppBuilder.Configure<Application>().UseHeadless(new AvaloniaHeadlessPlatformOptions());
}

public class WorkbenchTests
{
    private static WorkbenchDockFactory Factory()
    {
        var factory = new WorkbenchDockFactory(new()
        {
            ["Scene"] = () => new TextBlock(),
            ["Settings"] = () => new TextBlock(),
            ["Render Graph"] = () => new TextBlock(),
            ["Profiler"] = () => new TextBlock(),
            ["Log"] = () => new TextBlock()
        }, NullLogger.Instance);
        factory.UseLayout(factory.CreateLayout());
        return factory;
    }
    [AvaloniaTest]
    public void DefaultAndSingleInstanceMenuRestore()
    {
        var factory = Factory();
        Assert.That(factory.Root.VisibleDockables!.Contains(factory.Root.ActiveDockable!), Is.True,
            "Root navigation must target the complete workspace, not a nested panel.");
        Assert.That(new[] { "Scene", "Settings" }.All(factory.IsOpen), Is.True);
        Assert.That(new[] { "Render Graph", "Profiler", "Log" }.Any(factory.IsOpen), Is.False);
        factory.Show("Log"); factory.Show("Log");
        Assert.That(factory.Root.VisibleDockables!.Contains(factory.Root.ActiveDockable!), Is.True);
        var diagnosticsArea = (Dock.Model.Core.IDock)factory.Panel("Log").Owner!.Owner!;
        Assert.That(WorkbenchDockFactory.Walk(diagnosticsArea).Any(panel => panel.Id == "Settings"), Is.False,
            "Opening diagnostics must preserve the settings column's full height.");
        Assert.That(WorkbenchDockFactory.Walk(factory.Root).Count(p => p.Id == "Log"), Is.EqualTo(1));
        var saved = factory.Serialize();
        var other = Factory();
        other.UseLayout(other.Restore(saved, new Rect(0, 0, 1920, 1080)));
        Assert.That(other.IsOpen("Log"), Is.True);
        Assert.That(other.IsOpen("Profiler"), Is.False);
        other.Root.VisibleDockables!.Clear();
        other.Show("Scene");
        Assert.That(other.IsOpen("Scene"), Is.True);
    }
    [AvaloniaTest]
    public void CorruptAndUnknownLayoutAreNotRuntimeObjects()
    {
        var factory = Factory();
        Assert.Throws<System.Text.Json.JsonException>(() => factory.Restore("{broken", new Rect(0, 0, 800, 600)));
        var saved = factory.Serialize().Replace("\"Version\":1", "\"Version\":99");
        Assert.Throws<InvalidDataException>(() => factory.Restore(saved, new Rect(0, 0, 800, 600)));
        Assert.That(factory.Serialize(), Does.Not.Contain("Context").And.Not.Contain("$type").And.Not.Contain("Handle"));
    }
    [Test]
    public void LargeStructuredEventsCannotExceedLogProjectionByteBudget()
    {
        var buffer = new LogBuffer();
        using var logger = new LoggerConfiguration().WriteTo.Sink(buffer).CreateLogger();
        var source = new string('S', 20000);
        var payload = new string('P', 16000);
        for (var i = 0; i < 400; ++i) { logger.ForContext("SourceContext", source).Information("{Payload}", payload); }
        var retained = buffer.Snapshot();
        Assert.That(retained.Sum(row => row.Bytes), Is.LessThanOrEqualTo(8 * 1024 * 1024));
        Assert.That(retained.All(row => row.Source.Length < source.Length), Is.True);
        Assert.That(buffer.Counts.Dropped, Is.GreaterThan(0));
    }
    [Test]
    public void LogProjectionIsBoundedAndClearPreservesTotals()
    {
        var buffer = new LogBuffer();
        using var logger = new LoggerConfiguration().WriteTo.Sink(buffer).CreateLogger();
        for (var i = 0; i < 10100; ++i) { logger.Information("Event {Index}", i); }
        logger.Error(new InvalidOperationException("native fixture"), "Failure {RequestId}", 42);
        Assert.That(buffer.Snapshot().Length, Is.LessThanOrEqualTo(10000));
        Assert.That(buffer.Counts.Dropped, Is.GreaterThan(0));
        var model = new LogViewModel(buffer) { LevelIndex = 2 };
        model.Refresh(true);
        Assert.That(model.Rows.Count, Is.EqualTo(1));
        Assert.That(model.Rows[0].Details, Does.Contain("native fixture").And.Contain("RequestId"));
        model.ClearCommand.Execute(null);
        Assert.That(model.Rows, Is.Empty);
        Assert.That(buffer.Counts.Errors, Is.EqualTo(1));
        logger.Warning("Recorded while panel closed");
        model.LevelIndex = 0; model.Refresh(true);
        Assert.That(model.Rows.Single().Message, Does.Contain("panel closed"));
    }
}
