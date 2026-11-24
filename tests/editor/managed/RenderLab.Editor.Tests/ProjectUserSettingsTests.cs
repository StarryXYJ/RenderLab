using NUnit.Framework;
using RenderLab.Editor.Configuration;
using RenderLab.Editor.Interop;
using RenderLab.Editor.Services;

namespace RenderLab.Editor.Tests;

public class ProjectUserSettingsTests
{
    [Test]
    public void ProjectsSharingFolderKeepSeparateSettingsAndIsolatedRunsUseOutput()
    {
        var root = Path.Combine(Path.GetTempPath(), "renderlab-user-settings-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            var first = Path.Combine(root, "first.rlproject");
            var second = Path.Combine(root, "second.rlproject");
            File.WriteAllText(first, "{\"id\":\"11111111111111111111111111111111\"}");
            File.WriteAllText(second, "{\"id\":\"22222222222222222222222222222222\"}");
            var options = new EditorOptions("dx12", first, "shaders", Path.Combine(root, "output"), false, false, true, 2, 0, 0);
            var firstPath = ProjectUserSettings.DirectoryFor(options);
            var secondPath = ProjectUserSettings.DirectoryFor(options with { Project = second });
            var firstPreferences = new ProjectPreferences(1, "renderlab.forward", 30);
            var secondPreferences = new ProjectPreferences(1, "renderlab.pathtracing", 0);
            ProjectUserSettings.Save(firstPath, firstPreferences);
            ProjectUserSettings.Save(secondPath, secondPreferences);
            Assert.Multiple(() =>
            {
                Assert.That(firstPath, Is.Not.EqualTo(secondPath));
                Assert.That(ProjectUserSettings.Load(firstPath), Is.EqualTo(firstPreferences));
                Assert.That(ProjectUserSettings.Load(secondPath), Is.EqualTo(secondPreferences));
                Assert.That(ProjectUserSettings.DirectoryFor(options with { Isolated = true }), Is.EqualTo(options.Output));
                Assert.That(ProjectUserSettings.Persistent(options with { Smoke = true }), Is.False);
            });
        }
        finally { Directory.Delete(root, true); }
    }

    [Test]
    public void OnlyTheAppliedUserRequestCanBeSaved()
    {
        var applied = new ViewportConfig { Size = 112, Pipeline = 1, TargetFps = 60 };
        var state = new ViewportConfigState { Ready = 1, Status = 1, AppliedId = 3, Applied = applied };
        Assert.That(ProjectUserSettings.Accepted(state, 4), Is.Null);
        state.Status = 3;
        state.RejectedId = 4;
        Assert.That(ProjectUserSettings.Accepted(state, 4), Is.Null);
        state.Status = 2;
        state.AppliedId = 4;
        state.Applied = new ViewportConfig { Size = 112, Pipeline = 4, TargetFps = 0 };
        Assert.That(ProjectUserSettings.Accepted(state, 4), Is.EqualTo(new ProjectPreferences(1, "renderlab.pathtracing", 0)));
        Assert.That(ProjectUserSettings.Accepted(state, 0), Is.Null);
        Assert.That(ProjectUserSettings.Accepted(state, 3), Is.Null);
    }

    [Test]
    public void ExplicitArgumentsOverrideSavedValuesAndPathTracingUsesCompatibleOptions()
    {
        var options = new EditorOptions("dx12", "project", "shaders", "output", false, false, true, 2, 0, 0);
        var preferences = new ProjectPreferences(1, "renderlab.pathtracing", 24);
        var config = new ViewportConfig { Pipeline = 2, TargetFps = 120, Submission = 3, View = 7, Flags = 32 };
        var restored = ProjectUserSettings.Apply(preferences, options, config);
        Assert.Multiple(() =>
        {
            Assert.That(restored.Pipeline, Is.EqualTo(4));
            Assert.That(restored.TargetFps, Is.EqualTo(24));
            Assert.That(restored.Submission, Is.Zero);
            Assert.That(restored.View, Is.Zero);
            Assert.That(restored.Flags & 32, Is.Zero);
        });
        var overridden = ProjectUserSettings.Apply(preferences, options with { PipelineExplicit = true, TargetFpsExplicit = true }, config);
        Assert.That(overridden.Pipeline, Is.EqualTo(2));
        Assert.That(overridden.TargetFps, Is.EqualTo(120));
    }

    [Test]
    public void InvalidSettingsAreRejectedWithoutModifyingTheFile()
    {
        var root = Path.Combine(Path.GetTempPath(), "renderlab-invalid-settings-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            var file = Path.Combine(root, "session.json");
            const string invalid = "{\"version\":1,\"pipeline\":\"renderlab.forward\"}";
            File.WriteAllText(file, invalid);
            Assert.Throws<System.Text.Json.JsonException>(() => ProjectUserSettings.Load(root));
            Assert.That(File.ReadAllText(file), Is.EqualTo(invalid));
            Assert.Throws<InvalidDataException>(() => ProjectUserSettings.Save(root, new ProjectPreferences(1, "unknown", 60)));
            Assert.That(File.ReadAllText(file), Is.EqualTo(invalid));
        }
        finally { Directory.Delete(root, true); }
    }
}
