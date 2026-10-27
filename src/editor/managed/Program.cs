using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Themes.Fluent;
using Avalonia.Threading;

namespace RenderLab.Editor.Host;

internal sealed record HostOptions(string Backend, string Project, string Shaders, string Output, bool Smoke)
{
    internal static HostOptions Parse(string[] args)
    {
        var root = new DirectoryInfo(AppContext.BaseDirectory);
        while (root != null && !File.Exists(Path.Combine(root.FullName, "sample/m4/RayTracing.rlproject")))
        {
            root = root.Parent;
        }
        var backend = "dx12";
        var project = root == null ? "" : Path.Combine(root.FullName, "sample/m4/RayTracing.rlproject");
        var shaders = Path.Combine(AppContext.BaseDirectory, "shaders");
        string? output = null;
        var smoke = false;
        for (var i = 0; i < args.Length; ++i)
        {
            var key = args[i];
            if (key == "--smoke")
            {
                smoke = true;
                continue;
            }
            if (++i >= args.Length)
            {
                throw new ArgumentException($"Missing value for {key}");
            }
            switch (key)
            {
                case "--backend": backend = args[i]; break;
                case "--project": project = args[i]; break;
                case "--shaders": shaders = args[i]; break;
                case "--output": output = args[i]; break;
                default: throw new ArgumentException($"Unknown argument: {key}");
            }
        }
        if (backend is not ("dx12" or "vulkan") || !File.Exists(project) || !Directory.Exists(shaders))
        {
            throw new ArgumentException("Select dx12/vulkan and existing project/shader paths.");
        }
        output ??= Path.Combine(root?.FullName ?? Environment.CurrentDirectory, "out/m7", backend);
        return new HostOptions(backend, Path.GetFullPath(project), Path.GetFullPath(shaders), Path.GetFullPath(output), smoke);
    }
}

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
            return AppBuilder.Configure<HostApplication>().UsePlatformDetect()
                .LogToTrace().StartWithClassicDesktopLifetime(args);
        }
        catch (Exception error)
        {
            if (Options != null)
            {
                File.WriteAllText(Path.Combine(Options.Output, "managed-error.txt"), error.ToString());
            }
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}

public sealed class HostApplication : Application
{
    public override void Initialize()
    {
        RequestedThemeVariant = Avalonia.Styling.ThemeVariant.Dark;
        Styles.Add(new FluentTheme());
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            Dispatcher.UIThread.UnhandledException += (_, args) =>
            {
                File.WriteAllText(Path.Combine(Program.Options.Output, "managed-error.txt"), args.Exception.ToString());
                args.Handled = true;
                desktop.Shutdown(1);
            };
            desktop.MainWindow = new MainWindow(Program.Options);
        }
        base.OnFrameworkInitializationCompleted();
    }
}
