using Avalonia.Controls;
using Avalonia;
using Avalonia.Interactivity;
using Avalonia.Data;
using Avalonia.Threading;
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
    private readonly RenderSettingsView settings = new();
    private readonly ContentControl viewportContainer = new();
    private readonly ContentControl sceneView = new();
    private readonly string layoutPath = "";
    private readonly EditorOptions? options;
    private readonly ILogger? logger;
    private bool closingWindow;
    private IDisposable? dragWatch;
    internal WorkbenchDockFactory Docks { get; private set; } = null!;
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
        this.options = options;
        this.logger = logger;
        settings.DataContext = viewModel;
        sceneView.Content = viewportContainer;
        var logView = new LogView { DataContext = logModel };
        var pendingGraph = new TextBlock { Text = "Render Graph snapshots are not available in this build.", Margin = new Thickness(16), TextWrapping = Avalonia.Media.TextWrapping.Wrap };
        var pendingProfiler = new TextBlock { Margin = new Thickness(16), TextWrapping = Avalonia.Media.TextWrapping.Wrap, DataContext = viewModel };
        pendingProfiler.Bind(TextBlock.TextProperty, new Binding(nameof(MainWindowViewModel.PerformanceText)));
        Docks = new WorkbenchDockFactory(new()
        {
            ["Scene"] = sceneView,
            ["Settings"] = settings,
            ["Render Graph"] = pendingGraph,
            ["Profiler"] = pendingProfiler,
            ["Log"] = logView
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
        this.viewportService = viewportService;
        viewportService.RecreateRequested += RecreateViewport;
        viewportService.StatsUpdated += ForwardStats;
        viewportService.Failed += OnViewportFailure;
        Closing += (_, _) =>
        {
            closingWindow = true;
            try { Directory.CreateDirectory(Path.GetDirectoryName(layoutPath)!); File.WriteAllText(layoutPath, Docks.Serialize()); }
            catch (Exception error) { logger.LogWarning(error, "Saving layout failed"); }
            Viewport?.CloseViewport();
            Docks.CloseFloatingWindows();
        };
        Closed += (_, _) =>
        {
            dragWatch?.Dispose();
            viewportService.RecreateRequested -= RecreateViewport;
            viewportService.StatsUpdated -= ForwardStats;
            viewportService.Failed -= OnViewportFailure;
        };
        RecreateViewport();
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
        var counts = Program.Logging!.Buffer.Counts;
        LogStatus.Content = $"Log · W {counts.Warnings} / E {counts.Errors} · dropped {viewportService!.NativeLogDropped + (ulong)counts.Dropped}";
    }

    internal void RecreateViewport()
    {
        Viewport?.CloseViewport();
        viewportContainer.Content = null;
        Viewport = new RenderViewportHost(viewportService!);
        viewportContainer.Content = Viewport;
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
        Viewport.CloseViewport();
        Docks.CloseFloatingWindows();
        Workbench.Layout = null;
        LoadLayout();
        Workbench.Layout = Docks.Root;
        RefreshMenus();
    }
    internal void ResetLayout(bool diagnostics)
    {
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
