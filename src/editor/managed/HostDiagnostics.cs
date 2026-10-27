using System.Text.Json;

namespace RenderLab.Editor.Host;

internal static class HostDiagnostics
{
    internal static JsonSerializerOptions JsonOptions { get; } = new()
    {
        IncludeFields = true,
        WriteIndented = true
    };

    internal static void WriteError(HostOptions options, Exception error) =>
        File.WriteAllText(Path.Combine(options.Output, "managed-error.txt"), error.ToString());
}
