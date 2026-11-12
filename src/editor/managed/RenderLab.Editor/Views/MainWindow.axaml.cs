using Avalonia.Controls;
using Avalonia.Platform.Storage;
using System.Text.Json;
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
    private NativeViewportService? viewportService;
    private ProjectSession? projectSession;
    private readonly ProjectSessionFactory? sessions;
    private readonly RecentProjects? recent;
    private readonly LogViewModel? logModel;
    private CancellationTokenSource? projectSwitch;
    private TaskCompletionSource? firstFrame;
    private bool initialRecorded, projectReady;
    private readonly SmokeTest? smoke;
    private readonly ViewportBenchmark? benchmark;
    private readonly WorkbenchSoak? soak;
    private RenderSettingsView settings = null!;
    private ContentControl? viewportContainer;
    private string layoutPath = "";
    private EditorOptions? options;
    private readonly ILogger? logger;
    private bool closingWindow;
    private IDisposable? dragWatch;
    private readonly DispatcherTimer logStatusTimer = new() { Interval = TimeSpan.FromMilliseconds(250) };
    private readonly DispatcherTimer observationTimer = new() { Interval = TimeSpan.FromMilliseconds(500) };
    internal WorkbenchDockFactory Docks { get; private set; } = null!;
    internal RenderGraphViewModel GraphModel { get; private set; } = null!;
    internal ProfilerViewModel ProfilerModel { get; private set; } = null!;
    internal NativeViewportService ViewportService => viewportService!;
    internal RenderViewportHost Viewport { get; private set; } = null!;
    internal MainWindowViewModel ViewModel => (MainWindowViewModel)DataContext!;
    internal TextBox FocusBox
    {
        get
        {
            if (options?.Smoke == true) { settings.FindControl<Expander>("HostDiagnostics")!.IsExpanded = true; }
            return settings.FindControl<TextBox>("ManagedInput")!;
        }
    }
    internal ComboBox PipelinePicker => settings.FindControl<ComboBox>("PipelineSelector")!;
    internal Slider ExposureSlider => settings.FindControl<Slider>("ExposureControl")!;
    internal Button PauseButton => this.FindControl<Button>("PauseAction")!;
    internal Button ResetButton => this.FindControl<Button>("ResetAction")!;
    internal Button RecreateButton => settings.FindControl<Button>("RecreateAction")!;
    internal IReadOnlyList<CloseReport> CloseReports => viewportService!.CloseReports;
    internal event Action<ViewportStats>? StatsUpdated;

    // The previewer loads XAML without creating native rendering resources.
    public MainWindow() => InitializeComponent();

    public MainWindow(ProjectSessionFactory sessions, StartupOptions startup, RecentProjects recent,
        LogViewModel logModel, ILogger<MainWindow> logger) : this()
    {
        this.sessions = sessions;
        this.recent = recent;
        this.logModel = logModel;
        this.logger = logger;
        logStatusTimer.Tick += (_, _) => UpdateLogStatus();
        Opened += (_, _) => logStatusTimer.Start();
        observationTimer.Tick += (_, _) => { if (options?.Observation == "graph") { GraphModel.Refresh(); } else { ProfilerModel.Refresh(); } };
        Opened += (_, _) => { if (options?.Observation is "graph" or "readback") { observationTimer.Start(); } };
        Closing += (_, _) =>
        {
            closingWindow = true;
            projectSwitch?.Cancel();
            DetachProject();
        };
        Closed += (_, _) =>
        {
            dragWatch?.Dispose();
            logStatusTimer.Stop();
            observationTimer.Stop();
        };
        dragWatch = Dock.Avalonia.Controls.DockControl.IsDraggingDockProperty.Changed.AddClassHandler<Dock.Avalonia.Controls.DockControl>((control, args) => SetDragging(control.IsDraggingDock));
        AttachProject(startup.Value.Project);
        RefreshRecent();
        if (options!.Smoke) { smoke = new SmokeTest(this, options); }
        else if (options.BenchmarkSeconds > 0) { benchmark = new ViewportBenchmark(this, viewportService!, options); }
        else if (options.SoakSeconds > 0) { soak = new WorkbenchSoak(this, viewportService!, options); }
    }

    private void AttachProject(string project, DeviceSelection? device = null)
    {
        projectSession = sessions!.Create(project, device);
        options = projectSession.Options;
        var viewModel = projectSession.ViewModel;
        var viewportService = projectSession.Viewport;
        this.viewportService = viewportService;
        DataContext = viewModel;
        initialRecorded = false;
        projectReady = false;
        GraphModel = new RenderGraphViewModel(viewportService);
        ProfilerModel = new ProfilerViewModel(viewportService, GraphModel);
        Docks = new WorkbenchDockFactory(new()
        {
            ["Scene"] = CreateSceneView,
            ["Settings"] = () => settings = new RenderSettingsView { DataContext = viewModel },
            ["Render Graph"] = () => new RenderGraphView { DataContext = GraphModel },
            ["Profiler"] = () => new ProfilerView { DataContext = ProfilerModel },
            ["Log"] = () => new LogView { DataContext = logModel }
        }, logger!);
        Workbench.Factory = Docks;
        ProfilerModel.GraphRequested += () => Docks.Show("Render Graph");
        Workbench.InitializeFactory = false;
        Workbench.InitializeLayout = false;
        var settingsRoot = options.Smoke || options.Isolated || options.BenchmarkSeconds > 0 || options.SoakSeconds > 0 ? options.Output : Path.Combine(Path.GetDirectoryName(options.Project)!, "UserSettings", "Editor");
        layoutPath = Path.Combine(settingsRoot, "layout.json");
        Docks.UseLayout(Docks.CreateLayout());
        if (!options.Smoke && !options.Isolated && options.BenchmarkSeconds == 0 && options.SoakSeconds == 0) { LoadLayout(); }
        Workbench.Layout = Docks.Root;
        Docks.WindowMoveDragBegin += (_, _) => SetDragging(true);
        Docks.WindowMoveDragEnd += (_, _) => SetDragging(false);
        Docks.DockableClosed += (_, _) => RefreshMenus();
        Docks.DockableAdded += (_, _) => RefreshMenus();
        viewportService.RecreateRequested += RecreateViewport;
        viewportService.StatsUpdated += ForwardStats;
        viewportService.Failed += OnViewportFailure;
        RefreshMenus();
    }

    private void DetachProject()
    {
        if (projectSession is null) { return; }
        viewportService?.CancelSwitch();
        if (Docks is not null)
        {
            if (projectReady)
            {
                try { Directory.CreateDirectory(Path.GetDirectoryName(layoutPath)!); File.WriteAllText(layoutPath, Docks.Serialize()); }
                catch (Exception error) { logger?.LogWarning(error, "Saving layout failed"); }
            }
            Viewport?.CloseViewport();
            Docks.CloseFloatingWindows();
            Workbench.Layout = null;
        }
        if (viewportService is not null)
        {
            viewportService.RecreateRequested -= RecreateViewport;
            viewportService.StatsUpdated -= ForwardStats;
            viewportService.Failed -= OnViewportFailure;
        }
        DataContext = null;
        viewportContainer = null;
        Viewport = null!;
        projectSession.Dispose();
        projectSession = null;
        viewportService = null;
        Docks = null!;
    }

    private async void OpenProject(object? sender, RoutedEventArgs args)
    {
        var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Open RenderLab project", AllowMultiple = false,
            FileTypeFilter = [new FilePickerFileType("RenderLab project") { Patterns = ["*.rlproject"] }]
        });
        if (files.Count == 0) { return; }
        var path = files[0].TryGetLocalPath();
        if (path is not null) { await SwitchProject(path); }
    }

    private void ClearRecentProjects(object? sender, RoutedEventArgs args) { recent!.Clear(); RefreshRecent(); }

    private void RefreshRecent()
    {
        if (recent is null) { return; }
        var entries = new List<MenuItem>();
        foreach (var path in recent.Items)
        {
            var item = new MenuItem { Header = Path.GetFileNameWithoutExtension(path) };
            ToolTip.SetTip(item, path);
            item.Click += async (_, _) => await SwitchProject(path);
            entries.Add(item);
        }
        if (entries.Count == 0) { entries.Add(new MenuItem { Header = "No recent projects", IsEnabled = false }); }
        RecentMenu.ItemsSource = entries;
    }

    private static string ValidateProject(string project)
    {
        var path = Path.GetFullPath(project);
        if (!string.Equals(Path.GetExtension(path), ".rlproject", StringComparison.OrdinalIgnoreCase))
        { throw new ArgumentException("Select a .rlproject file."); }
        using var json = JsonDocument.Parse(File.ReadAllText(path));
        if (json.RootElement.ValueKind != JsonValueKind.Object ||
            !json.RootElement.TryGetProperty("startScene", out var scene) || scene.ValueKind != JsonValueKind.String ||
            string.IsNullOrWhiteSpace(scene.GetString()))
        { throw new ArgumentException("The project does not contain a valid startScene."); }
        return path;
    }

    private async Task SwitchProject(string project)
    {
        if (projectSwitch is not null || closingWindow) { return; }
        string target;
        try { target = ValidateProject(project); }
        catch (Exception error)
        {
            if (!File.Exists(project)) { recent!.Remove(project); RefreshRecent(); }
            await ShowProjectError("Cannot open project", error.Message);
            return;
        }
        if (options is not null && projectSession is not null && string.Equals(target, options.Project, StringComparison.OrdinalIgnoreCase)) { return; }
        if (viewportService?.DeviceSwitch.Busy == true)
        { await ShowProjectError("Cannot open project", "Wait for the graphics device switch to finish."); return; }
        var previous = projectSession?.Options.Project;
        var device = viewportService?.DeviceSwitch.Current;
        projectSwitch = new CancellationTokenSource();
        var token = projectSwitch.Token;
        FileMenu.IsEnabled = false;
        SetProjectControlsEnabled(false);
        try
        {
            var result = await ProjectSwitchTransaction.RunAsync(target, previous,
                (path, cancellation) => OpenProjectSession(path, device, cancellation), token);
            if (result.TargetError is not null)
            {
                var message = result.TargetError.Message;
                if (result.Restored) { message += "\n\nPrevious project reopened."; }
                else
                {
                    DetachProject();
                    if (result.RollbackError is not null) { message += "\n\nCould not reopen previous project: " + result.RollbackError.Message; }
                }
                if (!closingWindow) { await ShowProjectError("Cannot open project", message); }
            }
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested) { }
        finally
        {
            firstFrame = null;
            projectSwitch.Dispose(); projectSwitch = null;
            if (!closingWindow) { FileMenu.IsEnabled = true; SetProjectControlsEnabled(projectSession is not null); }
        }
    }

    private async Task OpenProjectSession(string path, DeviceSelection? device, CancellationToken token)
    {
        DetachProject();
        token.ThrowIfCancellationRequested();
        firstFrame = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        AttachProject(path, device);
        Docks.Show("Scene");
        await firstFrame.Task.WaitAsync(TimeSpan.FromSeconds(120), token);
    }

    private void SetProjectControlsEnabled(bool enabled)
    {
        WindowMenu.IsEnabled = enabled;
        LayoutMenu.IsEnabled = enabled;
        SceneActionsMenu.IsEnabled = enabled;
        Workbench.IsEnabled = enabled;
    }

    private async Task ShowProjectError(string title, string message)
    {
        var dialog = new Window { Title = title, Width = 520, SizeToContent = SizeToContent.Height,
            WindowStartupLocation = WindowStartupLocation.CenterOwner, CanResize = false };
        var panel = new StackPanel { Margin = new Thickness(20), Spacing = 16 };
        panel.Children.Add(new TextBlock { Text = message, TextWrapping = Avalonia.Media.TextWrapping.Wrap });
        var button = new Button { Content = "OK", HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Right };
        button.Click += (_, _) => dialog.Close();
        panel.Children.Add(button); dialog.Content = panel;
        await dialog.ShowDialog(this);
    }

    private void OnViewportFailure(Exception error) { firstFrame?.TrySetException(error); smoke?.Fail(error); }

    private void ForwardStats(ViewportStats stats)
    {
        if (stats.Ready != 0 && stats.Frames > 0)
        {
            projectReady = true;
            firstFrame?.TrySetResult();
            if (!initialRecorded && options is { Smoke: false, Isolated: false, BenchmarkSeconds: 0, SoakSeconds: 0 })
            { initialRecorded = true; recent!.Add(options.Project); RefreshRecent(); }
        }
        StatsUpdated?.Invoke(stats);
    }
    private void UpdateLogStatus()
    {
        if (viewportService is null) { return; }
        var counts = Program.Logging!.Buffer.Counts;
        LogStatus.Content = $"Log · W {counts.Warnings} / E {counts.Errors} · dropped {viewportService!.NativeLogDropped + (ulong)counts.Dropped}";
        if (viewportService.NativeLogFileFailed || Program.Logging.SinkError.Length > 0) { LogStatus.Content += " · log file failed"; }
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
