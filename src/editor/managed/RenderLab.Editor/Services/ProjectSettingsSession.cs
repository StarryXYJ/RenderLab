using System.Text.Json;
using RenderLab.Editor.Configuration;
using RenderLab.Project;

namespace RenderLab.Editor.Services;

/// <summary>Owns one project's optional settings schema and values for the project scope.</summary>
public sealed class ProjectSettingsSession : IDisposable
{
    private ProjectSettingsAssembly? assembly;
    internal IProjectSettingsSchema? Schema => assembly?.Schema;
    internal object? Values { get; private set; }
    internal string? UserSettingsError { get; private set; }

    public ProjectSettingsSession(EditorOptions options)
    {
        using var manifest = JsonDocument.Parse(File.ReadAllText(options.Project));
        if (!manifest.RootElement.TryGetProperty("settings", out var settings)) { return; }
        if (settings.ValueKind != JsonValueKind.Object || settings.GetProperty("version").GetInt32() != 1)
        { throw new InvalidDataException("Unsupported project settings declaration."); }
        var root = Path.GetDirectoryName(Path.GetFullPath(options.Project))!;
        var path = Inside(root, settings.GetProperty("assembly").GetString());
        var type = settings.GetProperty("entry").GetString();
        if (string.IsNullOrWhiteSpace(type) || type.Length > 512)
        { throw new InvalidDataException("Invalid project settings entry type."); }
        try
        {
            assembly = ProjectSettingsAssembly.Load(path, type);
            var schema = Schema!;
            Values = schema.CreateDefaults();
            if (settings.TryGetProperty("defaults", out var defaults))
            {
                var defaultsPath = Inside(root, defaults.GetString());
                Values = schema.Apply(Values, ReadLimited(defaultsPath));
            }
            if (ProjectUserSettings.Persistent(options))
            {
                var userPath = Path.Combine(ProjectUserSettings.DirectoryFor(options), "project-settings.json");
                if (File.Exists(userPath))
                {
                    try { Values = schema.Apply(Values, ReadLimited(userPath)); }
                    catch (Exception error)
                    {
                        UserSettingsError = error.Message;
                        Program.Logging?.Logger.Warning(error, "Unable to restore project settings; preserving the file");
                    }
                }
            }
        }
        catch
        {
            Dispose();
            throw;
        }
    }

    private static string ReadLimited(string path)
    {
        if (new FileInfo(path).Length > 65536) { throw new InvalidDataException("Project settings exceed budget."); }
        return File.ReadAllText(path);
    }

    private static string Inside(string root, string? relative)
    {
        if (string.IsNullOrWhiteSpace(relative) || Path.IsPathRooted(relative))
        { throw new InvalidDataException("Project settings path must be relative."); }
        var path = Path.GetFullPath(relative, root);
        var remainder = Path.GetRelativePath(root, path);
        if (remainder == ".." || remainder.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal) ||
            Path.IsPathRooted(remainder))
        { throw new InvalidDataException("Project settings path escapes the project."); }
        var current = root;
        foreach (var part in remainder.Split(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar))
        {
            current = Path.Combine(current, part);
            if ((File.Exists(current) || Directory.Exists(current)) &&
                (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
            { throw new InvalidDataException("Project settings paths cannot contain symbolic links."); }
        }
        return path;
    }

    public void Dispose()
    {
        Values = null;
        assembly?.Dispose();
        assembly = null;
    }
}
