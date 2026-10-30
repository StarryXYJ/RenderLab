using Serilog;
using Serilog.Core;
using Serilog.Events;
using Serilog.Formatting.Json;
using Serilog.Formatting;
using RenderLab.Editor.Configuration;

namespace RenderLab.Editor.Diagnostics;

public sealed record LogRow(long Id, DateTimeOffset Time, LogEventLevel Level, string Source,
    string Message, string Details)
{
    public string Display => $"{Time.ToLocalTime():HH:mm:ss.fff}  {Level,-11}  {Source}  {Message}";
    public string TimeText => Time.ToLocalTime().ToString("HH:mm:ss.fff", System.Globalization.CultureInfo.InvariantCulture);
    public string LevelColor => Level >= LogEventLevel.Error ? "#F09B8F" : Level == LogEventLevel.Warning ? "#D9B779" : "#A7C6D8";
    public int Bytes => (Message.Length + Details.Length + Source.Length) * 2 + 128;
}

/// <summary>A bounded value projection. Producers never touch Avalonia controls.</summary>
public sealed class LogBuffer : ILogEventSink
{
    private readonly object gate = new();
    private readonly Queue<LogRow> rows = new();
    private long sequence, warnings, errors, dropped;
    private int bytes;
    internal long Revision { get { lock (gate) { return sequence; } } }
    internal (long Warnings, long Errors, long Dropped) Counts
    {
        get { lock (gate) { return (warnings, errors, dropped); } }
    }

    public void Emit(LogEvent logEvent)
    {
        try
        {
            static string Bound(string value) => value.Length > 8192 ? value[..8192] + " [truncated; see log file]" : value;
            var source = Bound(logEvent.Properties.TryGetValue("SourceContext", out var context) ? context.ToString().Trim('"') : "Editor");
            var message = Bound(logEvent.RenderMessage(System.Globalization.CultureInfo.InvariantCulture));
            var details = Bound(string.Join(Environment.NewLine, logEvent.Properties.Select(p => $"{p.Key}: {p.Value}")) +
                (logEvent.Exception is null ? "" : Environment.NewLine + logEvent.Exception));
            lock (gate)
            {
                var row = new LogRow(++sequence, logEvent.Timestamp, logEvent.Level, source, message, details);
                if (logEvent.Level == LogEventLevel.Warning) { ++warnings; }
                if (logEvent.Level >= LogEventLevel.Error) { ++errors; }
                while (rows.Count > 0 && (rows.Count >= 10000 || bytes + row.Bytes > 8 * 1024 * 1024))
                {
                    bytes -= rows.Dequeue().Bytes;
                    ++dropped;
                }
                rows.Enqueue(row);
                bytes += row.Bytes;
            }
        }
        catch (Exception error) { Console.Error.WriteLine($"Log projection failed: {error.Message}"); }
    }

    internal LogRow[] Snapshot() { lock (gate) { return rows.ToArray(); } }
    internal void Clear() { lock (gate) { rows.Clear(); bytes = 0; ++sequence; } }
}

internal sealed class EditorLogging : IDisposable
{
    private sealed class UtcJsonFormatter : ITextFormatter
    {
        private readonly JsonFormatter inner = new();
        public void Format(LogEvent value, TextWriter output) => inner.Format(new LogEvent(value.Timestamp.ToUniversalTime(),
            value.Level, value.Exception, value.MessageTemplate, value.Properties.Select(p => new LogEventProperty(p.Key, p.Value))), output);
    }
    internal LogBuffer Buffer { get; } = new();
    internal Logger Logger { get; }
    internal string DirectoryPath { get; }
    internal string SinkError { get; private set; } = "";
    internal EditorLogging(EditorOptions options)
    {
        DirectoryPath = Path.Combine(options.Output, "logs");
        Directory.CreateDirectory(DirectoryPath);
        Serilog.Debugging.SelfLog.Enable(message =>
        {
            SinkError = message.Length > 4096 ? message[..4096] : message;
            Console.Error.WriteLine(message);
        });
        Logger = new LoggerConfiguration().MinimumLevel.Information()
            .Enrich.WithProperty("EditorRunId", Guid.NewGuid().ToString("N"))
            .WriteTo.File(new UtcJsonFormatter(), Path.Combine(DirectoryPath, "editor-.jsonl"),
                rollingInterval: RollingInterval.Day, fileSizeLimitBytes: 16 * 1024 * 1024,
                rollOnFileSizeLimit: true, retainedFileCountLimit: 10, shared: true)
            .WriteTo.Sink(Buffer).CreateLogger();
        Logger.Information("Editor starting with {Backend} and {Project}", options.Backend, options.Project);
    }
    public void Dispose() { Logger.Information("Editor logging stopped"); Logger.Dispose(); }
}
