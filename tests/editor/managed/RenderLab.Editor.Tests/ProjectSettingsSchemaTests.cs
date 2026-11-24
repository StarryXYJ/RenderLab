using NUnit.Framework;
using RenderLab.Project;

namespace RenderLab.Editor.Tests;

public sealed class TestProjectSettings : IProjectSettingsValidator
{
    [ProjectSetting("sample.count", Minimum = 1, Maximum = 128)]
    public int Samples { get; set; } = 16;
    [ProjectSetting("effect.enabled")]
    public bool Enabled { get; set; } = true;
    [ProjectSetting("effect.gain", Minimum = 0, Maximum = 2)]
    public double Gain { get; set; } = 1;
    [ProjectSetting("experiment.name")]
    public string Name { get; set; } = "Default";

    public void Validate()
    {
        if (Enabled && Samples < 2) { throw new InvalidDataException("Enabled effect needs two samples."); }
    }
}

public class ProjectSettingsSchemaTests
{
    [Test]
    public void DefaultsAndOverridesAreLayeredWithoutMutatingEarlierValues()
    {
        var schema = new SettingsSchema<TestProjectSettings>(3);
        var defaults = schema.CreateDefaults();
        var project = schema.Apply(defaults, "{\"version\":3,\"values\":{\"sample.count\":32,\"experiment.name\":\"Project\"}}");
        var user = schema.Apply(project, "{\"version\":3,\"values\":{\"effect.gain\":1.5}}");
        var preset = schema.Apply(user, "{\"version\":3,\"values\":{\"sample.count\":64}}");
        var restored = schema.Apply(schema.CreateDefaults(), schema.Serialize(preset));
        Assert.Multiple(() =>
        {
            Assert.That(defaults.Samples, Is.EqualTo(16));
            Assert.That(project.Samples, Is.EqualTo(32));
            Assert.That(project.Gain, Is.EqualTo(1));
            Assert.That(user.Gain, Is.EqualTo(1.5));
            Assert.That(restored.Samples, Is.EqualTo(64));
            Assert.That(restored.Gain, Is.EqualTo(1.5));
            Assert.That(restored.Name, Is.EqualTo("Project"));
        });
    }

    [Test]
    public void InvalidOverridesCannotMutateTheBaseline()
    {
        var schema = new SettingsSchema<TestProjectSettings>(1);
        var baseline = schema.CreateDefaults();
        string[] invalid =
        [
            "{\"version\":2,\"values\":{}}",
            "{\"version\":1,\"values\":{\"unknown\":1}}",
            "{\"version\":1,\"values\":{\"sample.count\":4,\"sample.count\":5}}",
            "{\"version\":1,\"version\":1,\"values\":{}}",
            "{\"version\":1,\"values\":{\"sample.count\":\"bad\"}}",
            "{\"version\":1,\"values\":{\"sample.count\":129}}",
            "{\"version\":1,\"values\":{\"sample.count\":1}}",
            "{\"version\":1,\"values\":{\"experiment.name\":null}}"
        ];
        foreach (var json in invalid) { Assert.Catch(() => schema.Apply(baseline, json), json); }
        Assert.That(baseline.Samples, Is.EqualTo(16));
        Assert.That(baseline.Name, Is.EqualTo("Default"));
    }
}
