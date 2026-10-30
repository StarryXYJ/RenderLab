using NUnit.Framework;
using RenderLab.Editor.Configuration;
using RenderLab.Editor.Diagnostics;

namespace RenderLab.Editor.Tests;

public class LoggingIntegrationTests
{
    private static EditorOptions Options(string suffix)
    {
        var output = Path.Combine(TestContext.CurrentContext.WorkDirectory, "out", "m72f", "logging-" + suffix + "-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(output);
        return new("dx12", "fixture", "fixture", output, false, true, true, 2, 0, 0);
    }

    [Test]
    public void LockedLogFileDoesNotDisableTheIndependentMemoryProjection()
    {
        var options = Options("locked");
        var directory = Path.Combine(options.Output, "logs"); Directory.CreateDirectory(directory);
        var path = Path.Combine(directory, $"editor-{DateTime.Now:yyyyMMdd}.jsonl");
        using var locked = new FileStream(path, FileMode.Create, FileAccess.ReadWrite, FileShare.None);
        using var logging = new EditorLogging(options);
        logging.Logger.Information("File unavailable; memory remains observable");
        Assert.That(logging.Buffer.Snapshot().Any(row => row.Message.Contains("memory remains observable")), Is.True);
        Assert.That(logging.SinkError, Is.Not.Empty);
    }

    [Test]
    public void UnwritableFileTargetKeepsTheMemorySinkAndFailureStatus()
    {
        var options = Options("unwritable");
        var directory = Path.Combine(options.Output, "logs");
        Directory.CreateDirectory(Path.Combine(directory, $"editor-{DateTime.Now:yyyyMMdd}.jsonl"));
        using var logging = new EditorLogging(options);
        logging.Logger.Error("File target is a directory; independent memory still records");
        Assert.That(logging.Buffer.Snapshot().Any(row => row.Message.Contains("independent memory still records")), Is.True);
        Assert.That(logging.SinkError, Is.Not.Empty);
        Assert.That(Directory.GetFiles(directory, "*.jsonl"), Is.Empty,
            "This fixture must actually prevent file persistence, rather than only roll to another file.");
    }

    [Test]
    public void ProductionRollingLimitsAndMessageFloodRemainBounded()
    {
        var options = Options("rolling");
        string directory;
        using (var logging = new EditorLogging(options))
        {
            directory = logging.DirectoryPath;
            var payload = new string('L', 1024 * 1024);
            for (var index = 0; index < 185; ++index) { logging.Logger.Information("Rolling {Index}: {Payload}", index, payload); }
            for (var index = 0; index < 10500; ++index) { logging.Logger.Information("Flood {Index}", index); }
            logging.Logger.Warning("Final drain sentinel {Sequence}", 42);
            Assert.That(logging.Buffer.Snapshot().Length, Is.LessThanOrEqualTo(10000));
            Assert.That(logging.Buffer.Snapshot().Sum(row => (long)row.Bytes), Is.LessThanOrEqualTo(8 * 1024 * 1024));
            Assert.That(logging.Buffer.Counts.Dropped, Is.GreaterThan(0));
            Assert.That(logging.Buffer.Snapshot().Last().Message, Does.Contain("Final drain sentinel"));
        }
        var files = Directory.GetFiles(directory, "*.jsonl").Select(path => new FileInfo(path)).ToArray();
        Assert.That(files.Length, Is.InRange(2, 10));
        Assert.That(files.Max(file => file.Length), Is.LessThan(18 * 1024 * 1024));
        Assert.That(files.Any(file => File.ReadLines(file.FullName).Any(line => line.Contains("Final drain sentinel"))), Is.True);
    }
}
