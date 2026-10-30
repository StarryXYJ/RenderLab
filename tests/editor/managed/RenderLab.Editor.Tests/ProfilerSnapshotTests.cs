using System.Text.Json.Nodes;
using NUnit.Framework;
using Avalonia.Headless.NUnit;
using RenderLab.Editor.Diagnostics;
using RenderLab.Editor.Configuration;
using RenderLab.Editor.Services;
using RenderLab.Editor.ViewModels;

namespace RenderLab.Editor.Tests;

public class ProfilerSnapshotTests
{
    private static JsonNode Fixture()
    {
        var root = JsonNode.Parse(GraphSnapshotTests.Fixture)!;
        root["plan"]!["aliasBackingBytes"] = 0;
        root["plan"]!["gpuTimingReason"] = "GPU timing disabled";
        foreach (var pass in root["plan"]!["passes"]!.AsArray())
        {
            pass!["category"] = "Opaque";
            pass["commands"] = JsonNode.Parse("""
                {"direct":2,"indexed":1,"indirect":1,"indirectCapacity":128,"batches":2,"directInstances":12,
                 "indirectInstancesUnknown":true,"dispatch":1,"threadGroups":24,"traceRays":1,"rays":32,"asBuilds":1,"asUpdates":0,
                 "copyBytes":0,"lastDispatch":[2,3,4],"lastTrace":[8,4,1]}
                """);
        }
        root["profile"] = JsonNode.Parse("""
            {"cpuNs":{"frame":2000000,"fence":0,"acquire":0,"render":1000000,"scene":1,"compile":2,"prepare":3,"record":4,"present":0,"resize":0},
             "nativeRecordNs":null,"nativeSubmitNs":null,"candidates":128,"cpuVisible":null,"gpuVisible":null,"gpuPasses":[],
             "gpuSumNs":null,"gpuSpanNs":null,"gpuGraphicsNs":null,"gpuComputeNs":null,"completionObservationNs":5000000,
             "nativeBytes":1000,"peakBytes":2000,"poolEstimatedBytes":500,"poolCached":2,"sceneUploadBytes":40,"uploadPages":1,"uploadReservedBytes":1024,
             "descriptorSlots":4,"descriptorCapacity":64,"timingEnabled":false,"readbackEnabled":false}
            """);
        return root;
    }

    [AvaloniaTest]
    public void LiveFrameChoiceUpdatesDoNotAccidentallyFreezeCollection()
    {
        using var service = new NativeViewportService(new EditorOptions("dx12", "fixture", "fixture", "fixture", false, true, true, 2, 0, 0));
        var model = new ProfilerViewModel(service, new RenderGraphViewModel(service));
        var first = ProfilerSnapshot.Parse(Fixture().ToJsonString());
        var second = first with { Graph = first.Graph with { Frame = 8 } };
        model.Observe(first); model.Observe(second);
        model.SelectedFrame = 0;
        Assert.That(model.Frozen, Is.False);
        model.Frozen = true;
        model.SelectedFrame = 1;
        Assert.That(model.Current!.Graph.Frame, Is.EqualTo(8));
        for (var index = 0; index < 100; ++index) { model.Observe(second); }
        Assert.That(model.HistoryCount, Is.LessThanOrEqualTo(64));
        Assert.That(model.HistoryBytes, Is.LessThanOrEqualTo(8 * 1024 * 1024));
    }

    [Test]
    public void DisabledGpuDiagnosticsRemainUnknownAndCapacityRemainsSeparate()
    {
        var snapshot = ProfilerSnapshot.Parse(Fixture().ToJsonString());
        Assert.That(snapshot.GpuVisible, Is.Null);
        Assert.That(snapshot.Passes.All(pass => pass.Gpu == "N/A"), Is.True);
        Assert.That(snapshot.Passes[0].Capacity, Is.EqualTo(128));
        Assert.That(snapshot.Passes[0].Instances, Does.Contain("unknown indirect"));
        Assert.That(snapshot.Summary, Does.Contain("span N/A").And.Contain("not display latency"));
    }

    [Test]
    public void ExplicitVisibilityKeepsThePairedGraphIdentity()
    {
        var root = Fixture();
        root["profile"]!["readbackEnabled"] = true;
        root["profile"]!["gpuVisible"] = 3;
        var snapshot = ProfilerSnapshot.Parse(root.ToJsonString());
        Assert.That(snapshot.GpuVisible, Is.EqualTo(3));
        Assert.That(snapshot.Graph.Frame, Is.EqualTo(7));
        Assert.That(snapshot.Graph.Config, Is.EqualTo(3));
    }
}
