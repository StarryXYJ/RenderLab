using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.NUnit;
using Avalonia.Markup.Xaml.Styling;
using Avalonia.Themes.Fluent;
using Avalonia.Threading;
using Avalonia.VisualTree;
using AutoSettingUI.Avalonia.Attributes;
using AutoSettingUI.Avalonia.Controls;
using AutoSettingUI.Core.Attributes;
using NUnit.Framework;

namespace RenderLab.Editor.Tests;

[SettingUI]
public sealed class AutoSettingProbe
{
    [Title("Name")]
    public string Name { get; set; } = "Before";

    [Title("Samples"), NumericUpDown(Minimum = 1, Maximum = 128)]
    public int Samples { get; set; } = 16;

    [Title("Enabled"), CheckBox]
    public bool Enabled { get; set; } = true;
}

public class AutoSettingUiTests
{
    [AvaloniaTest]
    public void PublishedPanelCreatesControlsAndWritesBackToDto()
    {
        var styles = Application.Current!.Styles;
        var fluent = new FluentTheme { DensityStyle = DensityStyle.Compact };
        var panelStyle = new StyleInclude(new Uri("avares://AutoSettingUI.Avalonia/"))
        {
            Source = new Uri("avares://AutoSettingUI.Avalonia/Themes/Generic.axaml")
        };
        styles.Add(fluent);
        styles.Add(panelStyle);
        var dto = new AutoSettingProbe();
        var panel = new AvaloniaAutoSettingPanel { Targets = new object[] { dto }, ShowNavigation = false };
        var window = new Window { Width = 400, Height = 400, Content = panel };
        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            var controls = panel.GetVisualDescendants().ToArray();
            controls.OfType<TextBox>().Single(c => c.Text == "Before").Text = "After";
            controls.OfType<NumericUpDown>().Single().Value = 32;
            controls.OfType<CheckBox>().Single().IsChecked = false;
            Dispatcher.UIThread.RunJobs();
            Assert.Multiple(() =>
            {
                Assert.That(dto.Name, Is.EqualTo("After"));
                Assert.That(dto.Samples, Is.EqualTo(32));
                Assert.That(dto.Enabled, Is.False);
            });
        }
        finally
        {
            window.Close();
            styles.Remove(panelStyle);
            styles.Remove(fluent);
        }
    }
}
