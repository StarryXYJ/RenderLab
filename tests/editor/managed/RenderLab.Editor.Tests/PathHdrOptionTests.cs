using Avalonia.Headless.NUnit;
using NUnit.Framework;
using RenderLab.Editor.Configuration;
using RenderLab.Editor.Services;
using RenderLab.Editor.ViewModels;

namespace RenderLab.Editor.Tests;

public class PathHdrOptionTests
{
    [AvaloniaTest]
    public void ToggleChangesOnlyTheOptionalPathFilterFlag()
    {
        using var service = new NativeViewportService(
            new EditorOptions("dx12", "fixture", "fixture", "fixture", false, true, true, 2, 0, 0));
        using var model = new RenderSettingsViewModel(service);
        var original = model.BuildConfiguration().Flags;
        Assert.That(original & 64u, Is.Zero);
        model.PathHdrNormalize = true;
        Assert.That(model.BuildConfiguration().Flags, Is.EqualTo(original | 64u));
        model.PathHdrNormalize = false;
        Assert.That(model.BuildConfiguration().Flags, Is.EqualTo(original));
    }
}
