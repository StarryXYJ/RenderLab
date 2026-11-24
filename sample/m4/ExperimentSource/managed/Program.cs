using RenderLab.Project;

if (args.Length != 1) { throw new ArgumentException("Provide the output pipeline catalog path."); }
var pipelines = new PipelineCatalog();
pipelines.AddForward("example.managed-blue", "example.clear");
pipelines.Save(args[0]);
