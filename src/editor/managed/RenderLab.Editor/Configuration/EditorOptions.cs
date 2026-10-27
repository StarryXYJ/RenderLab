namespace RenderLab.Editor.Configuration;

public sealed record EditorOptions(string Backend, string Project, string Shaders, string Output, bool Smoke,
    bool Validation, bool Profiling, uint Pipeline, double BenchmarkSeconds, double WarmupSeconds)
{
    internal static EditorOptions Parse(string[] args)
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
        bool? validation = null;
        var profiling = true;
        uint pipeline = 2;
        double benchmarkSeconds = 0, warmupSeconds = 2;
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
                case "--validation": validation = ParseSwitch(args[i]); break;
                case "--profile": profiling = ParseSwitch(args[i]); break;
                case "--pipeline":
                    pipeline = args[i] switch
                    {
                        "forward" => 1,
                        "deferred" => 2,
                        "raytracing" => 3,
                        _ => throw new ArgumentException("Pipeline must be forward/deferred/raytracing.")
                    }; break;
                case "--benchmark-seconds": benchmarkSeconds = double.Parse(args[i], System.Globalization.CultureInfo.InvariantCulture); break;
                case "--warmup-seconds": warmupSeconds = double.Parse(args[i], System.Globalization.CultureInfo.InvariantCulture); break;
                default: throw new ArgumentException($"Unknown argument: {key}");
            }
        }
        if (backend is not ("dx12" or "vulkan") || !File.Exists(project) || !Directory.Exists(shaders) ||
            !double.IsFinite(benchmarkSeconds) || benchmarkSeconds < 0 || benchmarkSeconds > 120 ||
            !double.IsFinite(warmupSeconds) || warmupSeconds < 0 || warmupSeconds > 30 ||
            (smoke && benchmarkSeconds > 0))
        {
            throw new ArgumentException("Select dx12/vulkan and existing project/shader paths.");
        }
        output ??= Path.Combine(root?.FullName ?? Environment.CurrentDirectory, "out/m7", backend);
#if DEBUG
        validation ??= true;
#else
        validation ??= smoke;
#endif
        return new EditorOptions(backend, Path.GetFullPath(project), Path.GetFullPath(shaders), Path.GetFullPath(output), smoke,
            validation.Value, profiling, pipeline, benchmarkSeconds, warmupSeconds);
    }

    private static bool ParseSwitch(string value) => value switch
    {
        "on" => true,
        "off" => false,
        _ => throw new ArgumentException("Expected on/off.")
    };
}
