using System.Text.Json;
using System.Text.Json.Serialization;

namespace RenderLab.Project;

/// <summary>Builds a portable project pipeline catalog referencing registered native node IDs.</summary>
public sealed class PipelineCatalog
{
    private const int Limit = 256;
    private readonly List<Pipeline> pipelines = [];
    private readonly HashSet<string> pipelineIds = new(StringComparer.Ordinal);

    /// <summary>Adds a pipeline extending the native Forward renderer.</summary>
    public void AddForward(string id, params string[] nodeIds)
    {
        ArgumentNullException.ThrowIfNull(nodeIds);
        ValidateId(id);
        if (id.StartsWith("renderlab.", StringComparison.Ordinal))
        { throw new ArgumentException("The renderlab namespace is reserved.", nameof(id)); }
        if (pipelines.Count == Limit) { throw new InvalidOperationException("Too many pipelines."); }
        if (pipelineIds.Contains(id)) { throw new ArgumentException("Duplicate pipeline ID.", nameof(id)); }
        if (nodeIds.Length > Limit) { throw new ArgumentException("Too many pipeline nodes.", nameof(nodeIds)); }
        var nodes = new HashSet<string>(StringComparer.Ordinal);
        foreach (var node in nodeIds)
        {
            ValidateId(node);
            if (!nodes.Add(node)) { throw new ArgumentException("Duplicate node ID in pipeline.", nameof(nodeIds)); }
        }
        pipelineIds.Add(id);
        pipelines.Add(new Pipeline(id, "forward", [.. nodeIds]));
    }

    /// <summary>Atomically writes the catalog, leaving an existing file intact on failure.</summary>
    public void Save(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        var fullPath = Path.GetFullPath(path);
        var directory = Path.GetDirectoryName(fullPath)!;
        Directory.CreateDirectory(directory);
        var json = JsonSerializer.Serialize(new Catalog(1, pipelines), new JsonSerializerOptions { WriteIndented = true });
        if (System.Text.Encoding.UTF8.GetByteCount(json) + 1 > 1024 * 1024)
        { throw new InvalidOperationException("Pipeline catalog exceeds budget."); }
        var temporary = fullPath + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.WriteAllText(temporary, json + "\n");
            File.Move(temporary, fullPath, true);
        }
        finally
        {
            if (File.Exists(temporary)) { File.Delete(temporary); }
        }
    }

    private static void ValidateId(string? id)
    {
        if (id is not { Length: > 0 and <= 128 } || id.Any(c =>
            !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c is '.' or '-' or '_')))
        { throw new ArgumentException("ID must use lowercase ASCII letters, digits, '.', '-' or '_'."); }
    }

    private sealed record Catalog([property: JsonPropertyName("version")] int Version,
        [property: JsonPropertyName("pipelines")] IReadOnlyList<Pipeline> Pipelines);
    private sealed record Pipeline([property: JsonPropertyName("id")] string Id,
        [property: JsonPropertyName("base")] string Base,
        [property: JsonPropertyName("nodes")] string[] Nodes);
}
