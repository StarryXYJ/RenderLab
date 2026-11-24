using System.Text.Json;
using NUnit.Framework;
using RenderLab.Project;

namespace RenderLab.Editor.Tests;

public class ProjectPipelineCatalogTests
{
    [Test]
    public void ExportUsesStableIdsAndCopiesNodeList()
    {
        var root = Path.Combine(Path.GetTempPath(), "renderlab-pipelines-" + Guid.NewGuid().ToString("N"));
        var path = Path.Combine(root, "pipelines.json");
        try
        {
            string[] nodes = ["project.first", "project.second"];
            var catalog = new PipelineCatalog();
            catalog.AddForward("project.forward", nodes);
            nodes[0] = "changed";
            catalog.Save(path);
            using var json = JsonDocument.Parse(File.ReadAllText(path));
            var pipeline = json.RootElement.GetProperty("pipelines")[0];
            Assert.Multiple(() =>
            {
                Assert.That(json.RootElement.GetProperty("version").GetInt32(), Is.EqualTo(1));
                Assert.That(pipeline.GetProperty("id").GetString(), Is.EqualTo("project.forward"));
                Assert.That(pipeline.GetProperty("base").GetString(), Is.EqualTo("forward"));
                Assert.That(pipeline.GetProperty("nodes")[0].GetString(), Is.EqualTo("project.first"));
                Assert.That(pipeline.GetProperty("nodes")[1].GetString(), Is.EqualTo("project.second"));
            });
        }
        finally { if (Directory.Exists(root)) { Directory.Delete(root, true); } }
    }

    [Test]
    public void InvalidRegistrationDoesNotReserveThePipelineId()
    {
        var catalog = new PipelineCatalog();
        Assert.Throws<ArgumentException>(() => catalog.AddForward("renderlab.reserved"));
        Assert.Throws<ArgumentException>(() => catalog.AddForward("Bad ID"));
        Assert.Throws<ArgumentException>(() => catalog.AddForward("project.good", "node.a", "node.a"));
        Assert.Throws<ArgumentException>(() => catalog.AddForward("project.good", "node\0bad"));
        Assert.Throws<ArgumentException>(() => catalog.AddForward("project.good", new string('a', 129)));
        Assert.Throws<ArgumentException>(() => catalog.AddForward("project.good",
            Enumerable.Range(0, 257).Select(i => $"node.{i}").ToArray()));
        catalog.AddForward("project.good", "node.a");
        Assert.Throws<ArgumentException>(() => catalog.AddForward("project.good", "node.b"));
    }
}
