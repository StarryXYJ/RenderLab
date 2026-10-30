using Avalonia.Controls;
using Avalonia.Threading;
using RenderLab.Editor.ViewModels;

namespace RenderLab.Editor.Views;

internal partial class ProfilerView : UserControl
{
    private readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromMilliseconds(250) };
    private ProfilerViewModel? model;
    public ProfilerView()
    {
        InitializeComponent();
        timer.Tick += (_, _) => model?.Refresh();
        AttachedToVisualTree += (_, _) =>
        {
            model = DataContext as ProfilerViewModel; Timeline.Model = model;
            if (model is not null) { model.CurveChanged += Timeline.InvalidateVisual; model.Refresh(); }
            timer.Start();
        };
        DetachedFromVisualTree += (_, _) =>
        {
            timer.Stop();
            if (model is not null) { model.CurveChanged -= Timeline.InvalidateVisual; }
            model = null;
        };
    }
}
