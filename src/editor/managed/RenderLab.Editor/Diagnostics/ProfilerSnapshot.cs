using System.Text.Json;

namespace RenderLab.Editor.Diagnostics;

public sealed record ProfilerPassRow(string Pass, string Category, string Queue, ulong Direct, ulong Indirect,
    ulong Capacity, ulong Batches, string Instances, ulong Dispatch, ulong TraceRays, string Gpu);

internal sealed record ProfilerSnapshot(GraphSnapshot Graph, ProfilerPassRow[] Passes, string Summary, string Details,
    ulong? GpuVisible, bool TimingEnabled, bool ReadbackEnabled)
{
    internal static ProfilerSnapshot Parse(string json)
    {
        var graph = GraphSnapshot.Parse(json);
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement; var profile = root.GetProperty("profile"); var plan = root.GetProperty("plan");
        static string Value(JsonElement parent, string key) => parent.GetProperty(key).ValueKind == JsonValueKind.Null ? "N/A" : parent.GetProperty(key).ToString();
        static string Ms(JsonElement parent, string key) => parent.GetProperty(key).ValueKind == JsonValueKind.Null ? "N/A" : $"{parent.GetProperty(key).GetDouble() / 1e6:F3} ms";
        var gpu = profile.GetProperty("gpuPasses").EnumerateArray().ToArray();
        var passes = plan.GetProperty("passes").EnumerateArray().Where(p => p.GetProperty("live").GetBoolean()).Select(p =>
        {
            var name = p.GetProperty("name").GetString()!;
            var timing = gpu.FirstOrDefault(t => t.GetProperty("name").GetString() == name);
            var commands = p.GetProperty("commands"); var id = p.GetProperty("id").GetInt32();
            var native = graph.Passes.First(item => item.Id == id);
            return new ProfilerPassRow(name, p.GetProperty("category").GetString()!, native.Queue,
                commands.GetProperty("direct").GetUInt64(), commands.GetProperty("indirect").GetUInt64(),
                commands.GetProperty("indirectCapacity").GetUInt64(), commands.GetProperty("batches").GetUInt64(),
                commands.GetProperty("indirectInstancesUnknown").GetBoolean() ? $"{commands.GetProperty("directInstances")} direct + unknown indirect" : commands.GetProperty("directInstances").ToString(),
                commands.GetProperty("dispatch").GetUInt64(), commands.GetProperty("traceRays").GetUInt64(),
                timing.ValueKind == JsonValueKind.Undefined ? "N/A" : Ms(timing, "ns"));
        }).ToArray();
        var cpu = profile.GetProperty("cpuNs");
        var commandRows = plan.GetProperty("passes").EnumerateArray().Where(p => p.GetProperty("live").GetBoolean()).ToArray();
        ulong Sum(string key) => commandRows.Aggregate(0ul, (value, pass) => value + pass.GetProperty("commands").GetProperty(key).GetUInt64());
        var summary = $"{graph.Backend} · frame {graph.Frame} · config {graph.Config} · session {graph.Session} · topology {graph.Topology}\n" +
            $"Draw API {passes.Sum(p => (long)p.Direct)} · indirect API {passes.Sum(p => (long)p.Indirect)} · capacity {passes.Sum(p => (long)p.Capacity)} · raster batches {passes.Sum(p => (long)p.Batches)}\n" +
            $"Scene candidates {Value(profile, "candidates")} · CPU visible {Value(profile, "cpuVisible")} · GPU opaque visible {Value(profile, "gpuVisible")}\n" +
            $"GPU Pass sum {Ms(profile, "gpuSumNs")} · span {Ms(profile, "gpuSpanNs")} · graphics {Ms(profile, "gpuGraphicsNs")} · compute {Ms(profile, "gpuComputeNs")}\n" +
            $"Submit return → CPU observed GPU completion: {Ms(profile, "completionObservationNs")} (includes polling; not display latency)";
        var details = $"CPU viewport Tick wall: {Ms(cpu, "frame")} · fence {Ms(cpu, "fence")} · acquire {Ms(cpu, "acquire")} · present {Ms(cpu, "present")}\n" +
            $"Render (inclusive) {Ms(cpu, "render")}: scene {Ms(cpu, "scene")}, compile {Ms(cpu, "compile")}, prepare {Ms(cpu, "prepare")}, graph packet record {Ms(cpu, "record")}\n" +
            $"RHI native record {Ms(profile, "nativeRecordNs")} · native submit {Ms(profile, "nativeSubmitNs")} · resize {Ms(cpu, "resize")}\n" +
            $"Tracked native {profile.GetProperty("nativeBytes").GetDouble() / 1048576:F1} MiB / peak {profile.GetProperty("peakBytes").GetDouble() / 1048576:F1} MiB; driver residency N/A\n" +
            $"Graph pool logical {Value(profile, "poolEstimatedBytes")} B / cached {Value(profile, "poolCached")}; alias backing {Value(plan, "aliasBackingBytes")} B\n" +
            $"Scene upload {Value(profile, "sceneUploadBytes")} B this frame / {Value(profile, "uploadPages")} pages / reserved {Value(profile, "uploadReservedBytes")} B (scene upload pool scope)\n" +
            $"Descriptor slots {Value(profile, "descriptorSlots")} / {Value(profile, "descriptorCapacity")}\n{Value(plan, "gpuTimingReason")}\n" +
            $"Indexed Draw subset {Sum("indexed")} · AS builds/updates {Sum("asBuilds")}/{Sum("asUpdates")} · rays {Sum("rays")} · thread groups {Sum("threadGroups")} · copied bytes {Sum("copyBytes")}\n" +
            string.Join("\n", commandRows.Where(p => p.GetProperty("commands").GetProperty("dispatch").GetUInt64() > 0 || p.GetProperty("commands").GetProperty("traceRays").GetUInt64() > 0)
                .Select(p => $"{p.GetProperty("name")}: last dispatch {p.GetProperty("commands").GetProperty("lastDispatch")}, last TraceRays {p.GetProperty("commands").GetProperty("lastTrace")}")) + "\n" +
            "Nested CPU intervals overlap. GPU Pass sum is not GPU span. A batch is a draw run sharing pipeline/bindings/geometry; indirect capacity is not a visible count.\n" +
            string.Join("\n", gpu.Select(t => $"{Value(t, "name")} [{Value(t, "startNs")}, {Value(t, "endNs")}] ns ({Value(t, "queue")})"));
        return new(graph, passes, summary, details, profile.GetProperty("gpuVisible").ValueKind == JsonValueKind.Null ? null : profile.GetProperty("gpuVisible").GetUInt64(),
            profile.GetProperty("timingEnabled").GetBoolean(), profile.GetProperty("readbackEnabled").GetBoolean());
    }
}
