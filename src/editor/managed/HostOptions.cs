namespace RenderLab.Editor.Host;

public sealed record HostOptions(string Backend, string Project, string Shaders, string Output, bool Smoke)
{
    internal static HostOptions Parse(string[] args)
    {
        var root = new DirectoryInfo(AppContext.BaseDirectory);
        while (root != null && !File.Exists(Path.Combine(root.FullName, "sample/m4/RayTracing.rlproject")))
        {
            root = root.Parent;
        }
        var backend = "dx12";
        var project = root == null ? "" : Path.Combine(root.FullName, "sample/m4/RayTracing.rlproject");
        var shaders = Path.Combine(AppContext.BaseDirectory, "shaders");
        string? output = null;
        var smoke = false;
        for (var i = 0; i < args.Length; ++i)
        {
            var key = args[i];
            if (key == "--smoke")
            {
                smoke = true;
                continue;
            }
            if (++i >= args.Length)
            {
                throw new ArgumentException($"Missing value for {key}");
            }
            switch (key)
            {
                case "--backend": backend = args[i]; break;
                case "--project": project = args[i]; break;
                case "--shaders": shaders = args[i]; break;
                case "--output": output = args[i]; break;
                default: throw new ArgumentException($"Unknown argument: {key}");
            }
        }
        if (backend is not ("dx12" or "vulkan") || !File.Exists(project) || !Directory.Exists(shaders))
        {
            throw new ArgumentException("Select dx12/vulkan and existing project/shader paths.");
        }
        output ??= Path.Combine(root?.FullName ?? Environment.CurrentDirectory, "out/m7", backend);
        return new HostOptions(backend, Path.GetFullPath(project), Path.GetFullPath(shaders), Path.GetFullPath(output), smoke);
    }
}
