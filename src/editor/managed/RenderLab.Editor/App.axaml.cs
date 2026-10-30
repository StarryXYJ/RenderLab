using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Avalonia.Threading;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Serilog;
using RenderLab.Editor.Services;
using RenderLab.Editor.ViewModels;
using RenderLab.Editor.Views;
using RenderLab.Editor.Configuration;
using RenderLab.Editor.Diagnostics;

namespace RenderLab.Editor;

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
                .AddSingleton(Program.Logging!.Buffer)
                .AddLogging(builder => builder.ClearProviders().AddSerilog(Program.Logging.Logger, dispose: false))
                .AddSingleton<LogViewModel>()
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
                EditorDiagnostics.WriteError(Program.Options, args.Exception);
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
