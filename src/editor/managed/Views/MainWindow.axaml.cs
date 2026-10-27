using Avalonia.Controls;
using RenderLab.Editor.Host.Controls;
using RenderLab.Editor.Host.Services;
using RenderLab.Editor.Host.ViewModels;

namespace RenderLab.Editor.Host.Views;

public partial class MainWindow : Window
{
    private readonly NativeViewportService? viewportService;
    private readonly SmokeTest? smoke;
    internal RenderViewportHost Viewport { get; private set; } = null!;
    internal MainWindowViewModel ViewModel => (MainWindowViewModel)DataContext!;
    internal TextBox FocusBox => ManagedInput;
    internal ComboBox PipelinePicker => PipelineSelector;
    internal Slider ExposureSlider => ExposureControl;
    internal Button PauseButton => PauseAction;
    internal Button ResetButton => ResetAction;
    internal Button RecreateButton => RecreateAction;
    internal IReadOnlyList<CloseReport> CloseReports => viewportService!.CloseReports;
    internal event Action<ViewportStats>? FrameCompleted;

    // The previewer loads XAML without creating native rendering resources.
    public MainWindow() => InitializeComponent();

    public MainWindow(MainWindowViewModel viewModel, NativeViewportService viewportService, HostOptions options)
        : this()
    {
        DataContext = viewModel;
        this.viewportService = viewportService;
        viewportService.RecreateRequested += RecreateViewport;
        viewportService.FrameCompleted += ForwardFrame;
        viewportService.Failed += OnViewportFailure;
        Closing += (_, _) => Viewport.CloseViewport();
        Closed += (_, _) =>
        {
            viewportService.RecreateRequested -= RecreateViewport;
            viewportService.FrameCompleted -= ForwardFrame;
            viewportService.Failed -= OnViewportFailure;
        };
        RecreateViewport();
        if (options.Smoke)
        {
            smoke = new SmokeTest(this, options);
        }
    }

    private void OnViewportFailure(Exception error) => smoke?.Fail(error);

    private void ForwardFrame(ViewportStats stats) => FrameCompleted?.Invoke(stats);

    internal void RecreateViewport()
    {
        Viewport?.CloseViewport();
        ViewportContainer.Content = null;
        Viewport = new RenderViewportHost(viewportService!);
        ViewportContainer.Content = Viewport;
    }
}
