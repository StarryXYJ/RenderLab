namespace RenderLab.Project;

/// <summary>Project assembly entry point providing a versioned settings DTO schema.</summary>
public interface IProjectSettingsDefinition
{
    IProjectSettingsSchema CreateSchema();
}

/// <summary>Non-generic schema boundary used by project hosts.</summary>
public interface IProjectSettingsSchema
{
    Type SettingsType { get; }
    int Version { get; }
    object CreateDefaults();
    object Apply(object baseline, string json);
    string Serialize(object settings);
}
