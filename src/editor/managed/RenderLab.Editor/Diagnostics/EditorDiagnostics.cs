using System.Text.Json;
using RenderLab.Editor.Configuration;

namespace RenderLab.Editor.Diagnostics;

internal static class EditorDiagnostics
{
    internal static JsonSerializerOptions JsonOptions { get; } = new()
    {
        IncludeFields = true,
        WriteIndented = true
    };

    internal static void WriteError(EditorOptions options, Exception error) =>
        File.WriteAllText(Path.Combine(options.Output, "managed-error.txt"), error.ToString());
}
