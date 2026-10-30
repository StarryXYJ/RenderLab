using System.Text.Json.Nodes;
using NUnit.Framework;
using RenderLab.Editor.Diagnostics;

namespace RenderLab.Editor.Tests;

public class GraphSnapshotTests
{
    private const string Fixture = """
        {"schemaVersion":1,"sessionGeneration":42,"frameId":7,"configRevision":3,"topologyRevision":2,"backend":"dx12","status":"submitted",
         "plan":{"schedule":[{"pass":1,"queue":"compute"},{"pass":0,"queue":"graphics"}],
          "passes":[
           {"id":0,"name":"Consumer","kind":"Raster","source":"test:1","live":true,"reason":"export","recordingChunk":1,"accesses":[
            {"resource":0,"inputVersion":1,"outputVersion":1,"use":0,"read":true,"write":false,"bufferOffset":0,"bufferSize":0,"textureRange":{"mipCount":1}}]},
           {"id":1,"name":"Producer","kind":"Compute","source":"test:2","live":true,"reason":"dependency","recordingChunk":0,"accesses":[]}],
          "resources":[{"id":0,"name":"RT","kind":"Texture","width":17,"height":11,"format":"Rgba8","bufferBytes":0,"imported":false,"exported":true,"first":0,"last":2,"aliasGroup":null,"versions":2,"estimatedBytes":748}],
          "edges":[{"before":1,"after":0,"reason":"RAW RT v1"}]}}
        """;

    [Test]
    public void SnapshotPreservesExecutionOrderVersionsAndSourceIdentity()
    {
        var snapshot = GraphSnapshot.Parse(Fixture);
        Assert.That(snapshot.Passes.Select(p => p.Name), Is.EqualTo(new[] { "Producer", "Consumer" }));
        Assert.That(snapshot.Passes[1].Accesses[0].Input, Is.EqualTo(1));
        Assert.That(snapshot.Resources[0].Description, Does.Contain("17 × 11 Rgba8"));
        Assert.That((snapshot.Session, snapshot.Frame, snapshot.Config, snapshot.Topology), Is.EqualTo((42ul, 7ul, 3ul, 2ul)));
        Assert.That(snapshot.Edges.Single().Reason, Is.EqualTo("RAW RT v1"));
    }

    [Test]
    public void UnknownSchemaAndDanglingReferencesDoNotBecomeDisplayedGraphs()
    {
        var fixture = JsonNode.Parse(Fixture)!;
        fixture["schemaVersion"] = 9;
        Assert.Throws<InvalidDataException>(() => GraphSnapshot.Parse(fixture.ToJsonString()));
        fixture["schemaVersion"] = 1;
        fixture["plan"]!["edges"]![0]!["before"] = 99;
        Assert.Throws<InvalidDataException>(() => GraphSnapshot.Parse(fixture.ToJsonString()));
    }
}
