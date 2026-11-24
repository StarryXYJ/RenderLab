namespace RenderLab.Project;

/// <summary>Optional cross-field validation; throw when the candidate is invalid.</summary>
public interface IProjectSettingsValidator
{
    void Validate();
}
