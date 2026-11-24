using System.Text.Json;
using RenderLab.Editor.Configuration;
using RenderLab.Editor.Interop;

namespace RenderLab.Editor.Services;

internal sealed record ProjectScene(string Id, string Path);
internal sealed record ProjectScenes(string StartScene, IReadOnlyList<ProjectScene> Scenes)
{
    internal bool Contains(string id) => Scenes.Any(scene => scene.Id == id);

    internal string Select(string? requested, string? saved)
    {
        if (requested is not null)
        {
            if (!Contains(requested)) { throw new InvalidDataException("Selected scene is not in this project."); }
            return requested;
        }
        return saved is not null && Contains(saved) ? saved : StartScene;
    }

    internal static ProjectScenes Load(EditorOptions options)
    {
        var status = NativeMethods.ProjectScenes(options.Project, options.Shaders, null, 0, out var size);
        if (status != 5) { NativeMethods.Check(status); }
        for (var attempt = 0; attempt < 3; ++attempt)
        {
            if (size == 0 || size > 4 * 1024 * 1024) { throw new InvalidDataException("Scene catalog exceeds budget."); }
            var bytes = new byte[size];
            status = NativeMethods.ProjectScenes(options.Project, options.Shaders, bytes, size, out size);
            if (status == 5) { continue; }
            NativeMethods.Check(status);
            using var json = JsonDocument.Parse(bytes.AsMemory(0, checked((int)size - 1)));
            var root = json.RootElement;
            if (root.GetProperty("version").GetInt32() != 1) { throw new InvalidDataException("Unsupported scene catalog."); }
            var start = root.GetProperty("startScene").GetString() ?? throw new InvalidDataException("Missing start scene.");
            var scenes = root.GetProperty("scenes").EnumerateArray().Select(entry => new ProjectScene(
                entry.GetProperty("id").GetString()!, entry.GetProperty("path").GetString()!)).ToArray();
            var result = new ProjectScenes(start, scenes);
            if (!result.Contains(start)) { throw new InvalidDataException("Project start scene is not indexed as a scene."); }
            return result;
        }
        throw new InvalidOperationException("Scene catalog changed repeatedly during copy.");
    }
}
