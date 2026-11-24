using RenderLab.Project;

namespace RenderLab.Project.Fixture;

public sealed class FixtureSettings
{
    [ProjectSetting("fixture.count", Minimum = 1, Maximum = 10)]
    public int Count { get; set; } = 3;
}

public sealed class FixtureDefinition : IProjectSettingsDefinition
{
    public IProjectSettingsSchema CreateSchema() => new SettingsSchema<FixtureSettings>(1);
}
