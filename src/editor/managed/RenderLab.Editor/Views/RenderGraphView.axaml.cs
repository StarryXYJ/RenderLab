using Avalonia.Controls;
using Avalonia.Threading;
using RenderLab.Editor.ViewModels;

namespace RenderLab.Editor.Views;

internal partial class RenderGraphView : UserControl
{
    private readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromMilliseconds(500) };
    private RenderGraphViewModel? model;
    public RenderGraphView()
    {
        InitializeComponent();
        timer.Tick += (_, _) => model?.Refresh();
        AttachedToVisualTree += (_, _) =>
        {
            model = DataContext as RenderGraphViewModel;
            Matrix.Model = model;
            if (model is not null) { model.CanvasChanged += Matrix.Refresh; model.Refresh(); }
            Matrix.Refresh(); timer.Start();
        };
        DetachedFromVisualTree += (_, _) =>
        {
            timer.Stop();
            if (model is not null) { model.CanvasChanged -= Matrix.Refresh; }
            model = null;
        };
    }
}
