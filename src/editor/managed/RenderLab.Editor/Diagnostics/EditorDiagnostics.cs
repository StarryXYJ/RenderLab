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

    internal static void WriteError(EditorOptions options, Exception error)
    {
        Program.Logging?.Logger.Error(error, "Editor operation failed");
        try { File.WriteAllText(Path.Combine(options.Output, "managed-error.txt"), error.ToString()); }
        catch (Exception writeError) { Console.Error.WriteLine(writeError); }
    }
}
