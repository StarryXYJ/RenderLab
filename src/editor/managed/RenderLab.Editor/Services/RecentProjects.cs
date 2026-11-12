using System.Text.Json;
using Microsoft.Extensions.Logging;

namespace RenderLab.Editor.Services;

public sealed class RecentProjects
{
    private readonly string path;
    private readonly ILogger<RecentProjects> logger;
    public RecentProjects(ILogger<RecentProjects> logger) : this(logger, Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "RenderLab", "Editor", "recent-projects.json")) { }
    internal RecentProjects(ILogger<RecentProjects> logger, string path) { this.logger = logger; this.path = path; }
    private readonly List<string> items = [];
    private bool loaded;
    internal IReadOnlyList<string> Items { get { Load(); return items; } }
    private void Load()
    {
        if (loaded) { return; }
        loaded = true;
        try
        {
            if (File.Exists(path))
            {
                var paths = JsonSerializer.Deserialize<string[]>(File.ReadAllText(path)) ?? [];
                items.AddRange(paths.Where(p => !string.IsNullOrWhiteSpace(p) && Path.IsPathFullyQualified(p))
                    .Distinct(StringComparer.OrdinalIgnoreCase).Take(10));
            }
        }
        catch (Exception error) { logger.LogWarning(error, "Unable to read recent projects"); }
    }
    internal void Add(string project)
    {
        Load();
        items.RemoveAll(p => string.Equals(p, project, StringComparison.OrdinalIgnoreCase));
        items.Insert(0, project);
        if (items.Count > 10) { items.RemoveRange(10, items.Count - 10); }
        Save();
    }
    internal void Remove(string project) { Load(); items.RemoveAll(p => string.Equals(p, project, StringComparison.OrdinalIgnoreCase)); Save(); }
    internal void Clear() { Load(); items.Clear(); Save(); }
    private void Save()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            var temporary = path + ".tmp";
            File.WriteAllText(temporary, JsonSerializer.Serialize(items));
            File.Move(temporary, path, true);
        }
        catch (Exception error) { logger.LogWarning(error, "Unable to save recent projects"); }
    }
}
