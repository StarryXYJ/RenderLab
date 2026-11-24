using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using NUnit.Framework;
using RenderLab.Editor.Services;

namespace RenderLab.Editor.Tests;

public class ProjectSettingsAssemblyTests
{
    [Test]
    public void LoadsAndUnloadsProjectSettingsWithoutLockingTheAssembly()
    {
        var path = Path.Combine(AppContext.BaseDirectory, "RenderLab.Project.Fixture.dll");
        var weak = LoadAndRelease(path);
        for (var i = 0; i < 10 && weak.IsAlive; ++i)
        {
            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();
        }
        Assert.That(weak.IsAlive, Is.False, "Project load context must not outlive its session.");
        using var writable = new FileStream(path, FileMode.Open, FileAccess.ReadWrite, FileShare.None);
    }

    [Test]
    public void RejectsMissingOrInvalidEntryType()
    {
        var path = Path.Combine(AppContext.BaseDirectory, "RenderLab.Project.Fixture.dll");
        Assert.Throws<TypeLoadException>(() => ProjectSettingsAssembly.Load(path, "Missing.Type"));
        Assert.Throws<InvalidDataException>(() => ProjectSettingsAssembly.Load(path, "RenderLab.Project.Fixture.FixtureSettings"));
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference LoadAndRelease(string path)
    {
        using var project = ProjectSettingsAssembly.Load(path, "RenderLab.Project.Fixture.FixtureDefinition");
        var schema = project.Schema!;
        var original = schema.CreateDefaults();
        var updated = schema.Apply(original, "{\"version\":1,\"values\":{\"fixture.count\":7}}");
        Assert.That(schema.Serialize(original), Does.Contain("\"fixture.count\": 3"));
        Assert.That(schema.Serialize(updated), Does.Contain("\"fixture.count\": 7"));
        var context = AssemblyLoadContext.GetLoadContext(schema.SettingsType.Assembly)!;
        Assert.That(context.IsCollectible, Is.True);
        return new WeakReference(context);
    }
}
