using System.Text.Json;
using NUnit.Framework;
using RenderLab.Editor.Configuration;
using RenderLab.Editor.Services;

namespace RenderLab.Editor.Tests;

public class ProjectSettingsSessionTests
{
    [Test]
    public void EachProjectLoadsItsOwnDefaultsAndUserSettings()
    {
        var root = Path.Combine(Path.GetTempPath(), "renderlab-project-settings-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            var first = CreateProject(root, "first", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 4);
            var second = CreateProject(root, "second", "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", 6);
            var firstOptions = Options(first);
            var userDirectory = ProjectUserSettings.DirectoryFor(firstOptions);
            Directory.CreateDirectory(userDirectory);
            File.WriteAllText(Path.Combine(userDirectory, "project-settings.json"),
                "{\"version\":1,\"values\":{\"fixture.count\":9}}");
            using var firstSession = new ProjectSettingsSession(firstOptions);
            using var secondSession = new ProjectSettingsSession(Options(second));
            Assert.That(firstSession.Schema!.Serialize(firstSession.Values!), Does.Contain("\"fixture.count\": 9"));
            Assert.That(secondSession.Schema!.Serialize(secondSession.Values!), Does.Contain("\"fixture.count\": 6"));
            Assert.That(firstSession.Schema.SettingsType, Is.Not.EqualTo(secondSession.Schema.SettingsType));
        }
        finally { Directory.Delete(root, true); }
    }

    [Test]
    public void InvalidUserValuesFallBackAndLeaveTheFileUntouched()
    {
        var root = Path.Combine(Path.GetTempPath(), "renderlab-project-settings-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            var project = CreateProject(root, "first", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 4);
            var options = Options(project);
            var userDirectory = ProjectUserSettings.DirectoryFor(options);
            Directory.CreateDirectory(userDirectory);
            var userFile = Path.Combine(userDirectory, "project-settings.json");
            const string invalid = "{\"version\":99,\"values\":{\"fixture.count\":7}}";
            File.WriteAllText(userFile, invalid);
            using var session = new ProjectSettingsSession(options);
            Assert.That(session.UserSettingsError, Is.Not.Null.And.Not.Empty);
            Assert.That(session.Schema!.Serialize(session.Values!), Does.Contain("\"fixture.count\": 4"));
            Assert.That(File.ReadAllText(userFile), Is.EqualTo(invalid));
        }
        finally { Directory.Delete(root, true); }
    }

    [Test]
    public void ProjectPathsCannotEscapeAndInvalidDefaultsFailTheOpen()
    {
        var root = Path.Combine(Path.GetTempPath(), "renderlab-project-settings-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            var project = CreateProject(root, "first", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 4);
            var manifest = JsonSerializer.Deserialize<Dictionary<string, object>>(File.ReadAllText(project))!;
            var options = Options(project);
            File.WriteAllText(Path.Combine(Path.GetDirectoryName(project)!, "defaults.json"),
                "{\"version\":1,\"values\":{\"fixture.count\":99}}");
            Assert.Throws<InvalidDataException>(() => new ProjectSettingsSession(options));
            manifest["settings"] = new { version = 1, assembly = "../outside.dll", entry = "Fixture" };
            File.WriteAllText(project, JsonSerializer.Serialize(manifest));
            Assert.Throws<InvalidDataException>(() => new ProjectSettingsSession(options));
        }
        finally { Directory.Delete(root, true); }
    }

    private static EditorOptions Options(string project) => new("dx12", project, "shaders", "output",
        false, false, true, 1, 0, 0);

    private static string CreateProject(string root, string name, string id, int count)
    {
        var directory = Path.Combine(root, name);
        Directory.CreateDirectory(directory);
        File.Copy(Path.Combine(AppContext.BaseDirectory, "RenderLab.Project.Fixture.dll"), Path.Combine(directory, "fixture.dll"));
        File.WriteAllText(Path.Combine(directory, "defaults.json"),
            $"{{\"version\":1,\"values\":{{\"fixture.count\":{count}}}}}");
        var project = Path.Combine(directory, "project.rlproject");
        File.WriteAllText(project, JsonSerializer.Serialize(new
        {
            id,
            settings = new { version = 1, assembly = "fixture.dll", entry = "RenderLab.Project.Fixture.FixtureDefinition", defaults = "defaults.json" }
        }));
        return project;
    }
}
