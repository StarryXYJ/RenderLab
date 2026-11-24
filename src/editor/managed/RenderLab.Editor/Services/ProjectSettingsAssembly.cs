using System.Reflection;
using System.Runtime.Loader;
using RenderLab.Project;

namespace RenderLab.Editor.Services;

/// <summary>Owns a project settings assembly and its collectible load context.</summary>
internal sealed class ProjectSettingsAssembly : IDisposable
{
    private ProjectLoadContext? context;
    internal IProjectSettingsSchema? Schema { get; private set; }

    private ProjectSettingsAssembly(ProjectLoadContext context, IProjectSettingsSchema schema)
    {
        this.context = context;
        Schema = schema;
    }

    internal static ProjectSettingsAssembly Load(string path, string entryType)
    {
        var fullPath = Path.GetFullPath(path);
        var context = new ProjectLoadContext(fullPath);
        try
        {
            using var stream = File.OpenRead(fullPath);
            var assembly = context.LoadFromStream(stream);
            var type = assembly.GetType(entryType, throwOnError: true)!;
            if (type.IsAbstract || !typeof(IProjectSettingsDefinition).IsAssignableFrom(type) ||
                Activator.CreateInstance(type) is not IProjectSettingsDefinition definition)
            { throw new InvalidDataException("Invalid project settings entry type."); }
            var schema = definition.CreateSchema() ?? throw new InvalidDataException("Project settings schema is missing.");
            var settings = schema.CreateDefaults();
            if (!schema.SettingsType.IsInstanceOfType(settings))
            { throw new InvalidDataException("Project settings schema returned an invalid DTO."); }
            schema.Serialize(settings);
            return new ProjectSettingsAssembly(context, schema);
        }
        catch
        {
            context.Unload();
            throw;
        }
    }

    public void Dispose()
    {
        Schema = null;
        context?.Unload();
        context = null;
    }

    private sealed class ProjectLoadContext(string assemblyPath) : AssemblyLoadContext(isCollectible: true)
    {
        private readonly AssemblyDependencyResolver resolver = new(assemblyPath);
        private readonly string directory = Path.GetDirectoryName(assemblyPath)!;

        protected override Assembly? Load(AssemblyName name)
        {
            var id = name.Name ?? "";
            if (id == "RenderLab.Project" || id.StartsWith("AutoSettingUI.", StringComparison.Ordinal) ||
                id == "Avalonia" || id.StartsWith("Avalonia.", StringComparison.Ordinal))
            { return null; }
            var path = resolver.ResolveAssemblyToPath(name) ?? Path.Combine(directory, id + ".dll");
            if (!File.Exists(path)) { return null; }
            using var stream = File.OpenRead(path);
            return LoadFromStream(stream);
        }
    }
}
