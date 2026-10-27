using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using RenderLab.Editor.Configuration;
using RenderLab.Editor.Diagnostics;

namespace RenderLab.Editor;

internal static class Program
{
    internal static EditorOptions Options { get; private set; } = null!;

    [STAThread]
    internal static int Main(string[] args)
    {
        try
        {
            Options = EditorOptions.Parse(args);
            Directory.CreateDirectory(Options.Output);
            return BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
        }
        catch (Exception error)
        {
            if (Options != null)
            {
                EditorDiagnostics.WriteError(Options, error);
            }
            Console.Error.WriteLine(error);
            return 1;
        }
        finally
        {
            (Application.Current as App)?.DisposeServices();
        }
    }

    // This entry point is also used by the Avalonia visual designer.
    public static AppBuilder BuildAvaloniaApp() => AppBuilder.Configure<App>()
        .UsePlatformDetect()
#if DEBUG
        .WithDeveloperTools()
#endif
        .WithInterFont()
        .LogToTrace();
}
