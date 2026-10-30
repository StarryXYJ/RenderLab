using System.Text.Json;

namespace RenderLab.Editor.Diagnostics;

internal sealed record GraphAccess(int Resource, uint Input, uint Output, int Use, bool Read, bool Write, string Range)
{
    internal string UseName => Use is >= 0 and < 16 ? new[] { "Sampled", "Uniform", "ShaderRead", "Storage", "Color", "Depth", "DepthRead", "CopySource", "CopyDestination", "Vertex", "Index", "Indirect", "ASBuildInput", "ASBuildWrite", "ASRead", "ShaderTableRead" }[Use] : "Unknown";
}
internal sealed record GraphPass(int Id, string Name, string Kind, string Source, bool Live, string Reason,
    string Queue, int? Chunk, GraphAccess[] Accesses);
internal sealed record GraphResource(int Id, string Name, string Description, bool Imported, bool Exported,
    int? First, int Last, int? Alias, uint Versions, ulong EstimatedBytes);
internal sealed record GraphEdge(int Before, int After, string Reason);

/// <summary>Owned CPU values. Neither frozen snapshots nor exports retain GPU resources.</summary>
internal sealed record GraphSnapshot(ulong Session, ulong Frame, ulong Config, ulong Topology, string Backend,
    GraphPass[] Passes, GraphResource[] Resources, GraphEdge[] Edges, string Json)
{
    internal static GraphSnapshot Parse(string json)
    {
        if (json.Length > 4 * 1024 * 1024) { throw new InvalidDataException("Snapshot exceeds budget."); }
        using var document = JsonDocument.Parse(json, new JsonDocumentOptions { MaxDepth = 32 });
        var root = document.RootElement;
        if (root.GetProperty("schemaVersion").GetInt32() != 1) { throw new InvalidDataException("Unsupported graph schema."); }
        if (root.GetProperty("status").GetString() != "submitted") { throw new InvalidDataException(root.GetProperty("error").GetString()); }
        var plan = root.GetProperty("plan");
        var schedule = plan.GetProperty("schedule").EnumerateArray().ToDictionary(p => p.GetProperty("pass").GetInt32(), p => p.GetProperty("queue").GetString()!);
        static int? Nullable(JsonElement parent, string key) => parent.GetProperty(key).ValueKind == JsonValueKind.Null ? null : parent.GetProperty(key).GetInt32();
        var passes = plan.GetProperty("passes").EnumerateArray().Select(p =>
        {
            var id = p.GetProperty("id").GetInt32();
            var accesses = p.GetProperty("accesses").EnumerateArray().Select(a => new GraphAccess(a.GetProperty("resource").GetInt32(),
                a.GetProperty("inputVersion").GetUInt32(), a.GetProperty("outputVersion").GetUInt32(), a.GetProperty("use").GetInt32(),
                a.GetProperty("read").GetBoolean(), a.GetProperty("write").GetBoolean(),
                $"buffer [{a.GetProperty("bufferOffset")}, +{a.GetProperty("bufferSize")}] · texture {a.GetProperty("textureRange")}")).ToArray();
            return new GraphPass(id, p.GetProperty("name").GetString()!, p.GetProperty("kind").GetString()!, p.GetProperty("source").GetString()!,
                p.GetProperty("live").GetBoolean(), p.GetProperty("reason").GetString()!, schedule.GetValueOrDefault(id, "culled"), Nullable(p, "recordingChunk"), accesses);
        }).ToArray();
        var resources = plan.GetProperty("resources").EnumerateArray().Select(r => new GraphResource(r.GetProperty("id").GetInt32(), r.GetProperty("name").GetString()!,
            r.GetProperty("kind").GetString() == "Texture" ? $"{r.GetProperty("width")} × {r.GetProperty("height")} {r.GetProperty("format")}" : $"Buffer {r.GetProperty("bufferBytes")} B",
            r.GetProperty("imported").GetBoolean(), r.GetProperty("exported").GetBoolean(), Nullable(r, "first"), r.GetProperty("last").GetInt32(),
            Nullable(r, "aliasGroup"), r.GetProperty("versions").GetUInt32(), r.GetProperty("estimatedBytes").GetUInt64())).ToArray();
        var edges = plan.GetProperty("edges").EnumerateArray().Select(e => new GraphEdge(e.GetProperty("before").GetInt32(), e.GetProperty("after").GetInt32(), e.GetProperty("reason").GetString()!)).ToArray();
        if (passes.Length > 4096 || resources.Length > 16384 || passes.Select(p => p.Id).Distinct().Count() != passes.Length || resources.Select(r => r.Id).Distinct().Count() != resources.Length)
        { throw new InvalidDataException("Invalid graph identities or node budget."); }
        var passIds = passes.Select(p => p.Id).ToHashSet(); var resourceIds = resources.Select(r => r.Id).ToHashSet();
        if (passes.Any(p => p.Accesses.Any(a => !resourceIds.Contains(a.Resource))) || edges.Any(e => !passIds.Contains(e.Before) || !passIds.Contains(e.After)))
        { throw new InvalidDataException("Dangling graph reference."); }
        // Execution order first; keep culled nodes for the explicit diagnostic toggle.
        var order = plan.GetProperty("schedule").EnumerateArray().Select(p => p.GetProperty("pass").GetInt32()).ToList();
        passes = passes.OrderBy(p => p.Live ? order.IndexOf(p.Id) : int.MaxValue).ToArray();
        return new(root.GetProperty("sessionGeneration").GetUInt64(), root.GetProperty("frameId").GetUInt64(), root.GetProperty("configRevision").GetUInt64(),
            root.GetProperty("topologyRevision").GetUInt64(), root.GetProperty("backend").GetString()!, passes, resources, edges, json);
    }
}
