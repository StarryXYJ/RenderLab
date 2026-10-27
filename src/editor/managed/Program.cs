using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;

namespace RenderLab.Editor.Host;

internal static class Program
{
    internal static HostOptions Options { get; private set; } = null!;

    [STAThread]
    internal static int Main(string[] args)
    {
        try
        {
            Options = HostOptions.Parse(args);
            Directory.CreateDirectory(Options.Output);
            return BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
        }
        catch (Exception error)
        {
            if (Options != null)
            {
                HostDiagnostics.WriteError(Options, error);
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
