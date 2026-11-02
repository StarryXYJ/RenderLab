using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using RenderLab.Editor.ViewModels;
using System.Globalization;

namespace RenderLab.Editor.Controls;

internal sealed class FrameTimelineControl : Control
{
    internal ProfilerViewModel? Model { get; set; }
    public override void Render(DrawingContext context)
    {
        base.Render(context);
        var samples = Model?.Curve;
        var width = Math.Max(10, Bounds.Width - 52);
        var height = Math.Max(10, Bounds.Height - 23);
        void Text(string text, Point point, IBrush brush) => context.DrawText(new FormattedText(text,
            CultureInfo.InvariantCulture, FlowDirection.LeftToRight, Typeface.Default, 10, brush), point);
        if (samples is null || samples.Length < 2)
        {
            Text("No completed frame samples", new(0, 0), Brushes.Gray);
            return;
        }
        var gpuBySourceFrame = samples.Where(sample => sample.GpuMs.HasValue && sample.GpuFrame > 0)
            .GroupBy(sample => sample.GpuFrame).ToDictionary(group => group.Key, group => group.Last().GpuMs!.Value);
        var maximum = Math.Max(1, Math.Max(samples.Max(sample => Math.Max(sample.NativeMs, sample.IntervalMs ?? 0)),
            gpuBySourceFrame.Values.DefaultIfEmpty(0).Max())) * 1.05;
        Text("CPU Tick", new(0, 0), Brushes.LightSteelBlue);
        Text("Frame interval", new(75, 0), Brushes.Gray);
        Text("GPU Pass sum", new(172, 0), Brushes.MediumAquamarine);
        for (var line = 0; line <= 2; ++line)
        {
            var y = 23 + height * line / 2;
            context.DrawLine(new Pen(new SolidColorBrush(Color.Parse("#383838")), 1), new(0, y), new(width, y));
            Text($"{maximum * (1 - line / 2.0):F1} ms", new(width + 6, y - 6), Brushes.Gray);
        }
        for (var index = 1; index < samples.Length; ++index)
        {
            var x0 = (index - 1) * width / (samples.Length - 1); var x1 = index * width / (samples.Length - 1);
            void Line(double before, double after, IBrush color) => context.DrawLine(new Pen(color, 1.5),
                new(x0, 23 + height * (1 - before / maximum)), new(x1, 23 + height * (1 - after / maximum)));
            Line(samples[index - 1].NativeMs, samples[index].NativeMs, Brushes.LightSteelBlue);
            if (samples[index - 1].IntervalMs is { } i0 && samples[index].IntervalMs is { } i1) { Line(i0, i1, Brushes.Gray); }
            if (gpuBySourceFrame.TryGetValue(samples[index - 1].Frame, out var g0) && gpuBySourceFrame.TryGetValue(samples[index].Frame, out var g1)) { Line(g0, g1, Brushes.MediumAquamarine); }
        }
    }
}
