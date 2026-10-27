using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Avalonia.Threading;
using Microsoft.Extensions.DependencyInjection;
using RenderLab.Editor.Host.Services;
using RenderLab.Editor.Host.ViewModels;
using RenderLab.Editor.Host.Views;

namespace RenderLab.Editor.Host;

public partial class App : Application
{
    private ServiceProvider? services;

    public override void Initialize() => AvaloniaXamlLoader.Load(this);

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            services = new ServiceCollection()
                .AddSingleton(Program.Options)
                .AddSingleton<NativeViewportService>()
                .AddSingleton<MainWindowViewModel>()
                .AddSingleton<MainWindow>()
                .BuildServiceProvider(new ServiceProviderOptions
                {
                    ValidateOnBuild = true,
                    ValidateScopes = true
                });
            Dispatcher.UIThread.UnhandledException += (_, args) =>
            {
                HostDiagnostics.WriteError(Program.Options, args.Exception);
                args.Handled = true;
                desktop.Shutdown(1);
            };
            desktop.Exit += (_, _) => DisposeServices();
            desktop.MainWindow = services.GetRequiredService<MainWindow>();
        }
        base.OnFrameworkInitializationCompleted();
    }

    internal void DisposeServices()
    {
        services?.Dispose();
        services = null;
    }
}
