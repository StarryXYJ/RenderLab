using System.Reflection;
using System.Text;
using System.Text.Json;

namespace RenderLab.Project;

/// <summary>Validates and layers project DTO values without modifying the source object.</summary>
public sealed class SettingsSchema<T> : IProjectSettingsSchema where T : class, new()
{
    private const int MaxBytes = 65536;
    private readonly SortedDictionary<string, Field> fields = new(StringComparer.Ordinal);
    public int Version { get; }
    public Type SettingsType => typeof(T);

    object IProjectSettingsSchema.CreateDefaults() => CreateDefaults();
    object IProjectSettingsSchema.Apply(object baseline, string json) => Apply((T)baseline, json);
    string IProjectSettingsSchema.Serialize(object settings) => Serialize((T)settings);

    public SettingsSchema(int version)
    {
        if (version < 1) { throw new ArgumentOutOfRangeException(nameof(version)); }
        Version = version;
        foreach (var property in typeof(T).GetProperties(BindingFlags.Instance | BindingFlags.Public))
        {
            var attribute = property.GetCustomAttribute<ProjectSettingAttribute>();
            if (attribute is null) { continue; }
            ValidateId(attribute.Id);
            if (property.GetIndexParameters().Length != 0 || property.GetMethod?.IsPublic != true ||
                property.SetMethod?.IsPublic != true ||
                property.PropertyType != typeof(bool) && property.PropertyType != typeof(int) &&
                property.PropertyType != typeof(float) && property.PropertyType != typeof(double) &&
                property.PropertyType != typeof(string))
            { throw new InvalidOperationException($"Unsupported project setting property: {property.Name}."); }
            if (double.IsNaN(attribute.Minimum) || double.IsNaN(attribute.Maximum) ||
                attribute.Minimum > attribute.Maximum)
            { throw new InvalidOperationException($"Invalid limits for project setting: {attribute.Id}."); }
            if (!fields.TryAdd(attribute.Id, new Field(property, attribute)))
            { throw new InvalidOperationException($"Duplicate project setting ID: {attribute.Id}."); }
        }
        if (fields.Count is 0 or > 256) { throw new InvalidOperationException("Expected 1 to 256 project settings."); }
        Validate(new T());
    }

    /// <summary>Returns a fresh DTO with its declared defaults.</summary>
    public T CreateDefaults()
    {
        var result = new T();
        Validate(result);
        return result;
    }

    /// <summary>Serializes all registered values with stable IDs and a schema version.</summary>
    public string Serialize(T settings)
    {
        ArgumentNullException.ThrowIfNull(settings);
        Validate(settings);
        var values = new SortedDictionary<string, object?>(StringComparer.Ordinal);
        foreach (var (id, field) in fields) { values.Add(id, field.Property.GetValue(settings)); }
        var json = JsonSerializer.Serialize(new { version = Version, values }, new JsonSerializerOptions { WriteIndented = true }) + "\n";
        if (Encoding.UTF8.GetByteCount(json) > MaxBytes) { throw new InvalidDataException("Project settings exceed budget."); }
        return json;
    }

    /// <summary>Applies an override document to a copy of the supplied values.</summary>
    public T Apply(T baseline, string json)
    {
        ArgumentNullException.ThrowIfNull(baseline);
        ArgumentNullException.ThrowIfNull(json);
        if (Encoding.UTF8.GetByteCount(json) > MaxBytes) { throw new InvalidDataException("Project settings exceed budget."); }
        Validate(baseline);
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        if (root.ValueKind != JsonValueKind.Object || !root.TryGetProperty("version", out var version) ||
            version.ValueKind != JsonValueKind.Number || !version.TryGetInt32(out var number) || number != Version ||
            !root.TryGetProperty("values", out var values) || values.ValueKind != JsonValueKind.Object)
        { throw new InvalidDataException("Unsupported project settings document."); }
        var rootNames = new HashSet<string>(StringComparer.Ordinal);
        foreach (var entry in root.EnumerateObject())
        {
            if (entry.Name is not ("version" or "values") || !rootNames.Add(entry.Name))
            { throw new InvalidDataException("Duplicate or unknown project settings document field."); }
        }
        var result = new T();
        foreach (var field in fields.Values) { field.Property.SetValue(result, field.Property.GetValue(baseline)); }
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var entry in values.EnumerateObject())
        {
            if (!seen.Add(entry.Name) || !fields.TryGetValue(entry.Name, out var field))
            { throw new InvalidDataException($"Duplicate or unknown project setting: {entry.Name}."); }
            var type = field.Property.PropertyType;
            object? value;
            try
            {
                value = type == typeof(bool) ? entry.Value.GetBoolean()
                    : type == typeof(int) ? entry.Value.GetInt32()
                    : type == typeof(float) ? entry.Value.GetSingle()
                    : type == typeof(double) ? entry.Value.GetDouble()
                    : entry.Value.GetString();
            }
            catch (Exception error) when (error is InvalidOperationException or FormatException or OverflowException)
            { throw new InvalidDataException($"Invalid value for project setting: {entry.Name}.", error); }
            field.Property.SetValue(result, value);
        }
        Validate(result);
        return result;
    }

    private void Validate(T settings)
    {
        foreach (var (id, field) in fields)
        {
            var value = field.Property.GetValue(settings);
            if (value is null) { throw new InvalidDataException($"Null project setting: {id}."); }
            if (value is int or float or double)
            {
                var number = Convert.ToDouble(value, System.Globalization.CultureInfo.InvariantCulture);
                if (!double.IsFinite(number) || number < field.Attribute.Minimum || number > field.Attribute.Maximum)
                { throw new InvalidDataException($"Project setting outside range: {id}."); }
            }
        }
        if (settings is IProjectSettingsValidator validator) { validator.Validate(); }
    }

    private static void ValidateId(string? id)
    {
        if (id is not { Length: > 0 and <= 128 } || id.Any(c =>
            !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c is '.' or '-' or '_')))
        { throw new InvalidOperationException("Invalid project setting ID."); }
    }

    private sealed record Field(PropertyInfo Property, ProjectSettingAttribute Attribute);
}
