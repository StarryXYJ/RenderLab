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
        if (samples is null || samples.Length < 2) { return; }
        var maximum = Math.Max(1, samples.Max(sample => Math.Max(sample.NativeMs, sample.IntervalMs ?? 0)));
        var gpuBySourceFrame = samples.Where(sample => sample.GpuMs.HasValue && sample.GpuFrame > 0)
            .GroupBy(sample => sample.GpuFrame).ToDictionary(group => group.Key, group => group.Last().GpuMs!.Value);
        var height = Math.Max(10, Bounds.Height - 25); var width = Math.Max(10, Bounds.Width);
        context.DrawText(new FormattedText($"{maximum:F1} ms · CPU work (blue), interval (gray), completed GPU Pass sum (green)",
            CultureInfo.InvariantCulture, FlowDirection.LeftToRight, Typeface.Default, 11, Brushes.Gainsboro), new(2, 1));
        for (var index = 1; index < samples.Length; ++index)
        {
            var x0 = (index - 1) * width / (samples.Length - 1); var x1 = index * width / (samples.Length - 1);
            void Line(double before, double after, IBrush color) => context.DrawLine(new Pen(color, 1.5),
                new(x0, 25 + height * (1 - before / maximum)), new(x1, 25 + height * (1 - after / maximum)));
            Line(samples[index - 1].NativeMs, samples[index].NativeMs, Brushes.SteelBlue);
            if (samples[index - 1].IntervalMs is { } i0 && samples[index].IntervalMs is { } i1) { Line(i0, i1, Brushes.Gray); }
            if (gpuBySourceFrame.TryGetValue(samples[index - 1].Frame, out var g0) && gpuBySourceFrame.TryGetValue(samples[index].Frame, out var g1)) { Line(g0, g1, Brushes.SeaGreen); }
        }
    }
}
