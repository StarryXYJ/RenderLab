using AutoSettingUI.Avalonia.Attributes;
using AutoSettingUI.Core.Attributes;
using RenderLab.Project;

namespace Experiment;

[SettingUI]
public sealed class Settings
{
    [Title("Blue")]
    [NumericUpDown(Minimum = 0, Maximum = 4, Increment = 0.1)]
    [ProjectSetting("example.blue", Minimum = 0, Maximum = 4)]
    public double Blue { get; set; } = 0.75;
}

public sealed class SettingsDefinition : IProjectSettingsDefinition
{
    public IProjectSettingsSchema CreateSchema() => new SettingsSchema<Settings>(1);
}
