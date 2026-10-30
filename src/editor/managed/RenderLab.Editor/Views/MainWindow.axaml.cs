using Avalonia.Controls;
using Avalonia;
using Avalonia.Interactivity;
using Avalonia.Data;
using Avalonia.Threading;
using Avalonia.VisualTree;
using RenderLab.Editor.Docking;
using Microsoft.Extensions.Logging;
using RenderLab.Editor.Controls;
using RenderLab.Editor.Services;
using RenderLab.Editor.ViewModels;
using RenderLab.Editor.Configuration;
using RenderLab.Editor.Diagnostics;
using RenderLab.Editor.Interop;

namespace RenderLab.Editor.Views;

public partial class MainWindow : Window
{
    private readonly NativeViewportService? viewportService;
    private readonly SmokeTest? smoke;
    private readonly ViewportBenchmark? benchmark;
    private RenderSettingsView settings = null!;
    private ContentControl? viewportContainer;
    private readonly string layoutPath = "";
    private readonly EditorOptions? options;
    private readonly ILogger? logger;
    private bool closingWindow;
    private IDisposable? dragWatch;
    private readonly DispatcherTimer logStatusTimer = new() { Interval = TimeSpan.FromMilliseconds(250) };
    internal WorkbenchDockFactory Docks { get; private set; } = null!;
    internal NativeViewportService ViewportService => viewportService!;
    internal RenderViewportHost Viewport { get; private set; } = null!;
    internal MainWindowViewModel ViewModel => (MainWindowViewModel)DataContext!;
    internal TextBox FocusBox => settings.FindControl<TextBox>("ManagedInput")!;
    internal ComboBox PipelinePicker => settings.FindControl<ComboBox>("PipelineSelector")!;
    internal Slider ExposureSlider => settings.FindControl<Slider>("ExposureControl")!;
    internal Button PauseButton => settings.FindControl<Button>("PauseAction")!;
    internal Button ResetButton => settings.FindControl<Button>("ResetAction")!;
    internal Button RecreateButton => settings.FindControl<Button>("RecreateAction")!;
    internal IReadOnlyList<CloseReport> CloseReports => viewportService!.CloseReports;
    internal event Action<ViewportStats>? StatsUpdated;

    // The previewer loads XAML without creating native rendering resources.
    public MainWindow() => InitializeComponent();

    public MainWindow(MainWindowViewModel viewModel, NativeViewportService viewportService, EditorOptions options,
        LogViewModel logModel, ILogger<MainWindow> logger)
        : this()
    {
        DataContext = viewModel;
        this.viewportService = viewportService;
        this.options = options;
        this.logger = logger;
        logStatusTimer.Tick += (_, _) => UpdateLogStatus();
        Opened += (_, _) => logStatusTimer.Start();
        Docks = new WorkbenchDockFactory(new()
        {
            ["Scene"] = CreateSceneView,
            ["Settings"] = () => settings = new RenderSettingsView { DataContext = viewModel },
            ["Render Graph"] = () => new TextBlock { Text = "Render Graph snapshots are not available in this build.", Margin = new Thickness(16), TextWrapping = Avalonia.Media.TextWrapping.Wrap },
            ["Profiler"] = () =>
            {
                var view = new TextBlock { Margin = new Thickness(16), TextWrapping = Avalonia.Media.TextWrapping.Wrap, DataContext = viewModel };
                view.Bind(TextBlock.TextProperty, new Binding(nameof(MainWindowViewModel.PerformanceText)));
                return view;
            },
            ["Log"] = () => new LogView { DataContext = logModel }
        }, logger);
        Workbench.Factory = Docks;
        Workbench.InitializeFactory = false;
        Workbench.InitializeLayout = false;
        var settingsRoot = options.Smoke || options.BenchmarkSeconds > 0 ? options.Output : Path.Combine(Path.GetDirectoryName(options.Project)!, "UserSettings", "Editor");
        layoutPath = Path.Combine(settingsRoot, "layout.json");
        Docks.UseLayout(Docks.CreateLayout());
        if (!options.Smoke && options.BenchmarkSeconds == 0) { LoadLayout(); }
        Workbench.Layout = Docks.Root;
        dragWatch = Dock.Avalonia.Controls.DockControl.IsDraggingDockProperty.Changed.AddClassHandler<Dock.Avalonia.Controls.DockControl>((control, args) =>
        {
            SetDragging(control.IsDraggingDock);
        });
        Docks.WindowMoveDragBegin += (_, _) => SetDragging(true);
        Docks.WindowMoveDragEnd += (_, _) => SetDragging(false);
        Docks.DockableClosed += (_, _) => RefreshMenus();
        Docks.DockableAdded += (_, _) => RefreshMenus();
        viewportService.RecreateRequested += RecreateViewport;
        viewportService.StatsUpdated += ForwardStats;
        viewportService.Failed += OnViewportFailure;
        Closing += (_, _) =>
        {
            closingWindow = true;
            viewportService.CancelSwitch();
            try { Directory.CreateDirectory(Path.GetDirectoryName(layoutPath)!); File.WriteAllText(layoutPath, Docks.Serialize()); }
            catch (Exception error) { logger.LogWarning(error, "Saving layout failed"); }
            Viewport?.CloseViewport();
            Docks.CloseFloatingWindows();
        };
        Closed += (_, _) =>
        {
            dragWatch?.Dispose();
            logStatusTimer.Stop();
            viewportService.RecreateRequested -= RecreateViewport;
            viewportService.StatsUpdated -= ForwardStats;
            viewportService.Failed -= OnViewportFailure;
        };
        RefreshMenus();
        if (options.Smoke)
        {
            smoke = new SmokeTest(this, options);
        }
        else if (options.BenchmarkSeconds > 0)
        {
            benchmark = new ViewportBenchmark(this, viewportService, options);
        }
    }

    private void OnViewportFailure(Exception error) => smoke?.Fail(error);

    private void ForwardStats(ViewportStats stats)
    {
        StatsUpdated?.Invoke(stats);
    }
    private void UpdateLogStatus()
    {
        var counts = Program.Logging!.Buffer.Counts;
        LogStatus.Content = $"Log · W {counts.Warnings} / E {counts.Errors} · dropped {viewportService!.NativeLogDropped + (ulong)counts.Dropped}";
    }

    internal void RecreateViewport()
    {
        if (viewportContainer is null || !viewportContainer.IsAttachedToVisualTree()) { Docks.Show("Scene"); return; }
        Viewport?.CloseViewport();
        viewportContainer.Content = null;
        viewportContainer.Content = CreateHost(viewportContainer);
    }
    private Control CreateSceneView()
    {
        var container = new ContentControl();
        container.Content = CreateHost(container);
        return container;
    }
    private RenderViewportHost CreateHost(ContentControl container)
    {
        var host = new RenderViewportHost(viewportService!);
        host.NativeCreated += () => { Viewport = host; viewportContainer = container; };
        return host;
    }

    private void SetDragging(bool dragging)
    {
        if (Viewport is null) { return; }
        if (dragging)
        {
            NativeMethods.ReleaseCapture();
            NativeMethods.SetFocus(TryGetPlatformHandle()?.Handle ?? 0);
            Viewport.IsVisible = false;
        }
        else if (!closingWindow) { Viewport.IsVisible = true; }
    }
    private void ShowPanel(object? sender, RoutedEventArgs args)
    {
        if (sender is MenuItem { Tag: string id }) { Docks.Show(id); RefreshMenus(); }
    }
    private void OpenLog(object? sender, RoutedEventArgs args) { Docks.Show("Log"); RefreshMenus(); }
    private void DefaultLayout(object? sender, RoutedEventArgs args) => ResetLayout(false);
    private void DiagnosticLayout(object? sender, RoutedEventArgs args) => ResetLayout(true);
    private void RestoreLayout(object? sender, RoutedEventArgs args)
    {
        viewportService!.CancelSwitch();
        Viewport?.CloseViewport();
        Docks.CloseFloatingWindows();
        Workbench.Layout = null;
        LoadLayout();
        Workbench.Layout = Docks.Root;
        RefreshMenus();
    }
    internal void ResetLayout(bool diagnostics)
    {
        viewportService!.CancelSwitch();
        Viewport?.CloseViewport();
        Docks.CloseFloatingWindows();
        Workbench.Layout = null;
        Docks.UseLayout(Docks.CreateLayout());
        Workbench.Layout = Docks.Root;
        if (diagnostics) { Docks.Show("Render Graph"); Docks.Show("Profiler"); Docks.Show("Log"); }
        RefreshMenus();
    }
    private void LoadLayout()
    {
        try
        {
            var screen = Screens.Primary;
            var area = screen is null ? new Rect(0, 0, 1920, 1080) : new Rect(screen.WorkingArea.X, screen.WorkingArea.Y, screen.WorkingArea.Width, screen.WorkingArea.Height);
            Docks.UseLayout(File.Exists(layoutPath) ? Docks.Restore(File.ReadAllText(layoutPath), area) : Docks.CreateLayout());
        }
        catch (Exception error) { logger?.LogWarning(error, "Restoring layout failed; using default"); Docks.UseLayout(Docks.CreateLayout()); }
    }
    private void RefreshMenus()
    {
        if (Docks is null) { return; }
        foreach (var menu in new[] { SceneMenu, SettingsMenu, GraphMenu, ProfilerMenu, LogMenu })
        {
            var id = (string)menu.Tag!;
            menu.Header = (Docks.IsOpen(id) ? "✓ " : "") + (id == "Settings" ? "Render Settings" : id);
        }
    }
}
