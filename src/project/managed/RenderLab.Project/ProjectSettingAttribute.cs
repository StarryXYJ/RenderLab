namespace RenderLab.Project;

/// <summary>Assigns a stable serialized ID and optional numeric limits to a DTO property.</summary>
[AttributeUsage(AttributeTargets.Property)]
public sealed class ProjectSettingAttribute(string id) : Attribute
{
    public string Id { get; } = id;
    public double Minimum { get; set; } = double.NegativeInfinity;
    public double Maximum { get; set; } = double.PositiveInfinity;
}
