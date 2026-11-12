using Microsoft.Extensions.DependencyInjection;
using RenderLab.Editor.Configuration;
using RenderLab.Editor.ViewModels;

namespace RenderLab.Editor.Services;

public sealed record StartupOptions(EditorOptions Value);

public sealed class ProjectContext
{
    public EditorOptions Options { get; internal set; } = null!;
}

public sealed class ProjectSessionFactory(IServiceScopeFactory scopes, StartupOptions startup)
{
    internal ProjectSession Create(string project, DeviceSelection? device = null)
    {
        var scope = scopes.CreateScope();
        try
        {
            scope.ServiceProvider.GetRequiredService<ProjectContext>().Options = startup.Value with
            {
                Project = project,
                Backend = device?.Backend ?? startup.Value.Backend,
                Validation = device?.Validation ?? startup.Value.Validation
            };
            return new ProjectSession(scope);
        }
        catch { scope.Dispose(); throw; }
    }
}

internal sealed class ProjectSession(IServiceScope scope) : IDisposable
{
    internal EditorOptions Options => scope.ServiceProvider.GetRequiredService<ProjectContext>().Options;
    internal NativeViewportService Viewport => scope.ServiceProvider.GetRequiredService<NativeViewportService>();
    internal MainWindowViewModel ViewModel => scope.ServiceProvider.GetRequiredService<MainWindowViewModel>();
    public void Dispose() => scope.Dispose();
}
