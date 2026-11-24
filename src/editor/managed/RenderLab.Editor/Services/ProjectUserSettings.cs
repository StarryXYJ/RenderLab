using System.Text.Json;
using System.Text.Json.Serialization;
using RenderLab.Editor.Configuration;
using RenderLab.Editor.Interop;

namespace RenderLab.Editor.Services;

internal sealed record ProjectPreferences(
    [property: JsonRequired] int Version,
    [property: JsonRequired] string Pipeline,
    [property: JsonRequired] uint TargetFps);

/// <summary>Project-local user state; the manifest ID separates projects sharing a directory.</summary>
internal static class ProjectUserSettings
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true
    };

    internal static bool Persistent(EditorOptions options) =>
        !options.Smoke && !options.Isolated && options.BenchmarkSeconds == 0 && options.SoakSeconds == 0;

    internal static string LegacyDirectory(EditorOptions options) =>
        Path.Combine(Path.GetDirectoryName(options.Project)!, "UserSettings", "Editor");

    internal static string DirectoryFor(EditorOptions options)
    {
        if (!Persistent(options)) { return options.Output; }
        using var manifest = JsonDocument.Parse(File.ReadAllText(options.Project));
        var id = manifest.RootElement.GetProperty("id").GetString();
        if (id is null || id.Length != 32 || id.Any(c => !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))))
        { throw new InvalidDataException("Invalid project ID for user settings."); }
        return Path.Combine(LegacyDirectory(options), id);
    }

    internal static string PipelineId(uint pipeline) => pipeline switch
    {
        1 => "renderlab.forward",
        2 => "renderlab.deferred",
        3 => "renderlab.raytracing",
        4 => "renderlab.pathtracing",
        _ => throw new InvalidDataException("Unknown pipeline selection.")
    };

    internal static uint PipelineNumber(string pipeline) => pipeline switch
    {
        "renderlab.forward" => 1,
        "renderlab.deferred" => 2,
        "renderlab.raytracing" => 3,
        "renderlab.pathtracing" => 4,
        _ => throw new InvalidDataException("Unknown saved pipeline selection.")
    };

    internal static ViewportConfig Apply(ProjectPreferences preferences, EditorOptions options, ViewportConfig config)
    {
        if (!options.PipelineExplicit) { config.Pipeline = PipelineNumber(preferences.Pipeline); }
        if (!options.TargetFpsExplicit) { config.TargetFps = preferences.TargetFps; }
        if (config.Pipeline == 4)
        {
            config.Submission = 0;
            if (config.View > 1) { config.View = 0; }
            config.Flags &= ~32u;
        }
        return config;
    }

    internal static ProjectPreferences? Accepted(ViewportConfigState state, ulong requestId)
    {
        if (requestId == 0 || state.Status != 2 || state.AppliedId != requestId || state.Applied.Size != 112)
        { return null; }
        return new ProjectPreferences(1, PipelineId(state.Applied.Pipeline), state.Applied.TargetFps);
    }

    internal static ProjectPreferences? Load(string directory)
    {
        var path = Path.Combine(directory, "session.json");
        if (!File.Exists(path)) { return null; }
        if (new FileInfo(path).Length > 16384) { throw new InvalidDataException("User settings exceed budget."); }
        var preferences = JsonSerializer.Deserialize<ProjectPreferences>(File.ReadAllText(path), JsonOptions)
            ?? throw new InvalidDataException("Invalid user settings.");
        if (preferences.Version != 1 || preferences.TargetFps > 240)
        { throw new InvalidDataException("Unsupported user settings version or frame limit."); }
        _ = PipelineNumber(preferences.Pipeline);
        return preferences;
    }

    internal static void Save(string directory, ProjectPreferences preferences)
    {
        if (preferences.Version != 1 || preferences.TargetFps > 240)
        { throw new InvalidDataException("Invalid user settings version or frame limit."); }
        _ = PipelineNumber(preferences.Pipeline);
        Directory.CreateDirectory(directory);
        var path = Path.Combine(directory, "session.json");
        var temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.WriteAllText(temporary, JsonSerializer.Serialize(preferences, JsonOptions) + "\n");
            File.Move(temporary, path, true);
        }
        finally
        {
            if (File.Exists(temporary)) { File.Delete(temporary); }
        }
    }
}
