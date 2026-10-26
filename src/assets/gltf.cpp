#include "assets/json_io.hpp"
#include "assets/model.hpp"
#include "core/hash.hpp"

#include <Windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <set>
#include <span>
#include <stdexcept>

namespace renderlab::assets
{
namespace
{
using Json = nlohmann::json;
void Require(bool value, const char* message)
{
    if (!value)
        throw std::invalid_argument(message);
}
uint32_t Word(const std::vector<std::byte>& data, size_t offset)
{
    Require(offset <= data.size() && data.size() - offset >= 4, "Truncated GLB");
    uint32_t result;
    std::memcpy(&result, data.data() + offset, 4);
    return result;
}
struct Document
{
    Database& db;
    std::string id;
    Json json;
    std::vector<std::vector<std::byte>> buffers, encodedImages;
    std::vector<std::byte> binary;
    std::string key;
    std::vector<std::byte> Uri(const std::string& uri)
    {
        if (uri.starts_with("data:"))
        {
            const auto marker = uri.find(";base64,");
            Require(marker != std::string::npos, "Only base64 data URIs are supported");
            const auto value = uri.substr(marker + 8);
            DWORD size = 0;
            Require(value.size() < UINT32_MAX, "Data URI exceeds size limit");
            Require(CryptStringToBinaryA(value.data(), static_cast<DWORD>(value.size()),
                                         CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, nullptr, &size,
                                         nullptr, nullptr) != 0,
                    "Invalid base64 URI");
            std::vector<std::byte> bytes(size);
            Require(CryptStringToBinaryA(value.data(), static_cast<DWORD>(value.size()),
                                         CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
                                         reinterpret_cast<BYTE*>(bytes.data()), &size, nullptr,
                                         nullptr) != 0,
                    "Invalid base64 URI");
            return bytes;
        }
        return detail::Read(db.Get(db.Dependency(id, uri)).path);
    }
    std::span<const std::byte> View(size_t index) const
    {
        const auto& view = json.at("bufferViews").at(index);
        const auto& buffer = buffers.at(view.at("buffer").get<size_t>());
        const auto offset = view.value("byteOffset", size_t{0}),
                   length = view.at("byteLength").get<size_t>();
        Require(offset <= buffer.size() && length <= buffer.size() - offset,
                "glTF bufferView exceeds buffer");
        return std::span(buffer).subspan(offset, length);
    }
};
struct Accessor
{
    std::span<const std::byte> data;
    size_t count, stride, bytes;
    uint32_t component;
    Accessor(const Document& d, size_t index, const std::string& type)
        : count(0), stride(0), bytes(0), component(0)
    {
        const auto& a = d.json.at("accessors").at(index);
        Require(!a.contains("sparse") && a.contains("bufferView"),
                "Sparse/implicit glTF accessors are unsupported");
        Require(a.at("type") == type && !a.value("normalized", false),
                "Unsupported glTF accessor type/normalization");
        component = a.at("componentType");
        bytes = component == 5121                        ? 1
                : component == 5123                      ? 2
                : component == 5125 || component == 5126 ? 4
                                                         : 0;
        Require(bytes != 0, "Unsupported glTF component type");
        const size_t columns = type == "VEC3" ? 3 : type == "VEC2" ? 2 : 1;
        const auto viewIndex = a.at("bufferView").get<size_t>();
        const auto view = d.View(viewIndex);
        const auto offset = a.value("byteOffset", size_t{0});
        stride = d.json.at("bufferViews").at(viewIndex).value("byteStride", bytes * columns);
        count = a.at("count");
        Require(count > 0 && count <= 4000000, "glTF accessor count outside limit");
        Require(stride >= bytes * columns && stride % bytes == 0 && offset % bytes == 0,
                "Invalid glTF accessor alignment/stride");
        Require(offset <= view.size() && (count - 1) <= ((view.size() - offset) / stride),
                "glTF accessor exceeds view");
        Require((count - 1) * stride + bytes * columns <= view.size() - offset,
                "Truncated glTF accessor");
        data = view.subspan(offset);
    }
    float Float(size_t index, size_t column) const
    {
        Require(component == 5126, "Vertex attributes currently require float32");
        float result;
        std::memcpy(&result, data.data() + index * stride + column * 4, 4);
        Require(std::isfinite(result), "Nonfinite glTF vertex");
        return result;
    }
    uint32_t Index(size_t index) const
    {
        Require(component != 5126, "glTF indices must be unsigned integers");
        uint32_t result = 0;
        std::memcpy(&result, data.data() + index * stride, bytes);
        return result;
    }
};
Json Encode(const Model& model)
{
    Json j{{"version", 2},
           {"id", model.id},
           {"key", model.key},
           {"meshes", Json::array()},
           {"materials", Json::array()},
           {"images", Json::array()},
           {"instances", Json::array()}};
    for (const auto& m : model.meshes)
    {
        std::vector<float> vertices;
        for (const auto& v : m.vertices)
            vertices.insert(vertices.end(), {v.position.x, v.position.y, v.position.z, v.normal.x,
                                             v.normal.y, v.normal.z, v.uv[0], v.uv[1]});
        j["meshes"].push_back({{"id", m.id},
                               {"name", m.name},
                               {"vertices", vertices},
                               {"indices", m.indices},
                               {"material", m.material}});
    }
    for (const auto& m : model.materials)
        j["materials"].push_back({{"id", m.id},
                                  {"name", m.name},
                                  {"color", m.color},
                                  {"image", m.image},
                                  {"doubleSided", m.doubleSided},
                                  {"metallic", m.metallic},
                                  {"roughness", m.roughness},
                                  {"normalScale", m.normalScale},
                                  {"occlusionStrength", m.occlusionStrength},
                                  {"emission", m.emission},
                                  {"metallicRoughnessImage", m.metallicRoughnessImage},
                                  {"normalImage", m.normalImage},
                                  {"occlusionImage", m.occlusionImage},
                                  {"emissionImage", m.emissionImage}});
    for (const auto& image : model.images)
        j["images"].push_back(
            {{"width", image.width}, {"height", image.height}, {"rgba", Json::binary(image.rgba)}});
    for (const auto& instance : model.instances)
        j["instances"].push_back({{"id", instance.id},
                                  {"mesh", instance.mesh},
                                  {"transform", instance.transform.values}});
    return j;
}
void BoundsFor(Mesh& mesh)
{
    mesh.bounds = {{INFINITY, INFINITY, INFINITY}, {-INFINITY, -INFINITY, -INFINITY}};
    for (const auto& v : mesh.vertices)
    {
        const auto p = v.position;
        mesh.bounds.minimum = {std::min(mesh.bounds.minimum.x, p.x),
                               std::min(mesh.bounds.minimum.y, p.y),
                               std::min(mesh.bounds.minimum.z, p.z)};
        mesh.bounds.maximum = {std::max(mesh.bounds.maximum.x, p.x),
                               std::max(mesh.bounds.maximum.y, p.y),
                               std::max(mesh.bounds.maximum.z, p.z)};
    }
}
Model Decode(const Json& j)
{
    Require(j.at("version") == 2, "Unsupported model cache");
    Model result;
    result.id = j.at("id");
    result.key = j.at("key");
    result.cacheHit = true;
    for (const auto& m : j.at("meshes"))
    {
        Mesh mesh;
        mesh.id = m.at("id");
        mesh.name = m.at("name");
        mesh.material = m.at("material");
        const auto vertices = m.at("vertices").get<std::vector<float>>();
        Require(!vertices.empty() && vertices.size() % 8 == 0, "Corrupt model cache");
        for (size_t i = 0; i < vertices.size(); i += 8)
            mesh.vertices.push_back({{vertices[i], vertices[i + 1], vertices[i + 2]},
                                     {vertices[i + 3], vertices[i + 4], vertices[i + 5]},
                                     {vertices[i + 6], vertices[i + 7]}});
        mesh.indices = m.at("indices").get<std::vector<uint32_t>>();
        BoundsFor(mesh);
        result.meshes.push_back(std::move(mesh));
    }
    for (const auto& m : j.at("materials"))
        result.materials.push_back(
            {m.at("id"), m.at("name"), m.at("color").get<std::array<float, 4>>(), m.at("image"),
             m.at("doubleSided"), m.at("metallic"), m.at("roughness"), m.at("normalScale"),
             m.at("occlusionStrength"), m.at("emission").get<std::array<float, 3>>(),
             m.at("metallicRoughnessImage"), m.at("normalImage"), m.at("occlusionImage"),
             m.at("emissionImage")});
    for (const auto& image : j.at("images"))
    {
        Image value;
        value.width = image.at("width");
        value.height = image.at("height");
        Require(value.width > 0 && value.height > 0 && value.width <= 8192 && value.height <= 8192,
                "Invalid cached image extent");
        value.rgba = image.at("rgba").get_binary();
        Require(value.rgba.size() == size_t(value.width) * value.height * 4, "Corrupt image cache");
        result.images.push_back(std::move(value));
    }
    for (const auto& i : j.at("instances"))
        result.instances.push_back(
            {i.at("id"), i.at("mesh"), math::Mat4{i.at("transform").get<std::array<float, 16>>()}});
    Require(!result.meshes.empty() && !result.materials.empty(), "Empty model cache");
    for (const auto& mesh : result.meshes)
    {
        Require(project::ValidId(mesh.id) && mesh.material < result.materials.size() &&
                    !mesh.indices.empty() && mesh.indices.size() % 3 == 0,
                "Invalid cached mesh");
        for (const auto index : mesh.indices)
            Require(index < mesh.vertices.size(), "Invalid cached index");
        for (const auto& vertex : mesh.vertices)
        {
            const auto p = vertex.position, n = vertex.normal;
            Require(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
                        std::isfinite(n.x) && std::isfinite(n.y) && std::isfinite(n.z) &&
                        math::Length(n) > 1e-6f && std::isfinite(vertex.uv[0]) &&
                        std::isfinite(vertex.uv[1]),
                    "Invalid cached vertex");
        }
    }
    for (const auto& material : result.materials)
    {
        Require(project::ValidId(material.id), "Invalid cached material");
        for (const auto image :
             {material.image, material.metallicRoughnessImage, material.normalImage,
              material.occlusionImage, material.emissionImage})
            Require(image >= -1 && (image < 0 || size_t(image) < result.images.size()),
                    "Invalid cached material image");
        const auto unit = [](float value)
        { return std::isfinite(value) && value >= 0 && value <= 1; };
        Require(unit(material.metallic) && unit(material.roughness) &&
                    unit(material.occlusionStrength) && std::isfinite(material.normalScale) &&
                    material.normalScale >= 0 && material.normalScale <= 2,
                "Invalid cached PBR material");
        for (float value : material.emission)
            Require(unit(value), "Invalid cached material emission");
        for (float value : material.color)
            Require(std::isfinite(value) && value >= 0 && value <= 1,
                    "Invalid cached material color");
    }
    for (const auto& instance : result.instances)
    {
        Require(project::ValidId(instance.id) && instance.mesh < result.meshes.size(),
                "Invalid cached node");
        math::Inverse(instance.transform);
    }
    return result;
}
std::string StableKey(const Json& array, size_t index, const std::string& prefix)
{
    const auto name = array.at(index).value("name", std::string{});
    if (!name.empty())
    {
        size_t matches = 0;
        for (const auto& item : array)
            if (item.value("name", std::string{}) == name)
                ++matches;
        if (matches == 1)
            return prefix + name;
    }
    return prefix + std::to_string(index);
}
} // namespace
Model LoadModel(Database& database, const std::string& id)
{
    Document d{database, id};
    const auto source = detail::Read(database.Get(id).path);
    if (source.size() >= 12 && Word(source, 0) == 0x46546c67)
    {
        Require(Word(source, 4) == 2 && Word(source, 8) == source.size(), "Invalid GLB header");
        size_t offset = 12;
        bool first = true;
        while (offset < source.size())
        {
            const auto length = Word(source, offset), type = Word(source, offset + 4);
            offset += 8;
            Require(length % 4 == 0 && length <= source.size() - offset, "Invalid GLB chunk");
            if (first)
            {
                Require(type == 0x4e4f534a, "GLB must start with JSON");
                d.json = Json::parse(source.begin() + offset, source.begin() + offset + length);
            }
            else if (type == 0x004e4942)
            {
                Require(d.binary.empty(), "Multiple GLB BIN chunks");
                d.binary.assign(source.begin() + offset, source.begin() + offset + length);
            }
            offset += length;
            first = false;
        }
    }
    else
        d.json = Json::parse(source.begin(), source.end());
    Require(d.json.at("asset").at("version") == "2.0", "Only glTF 2.0 is supported");
    Require(d.json.value("extensionsRequired", Json::array()).empty(),
            "Required glTF extensions are unsupported");
    Require(d.json.value("animations", Json::array()).empty() &&
                d.json.value("skins", Json::array()).empty(),
            "Animation/skinning is not supported");
    std::string hashes = "renderlab-static-gltf-v2:" + Sha256(source);
    for (const auto& buffer : d.json.value("buffers", Json::array()))
    {
        auto bytes = buffer.contains("uri") ? d.Uri(buffer.at("uri")) : d.binary;
        const auto length = buffer.at("byteLength").get<size_t>();
        Require(length <= bytes.size(), "Truncated glTF buffer");
        bytes.resize(length);
        hashes += Sha256(bytes);
        d.buffers.push_back(std::move(bytes));
    }
    for (const auto& image : d.json.value("images", Json::array()))
    {
        std::vector<std::byte> bytes;
        if (image.contains("uri"))
            bytes = d.Uri(image.at("uri"));
        else
        {
            const auto view = d.View(image.at("bufferView"));
            bytes.assign(view.begin(), view.end());
        }
        hashes += Sha256(bytes);
        d.encodedImages.push_back(std::move(bytes));
    }
    d.key = Sha256(hashes);
    const auto cache = database.GetProject().library / "Models" / (id + "-" + d.key + ".cbor");
    if (std::filesystem::exists(cache))
    {
        try
        {
            auto result = Decode(Json::from_cbor(detail::Read(cache)));
            std::set<std::string> ids;
            for (const auto& [name, child] : database.Get(id).subresources)
            {
                (void)name;
                ids.insert(child);
            }
            bool valid = result.id == id && result.key == d.key;
            for (const auto& mesh : result.meshes)
                valid = valid && ids.contains(mesh.id);
            for (const auto& material : result.materials)
                valid = valid && ids.contains(material.id);
            for (const auto& instance : result.instances)
                valid = valid && ids.contains(instance.id);
            if (valid)
                return result;
        }
        catch (const std::exception&)
        { /* Rebuild disposable corrupt/old cache. */
        }
    }
    Model result;
    result.id = id;
    result.key = d.key;
    for (const auto& image : d.encodedImages)
        result.images.push_back(DecodeImage(image));
    const auto materials = d.json.value("materials", Json::array());
    for (size_t i = 0; i < materials.size(); ++i)
    {
        const auto& material = materials[i];
        Require(material.value("alphaMode", "OPAQUE") == "OPAQUE",
                "Only opaque glTF materials are supported in M4.2");
        Require(material.value("extensions", Json::object()).empty(),
                "glTF material extensions are unsupported");
        Surface surface;
        surface.id = database.Subresource(id, StableKey(materials, i, "material:"));
        surface.name = material.value("name", "Material");
        surface.doubleSided = material.value("doubleSided", false);
        const auto pbr = material.value("pbrMetallicRoughness", Json::object());
        if (pbr.contains("baseColorFactor"))
            surface.color = pbr.at("baseColorFactor").get<std::array<float, 4>>();
        const auto imageFor = [&](const Json& map)
        {
            Require(map.value("texCoord", 0) == 0, "Only TEXCOORD_0 is supported");
            Require(!map.contains("extensions"), "Texture transforms are unsupported");
            const auto& texture = d.json.at("textures").at(map.at("index").get<size_t>());
            if (texture.contains("sampler"))
            {
                const auto& sampler = d.json.at("samplers").at(texture.at("sampler").get<size_t>());
                Require(sampler.value("magFilter", 9728) == 9728 &&
                            sampler.value("minFilter", 9728) == 9728 &&
                            sampler.value("wrapS", 33071) == 33071 &&
                            sampler.value("wrapT", 33071) == 33071,
                        "Imported textures support point-clamp samplers only");
            }
            const auto image = texture.at("source").get<int>();
            Require(image >= 0 && size_t(image) < result.images.size(), "Invalid material image");
            return image;
        };
        if (pbr.contains("baseColorTexture"))
            surface.image = imageFor(pbr.at("baseColorTexture"));
        surface.metallic = pbr.value("metallicFactor", 1.f);
        surface.roughness = pbr.value("roughnessFactor", 1.f);
        if (pbr.contains("metallicRoughnessTexture"))
            surface.metallicRoughnessImage = imageFor(pbr.at("metallicRoughnessTexture"));
        if (material.contains("normalTexture"))
        {
            surface.normalImage = imageFor(material.at("normalTexture"));
            surface.normalScale = material.at("normalTexture").value("scale", 1.f);
        }
        if (material.contains("occlusionTexture"))
        {
            surface.occlusionImage = imageFor(material.at("occlusionTexture"));
            surface.occlusionStrength = material.at("occlusionTexture").value("strength", 1.f);
        }
        if (material.contains("emissiveTexture"))
            surface.emissionImage = imageFor(material.at("emissiveTexture"));
        surface.emission = material.value("emissiveFactor", std::array<float, 3>{0, 0, 0});
        const auto unit = [](float value)
        { return std::isfinite(value) && value >= 0 && value <= 1; };
        Require(unit(surface.metallic) && unit(surface.roughness) &&
                    unit(surface.occlusionStrength) && std::isfinite(surface.normalScale) &&
                    surface.normalScale >= 0 && surface.normalScale <= 2,
                "Invalid glTF metallic/roughness/normal/occlusion factor");
        for (const auto value : surface.emission)
            Require(unit(value), "Invalid glTF emission factor");
        for (float value : surface.color)
            Require(std::isfinite(value) && value >= 0 && value <= 1,
                    "Invalid glTF base color factor");
        result.materials.push_back(std::move(surface));
    }
    const auto defaultMaterial = static_cast<uint32_t>(result.materials.size());
    result.materials.push_back({database.Subresource(id, "material:default"), "Default"});
    const auto meshes = d.json.at("meshes");
    std::vector<std::vector<uint32_t>> meshPrimitives;
    for (size_t i = 0; i < meshes.size(); ++i)
    {
        meshPrimitives.emplace_back();
        const auto& primitives = meshes[i].at("primitives");
        for (size_t j = 0; j < primitives.size(); ++j)
        {
            const auto& primitive = primitives[j];
            Require(primitive.value("mode", 4) == 4, "Only triangle glTF primitives are supported");
            Require(!primitive.contains("targets") && !primitive.contains("extensions"),
                    "Morph/compressed primitives are unsupported");
            const auto& attributes = primitive.at("attributes");
            Accessor positions(d, attributes.at("POSITION"), "VEC3");
            Mesh mesh;
            mesh.id = database.Subresource(id, StableKey(meshes, i, "mesh:") +
                                                   "/primitive:" + std::to_string(j));
            mesh.name = meshes[i].value("name", "Mesh");
            mesh.material = primitive.value("material", defaultMaterial);
            Require(mesh.material < result.materials.size(), "Invalid glTF material index");
            mesh.vertices.resize(positions.count);
            for (size_t v = 0; v < positions.count; ++v)
                mesh.vertices[v].position = {positions.Float(v, 0), positions.Float(v, 1),
                                             positions.Float(v, 2)};
            if (attributes.contains("NORMAL"))
            {
                Accessor normals(d, attributes.at("NORMAL"), "VEC3");
                Require(normals.count == positions.count, "Normal count mismatch");
                for (size_t v = 0; v < positions.count; ++v)
                    mesh.vertices[v].normal = math::Normalize(
                        {normals.Float(v, 0), normals.Float(v, 1), normals.Float(v, 2)});
            }
            if (attributes.contains("TEXCOORD_0"))
            {
                Accessor uv(d, attributes.at("TEXCOORD_0"), "VEC2");
                Require(uv.count == positions.count, "UV count mismatch");
                for (size_t v = 0; v < positions.count; ++v)
                    mesh.vertices[v].uv = {uv.Float(v, 0), uv.Float(v, 1)};
            }
            if (primitive.contains("indices"))
            {
                Accessor indices(d, primitive.at("indices"), "SCALAR");
                for (size_t k = 0; k < indices.count; ++k)
                {
                    auto v = indices.Index(k);
                    Require(v < positions.count, "glTF index exceeds vertex count");
                    mesh.indices.push_back(v);
                }
            }
            else
                for (uint32_t v = 0; v < positions.count; ++v)
                    mesh.indices.push_back(v);
            Require(mesh.indices.size() % 3 == 0,
                    "Triangle index count must be divisible by three");
            if (!attributes.contains("NORMAL"))
            {
                for (size_t k = 0; k < mesh.indices.size(); k += 3)
                {
                    auto& a = mesh.vertices[mesh.indices[k]];
                    auto& b = mesh.vertices[mesh.indices[k + 1]];
                    auto& c = mesh.vertices[mesh.indices[k + 2]];
                    auto n = math::Cross(b.position - a.position, c.position - a.position);
                    a.normal = a.normal + n;
                    b.normal = b.normal + n;
                    c.normal = c.normal + n;
                }
                for (auto& v : mesh.vertices)
                    v.normal = math::Normalize(v.normal);
            }
            BoundsFor(mesh);
            meshPrimitives.back().push_back(static_cast<uint32_t>(result.meshes.size()));
            result.meshes.push_back(std::move(mesh));
        }
    }
    const auto nodes = d.json.value("nodes", Json::array());
    std::vector<bool> visited(nodes.size()), active(nodes.size());
    std::function<void(size_t, const math::Mat4&)> visit =
        [&](size_t index, const math::Mat4& parent)
    {
        Require(index < nodes.size() && !active[index] && !visited[index],
                "Invalid/cyclic/shared glTF node");
        active[index] = true;
        visited[index] = true;
        const auto& node = nodes[index];
        math::Mat4 local;
        Require(!node.contains("skin") && !node.contains("weights"),
                "Skinned/morphed nodes are unsupported");
        if (node.contains("matrix"))
        {
            Require(!node.contains("translation") && !node.contains("rotation") &&
                        !node.contains("scale"),
                    "glTF matrix and TRS are mutually exclusive");
            const auto values = node.at("matrix").get<std::array<float, 16>>();
            for (size_t r = 0; r < 4; ++r)
                for (size_t c = 0; c < 4; ++c)
                    local.At(r, c) = values[c * 4 + r];
            math::Inverse(local);
        }
        else
        {
            const auto t = node.value("translation", std::array<float, 3>{0, 0, 0});
            const auto q = node.value("rotation", std::array<float, 4>{0, 0, 0, 1});
            const auto scale = node.value("scale", std::array<float, 3>{1, 1, 1});
            local = math::Compose({t[0], t[1], t[2]}, {q[0], q[1], q[2], q[3]},
                                  {scale[0], scale[1], scale[2]});
        }
        const auto world = parent * local;
        if (node.contains("mesh"))
            for (const auto mesh : meshPrimitives.at(node.at("mesh").get<size_t>()))
                result.instances.push_back(
                    {database.Subresource(id, StableKey(nodes, index, "node:")), mesh, world});
        for (const auto& child : node.value("children", Json::array()))
            visit(child.get<size_t>(), world);
        active[index] = false;
    };
    if (d.json.contains("scenes"))
        for (const auto& root :
             d.json.at("scenes").at(d.json.value("scene", size_t{0})).value("nodes", Json::array()))
            visit(root.get<size_t>(), math::Mat4::Identity());
    else
    {
        std::set<size_t> children;
        for (const auto& node : nodes)
            for (const auto& child : node.value("children", Json::array()))
                children.insert(child.get<size_t>());
        for (size_t i = 0; i < nodes.size(); ++i)
            if (!children.contains(i))
                visit(i, math::Mat4::Identity());
        Require(std::ranges::all_of(visited, [](bool v) { return v; }),
                "Unreachable/cyclic glTF nodes");
    }
    if (nodes.empty() && !d.json.contains("scenes"))
        for (uint32_t i = 0; i < result.meshes.size(); ++i)
            result.instances.push_back(
                {database.Subresource(id, "node:implicit:" + result.meshes[i].id), i,
                 math::Mat4::Identity()});
    Require(!result.meshes.empty(), "glTF contains no supported mesh");
    std::filesystem::create_directories(cache.parent_path());
    const auto bytes = Json::to_cbor(Encode(result));
    WriteFileAtomically(cache, std::as_bytes(std::span{bytes}));
    return result;
}
} // namespace renderlab::assets
