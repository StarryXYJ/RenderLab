using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace RenderLab.Editor.Host;

internal sealed class MainWindow : Window
{
    private readonly HostOptions options;
    private readonly ContentControl viewportContainer = new();
    private readonly TextBlock status = new() { Text = "Starting native viewport…" };
    private readonly TextBlock details = new() { TextWrapping = TextWrapping.Wrap };
    private readonly TextBlock errorText = new() { TextWrapping = TextWrapping.Wrap, Foreground = Brushes.OrangeRed };
    internal readonly TextBox FocusBox = new() { PlaceholderText = "Click here to test keyboard focus", AcceptsReturn = true, MinHeight = 100 };
    internal RenderViewportHost Viewport { get; private set; } = null!;
    internal List<CloseReport> CloseReports { get; } = [];
    internal event Action<ViewportStats>? FrameCompleted;
    private uint pipeline = 2;
    private float exposure;
    private SmokeTest? smoke;

    internal MainWindow(HostOptions options)
    {
        this.options = options;
        Title = $"RenderLab · Avalonia Host · {options.Backend.ToUpperInvariant()}";
        Width = 1200;
        Height = 780;
        MinWidth = 880;
        MinHeight = 540;
        Background = new SolidColorBrush(Color.Parse("#11151D"));

        var pipelinePicker = new ComboBox { ItemsSource = new[] { "Forward", "Deferred", "Ray Tracing" }, SelectedIndex = 1, HorizontalAlignment = HorizontalAlignment.Stretch };
        var exposureSlider = new Slider { Minimum = -4, Maximum = 4, Value = 0 };
        var reset = new Button { Content = "Reset camera", HorizontalAlignment = HorizontalAlignment.Stretch };
        var pause = new Button { Content = "Pause viewport", HorizontalAlignment = HorizontalAlignment.Stretch };
        var recreate = new Button { Content = "Recreate viewport", HorizontalAlignment = HorizontalAlignment.Stretch };
        var left = new StackPanel { Spacing = 16 };
        left.Children.Add(Label("RENDER CONTROLS", 13, "#8B9BB4"));
        left.Children.Add(Label(options.Backend == "dx12" ? "Direct3D 12" : "Vulkan", 23, "#F1F5FC"));
        left.Children.Add(Label("Pipeline", 13, "#A7B4C8"));
        left.Children.Add(pipelinePicker);
        left.Children.Add(Label("Exposure · −4 to +4 stops", 13, "#A7B4C8"));
        left.Children.Add(exposureSlider);
        left.Children.Add(reset);
        left.Children.Add(pause);
        left.Children.Add(recreate);
        left.Children.Add(new TextBlock { Text = "The native viewport is embedded in this window. All surrounding controls are Avalonia.", TextWrapping = TextWrapping.Wrap, Foreground = new SolidColorBrush(Color.Parse("#8797AF")) });

        var right = new StackPanel { Spacing = 16 };
        right.Children.Add(Label("HOST CONTRACT", 13, "#8B9BB4"));
        right.Children.Add(details);
        right.Children.Add(Label("Managed input", 13, "#A7B4C8"));
        right.Children.Add(FocusBox);
        right.Children.Add(new TextBlock { Text = "Click the scene for camera input.\nWASD / Q E to move.\nArrows or right drag to look.\n\nThe child HWND owns its pixels; viewport overlays belong in the native renderer.", TextWrapping = TextWrapping.Wrap, Foreground = new SolidColorBrush(Color.Parse("#8797AF")) });
        right.Children.Add(errorText);

        var scenePanel = new Grid { RowDefinitions = new RowDefinitions("Auto,*") };
        scenePanel.Children.Add(new Border
        {
            Padding = new Thickness(16, 12),
            Background = new SolidColorBrush(Color.Parse("#1A2230")),
            Child = Label("SCENE VIEW  /  native child HWND", 13, "#B8C6DB")
        });
        Grid.SetRow(viewportContainer, 1);
        scenePanel.Children.Add(viewportContainer);

        var body = new Grid { ColumnDefinitions = new ColumnDefinitions("220,6,*,6,250") };
        var leftBorder = Panel(left);
        var rightBorder = Panel(right);
        Grid.SetColumn(leftBorder, 0);
        Grid.SetColumn(scenePanel, 2);
        Grid.SetColumn(rightBorder, 4);
        body.Children.Add(leftBorder);
        body.Children.Add(scenePanel);
        body.Children.Add(rightBorder);
        foreach (var column in new[] { 1, 3 })
        {
            var splitter = new GridSplitter { ResizeDirection = GridResizeDirection.Columns, HorizontalAlignment = HorizontalAlignment.Stretch, Background = new SolidColorBrush(Color.Parse("#283449")) };
            Grid.SetColumn(splitter, column);
            body.Children.Add(splitter);
        }

        var layout = new Grid { RowDefinitions = new RowDefinitions("Auto,*,Auto") };
        layout.Children.Add(new Border
        {
            Padding = new Thickness(20, 16),
            Child = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 20, Children =
            {
                Label("RenderLab", 25, "#F1F5FC"),
                new TextBlock { Text = "AVALONIA / NATIVE HOST EXPERIMENT", VerticalAlignment = VerticalAlignment.Center, Foreground = new SolidColorBrush(Color.Parse("#7FADE6")) }
            } }
        });
        Grid.SetRow(body, 1);
        layout.Children.Add(body);
        var footer = new Border { Padding = new Thickness(18, 10), Child = status };
        Grid.SetRow(footer, 2);
        layout.Children.Add(footer);
        Content = layout;

        RecreateViewport();
        pipelinePicker.SelectionChanged += (_, _) =>
        {
            var selected = (uint)(pipelinePicker.SelectedIndex + 1);
            try
            {
                Viewport.Configure(selected, exposure);
                pipeline = selected;
                errorText.Text = "";
            }
            catch (Exception error)
            {
                errorText.Text = error.Message;
                pipelinePicker.SelectedIndex = (int)pipeline - 1;
            }
        };
        exposureSlider.ValueChanged += (_, _) =>
        {
            exposure = (float)exposureSlider.Value;
            Viewport.Configure(pipeline, exposure);
        };
        reset.Click += (_, _) => Viewport.Configure(pipeline, exposure, true);
        pause.Click += (_, _) =>
        {
            Viewport.Paused = !Viewport.Paused;
            pause.Content = Viewport.Paused ? "Resume viewport" : "Pause viewport";
        };
        recreate.Click += (_, _) => RecreateViewport();
        Closing += (_, _) => Viewport.CloseViewport();
        if (options.Smoke)
        {
            smoke = new SmokeTest(this, options);
        }
    }

    private static TextBlock Label(string text, double size, string color) => new()
    {
        Text = text, FontSize = size, Foreground = new SolidColorBrush(Color.Parse(color))
    };

    private static Border Panel(Control child) => new()
    {
        Padding = new Thickness(18), Background = new SolidColorBrush(Color.Parse("#171E29")), Child = child
    };

    internal void RecreateViewport()
    {
        Viewport?.CloseViewport();
        viewportContainer.Content = null;
        Viewport = new RenderViewportHost(options);
        Viewport.Configure(pipeline, exposure);
        Viewport.NativeClosed += report =>
        {
            CloseReports.Add(report);
            File.WriteAllText(Path.Combine(options.Output, "close-reports.json"), JsonSerializer.Serialize(CloseReports, JsonOptions));
        };
        Viewport.Failed += error =>
        {
            errorText.Text = error.Message;
            File.WriteAllText(Path.Combine(options.Output, "managed-error.txt"), error.ToString());
            smoke?.Fail(error);
        };
        Viewport.FrameCompleted += stats =>
        {
            if (stats.Frames % 8 == 0 || stats.PausedTicks != 0)
            {
                status.Text = $"{stats.Width} × {stats.Height} px   ·   DPI {stats.Dpi}   ·   frame {stats.Frames}   ·   RT {stats.ReflectionSamples} spp";
                details.Text = $"NativeControlHost\nC ABI v{NativeMethods.AbiVersion()} / UI STA\n\nClient: {stats.Width} × {stats.Height} px\nScale: {RenderScaling:F2}\nResizes: {stats.Resizes}\nPaused ticks: {stats.PausedTicks}\nNative: {stats.NativeBytes / 1048576.0:F1} MiB\n\nFocus: {stats.Focused}\nCapture: {stats.Captured}";
            }
            FrameCompleted?.Invoke(stats);
        };
        viewportContainer.Content = Viewport;
    }

    internal static JsonSerializerOptions JsonOptions { get; } = new() { IncludeFields = true, WriteIndented = true };
}
