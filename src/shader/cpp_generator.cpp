#include "shader/package.hpp"

#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

namespace renderlab::shader
{
namespace
{
std::string Limit(double value)
{
    std::ostringstream out;
    out << std::setprecision(17) << value;
    auto text = out.str();
    if (text.find_first_of(".eE") == std::string::npos)
        text += ".0";
    return text;
}
std::string Number(double value, Scalar scalar)
{
    std::ostringstream out;
    if (scalar == Scalar::Float)
    {
        out << std::setprecision(9) << static_cast<float>(value);
        auto text = out.str();
        if (text.find_first_of(".eE") == std::string::npos)
            text += ".0";
        return text + "f";
    }
    out << std::fixed << std::setprecision(0) << value;
    return out.str();
}
} // namespace
std::string GenerateCppHeader(const Package& package)
{
    std::map<std::string, const BlockLayout*> blocks;
    std::map<std::string, std::string> schemas;
    for (const auto& interface : package.interfaces)
        for (const auto& block : interface.blocks)
        {
            if (block.schemaId.empty())
                continue;
            const auto identity = std::to_string(block.schemaVersion) + ":" + block.layoutHash;
            if (const auto old = schemas.find(block.schemaId);
                old != schemas.end() && old->second != identity)
                throw std::invalid_argument("Conflicting parameter schema across passes: " +
                                            block.schemaId);
            schemas[block.schemaId] = identity;
            if (const auto old = blocks.find(block.typeName);
                old != blocks.end() && (old->second->schemaId != block.schemaId ||
                                        old->second->layoutHash != block.layoutHash))
                throw std::invalid_argument("Conflicting generated C++ parameter type " +
                                            block.typeName);
            blocks[block.typeName] = &block;
        }
    std::ostringstream out;
    out << "// Generated from parameter schemas. Do not edit.\n#pragma once\n"
           "#include \"shader/package.hpp\"\n#include <array>\n#include <cmath>\n#include "
           "<cstddef>\n"
           "#include <cstdint>\n#include <cstring>\n#include <stdexcept>\n#include <type_traits>\n"
           "namespace renderlab::shader::generated\n{\n";
    for (const auto& [name, input] : blocks)
    {
        const auto& block = *input;
        out << "inline constexpr BlockContract " << name << "Contract{"
            << std::quoted(block.schemaId) << ", " << block.schemaVersion << ", "
            << std::quoted(block.layoutHash) << ", static_cast<Scope>("
            << static_cast<uint32_t>(block.scope) << ")};\n";
        out << "struct " << name << "\n{\n";
        for (const auto& field : block.fields)
        {
            const auto& property = field.property;
            const auto& type = property.type;
            const auto count = type.rows * type.columns * type.count;
            const auto scalar = type.scalar == Scalar::Float ? "float"
                                : type.scalar == Scalar::Int ? "int32_t"
                                                             : "uint32_t";
            if (count == 1)
                out << "    " << scalar << " " << property.symbol << " = "
                    << Number(property.defaults.front(), type.scalar) << ";\n";
            else
            {
                out << "    std::array<" << scalar << ", " << count << "> " << property.symbol
                    << " = {";
                for (size_t i = 0; i < property.defaults.size(); ++i)
                    out << (i ? ", " : "") << Number(property.defaults[i], type.scalar);
                out << "};\n";
            }
        }
        out << "};\ninline std::array<std::byte, " << ((block.size + 255u) & ~255u)
            << "> Pack(const " << name << "& value)\n{\n    std::array<std::byte, "
            << ((block.size + 255u) & ~255u)
            << "> bytes{};\n"
               "    const auto store = [&](uint32_t offset, auto number)\n    {\n"
               "        static_assert(sizeof(number) == 4);\n"
               "        if constexpr (std::is_floating_point_v<decltype(number)>)\n"
               "            if (!std::isfinite(number)) throw std::invalid_argument(\"Non-finite "
               "parameter\");\n"
               "        std::memcpy(bytes.data() + offset, &number, 4);\n    };\n";
        for (const auto& field : block.fields)
        {
            const auto& property = field.property;
            const auto& type = property.type;
            for (uint32_t element = 0; element < type.count; ++element)
                for (uint32_t row = 0; row < type.rows; ++row)
                    for (uint32_t column = 0; column < type.columns; ++column)
                    {
                        const auto index =
                            element * type.rows * type.columns + row * type.columns + column;
                        const auto offset =
                            field.offset + element * field.arrayStride +
                            (type.columns > 1 ? column * field.matrixStride + row * 4 : row * 4);
                        const auto expression = "value." + property.symbol +
                                                (type.rows * type.columns * type.count == 1
                                                     ? ""
                                                     : "[" + std::to_string(index) + "]");
                        if (property.minimum)
                            out << "    if (static_cast<double>(" << expression << ") < "
                                << Limit(*property.minimum)
                                << ") throw std::invalid_argument(\"Parameter below minimum\");\n";
                        if (property.maximum)
                            out << "    if (static_cast<double>(" << expression << ") > "
                                << Limit(*property.maximum)
                                << ") throw std::invalid_argument(\"Parameter above maximum\");\n";
                        if (type.scalar == Scalar::Bool)
                            out << "    if (" << expression
                                << " > 1) throw std::invalid_argument(\"Invalid bool "
                                   "parameter\");\n";
                        if (!property.enumItems.empty())
                        {
                            out << "    if (";
                            for (size_t i = 0; i < property.enumItems.size(); ++i)
                                out << (i ? " && " : "") << expression
                                    << " != " << property.enumItems[i].value;
                            out << ") throw std::invalid_argument(\"Invalid enum parameter\");\n";
                        }
                        out << "    store(" << offset << ", " << expression << ");\n";
                    }
        }
        out << "    return bytes;\n}\n";
    }
    out << "} // namespace renderlab::shader::generated\n";
    return out.str();
}
} // namespace renderlab::shader
