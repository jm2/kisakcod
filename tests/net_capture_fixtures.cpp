// Implementation of the commercial-reference capture fixture subsystem.
// See net_capture_fixtures.hpp for the doctrine (fail-closed certification,
// explicit variable capture fields, no fork baselines).

#include "net_capture_fixtures.hpp"

#include <cstdlib>
#include <utility>

namespace netcapture
{

const KindRule *ruleForKind(const std::string &kind)
{
    for (std::size_t i = 0; i < kKindRuleCount; ++i)
        if (kind == kKindRules[i].kind)
            return &kKindRules[i];
    return nullptr;
}

bool parseVariableField(const std::string &spec, VariableField &out)
{
    const std::size_t at = spec.find('@');
    if (at == std::string::npos || at == 0 || at + 1 >= spec.size())
        return false;
    const std::size_t colon = spec.find(':', at + 1);
    if (colon == std::string::npos || colon + 1 >= spec.size())
        return false;

    out.name = spec.substr(0, at);

    const std::string byteText = spec.substr(at + 1, colon - at - 1);
    const std::string lenText = spec.substr(colon + 1);
    if (byteText.empty() || lenText.empty())
        return false;

    char *end = nullptr;
    const unsigned long long byteValue = std::strtoull(byteText.c_str(), &end, 10);
    if (end != byteText.c_str() + byteText.size() || *end != '\0')
        return false;
    const unsigned long long lenValue = std::strtoull(lenText.c_str(), &end, 10);
    if (end != lenText.c_str() + lenText.size() || *end != '\0')
        return false;

    out.byte = static_cast<std::size_t>(byteValue);
    out.len = static_cast<std::size_t>(lenValue);
    return true;
}

bool parseHexBytes(const std::string &hex, std::vector<std::uint8_t> &out)
{
    if (hex.size() % 2 != 0)
        return false;
    out.clear();
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2)
    {
        char byteText[3] = {hex[i], hex[i + 1], '\0'};
        char *end = nullptr;
        const unsigned long value = std::strtoul(byteText, &end, 16);
        if (end != byteText + 2 || value > 0xFFu)
            return false;
        out.push_back(static_cast<std::uint8_t>(value));
    }
    return true;
}

namespace
{

std::string trim(const std::string &s)
{
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && (s[begin] == ' ' || s[begin] == '\t'))
        ++begin;
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r'))
        --end;
    return s.substr(begin, end - begin);
}

// Splits "key = rest" on the FIRST '='. Returns false for lines without one.
bool splitKeyValue(const std::string &line, std::string &key, std::string &value)
{
    const std::size_t eq = line.find('=');
    if (eq == std::string::npos)
        return false;
    key = trim(line.substr(0, eq));
    value = trim(line.substr(eq + 1));
    return !key.empty();
}

bool hasDeclaredVar(const CaptureSpec &spec, const std::string &name)
{
    for (const VariableField &v : spec.variables)
        if (v.name == name)
            return true;
    return false;
}

} // namespace

CaptureManifest parseManifest(const std::string &profile, const std::string &text)
{
    CaptureManifest manifest;
    manifest.profile = profile;

    CaptureSpec *current = nullptr;
    int lineNumber = 0;

    std::size_t start = 0;
    while (start <= text.size())
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        ++lineNumber;

        std::string line = trim(text.substr(start, end - start));
        start = end + 1;

        if (line.empty() || line[0] == '#')
        {
            if (end >= text.size())
                break;
            continue;
        }

        std::string key;
        std::string value;
        if (!splitKeyValue(line, key, value))
        {
            manifest.error = "line " + std::to_string(lineNumber) + ": expected 'key = value'";
            return manifest;
        }

        if (key == "format_version")
        {
            manifest.formatVersion = std::atoi(value.c_str());
        }
        else if (key == "source_build")
        {
            manifest.sourceBuild = value;
        }
        else if (key == "sanitized_by")
        {
            manifest.sanitizedBy = value;
        }
        else if (key == "capture")
        {
            manifest.captures.emplace_back();
            current = &manifest.captures.back();
            current->file = value;
        }
        else if (key == "kind" || key == "verify" || key == "notes")
        {
            if (!current)
            {
                manifest.error = "line " + std::to_string(lineNumber) + ": '" + key + "' before any 'capture'";
                return manifest;
            }
            if (key == "kind")
                current->kind = value;
            else if (key == "verify")
                current->verify = value;
            else
                current->notes = value;
        }
        else if (key == "var")
        {
            if (!current)
            {
                manifest.error = "line " + std::to_string(lineNumber) + ": 'var' before any 'capture'";
                return manifest;
            }
            VariableField field;
            if (!parseVariableField(value, field))
            {
                manifest.error = "line " + std::to_string(lineNumber) + ": malformed var '" + value + "' (want name@byte:len)";
                return manifest;
            }
            if (hasDeclaredVar(*current, field.name))
            {
                manifest.error = "line " + std::to_string(lineNumber) + ": duplicate var '" + field.name + "'";
                return manifest;
            }
            current->variables.push_back(field);
        }
        else if (key == "input")
        {
            if (!current)
            {
                manifest.error = "line " + std::to_string(lineNumber) + ": 'input' before any 'capture'";
                return manifest;
            }
            const std::size_t eq = value.find('=');
            if (eq == std::string::npos || eq == 0)
            {
                manifest.error = "line " + std::to_string(lineNumber) + ": malformed input '" + value + "' (want name = value)";
                return manifest;
            }
            current->inputs.emplace_back(trim(value.substr(0, eq)), trim(value.substr(eq + 1)));
        }
        else
        {
            manifest.error = "line " + std::to_string(lineNumber) + ": unknown key '" + key + "'";
            return manifest;
        }

        if (end >= text.size())
            break;
    }

    if (manifest.formatVersion != 1)
    {
        manifest.error = "unsupported format_version (want 1)";
        return manifest;
    }
    if (manifest.sourceBuild.empty() || manifest.sanitizedBy.empty())
    {
        manifest.error = "manifest must record source_build and sanitized_by (provenance, see README)";
        return manifest;
    }
    if (manifest.captures.empty())
    {
        manifest.error = "manifest declares no captures";
        return manifest;
    }

    for (const CaptureSpec &spec : manifest.captures)
    {
        const KindRule *rule = ruleForKind(spec.kind);
        if (!rule)
        {
            manifest.error = "capture '" + spec.file + "': unknown kind '" + spec.kind + "'";
            return manifest;
        }
        if (spec.verify != rule->verify)
        {
            manifest.error = "capture '" + spec.file + "': verify must be '" + rule->verify + "' for kind '" + spec.kind + "'";
            return manifest;
        }
        for (std::size_t i = 0; i < rule->requiredVarCount; ++i)
        {
            if (!hasDeclaredVar(spec, rule->requiredVars[i]))
            {
                manifest.error = "capture '" + spec.file + "': kind '" + spec.kind
                                 + "' must declare variable '" + rule->requiredVars[i] + "'";
                return manifest;
            }
        }
        for (std::size_t i = 0; i < manifest.captures.size(); ++i)
        {
            if (&manifest.captures[i] != &spec && manifest.captures[i].file == spec.file)
            {
                manifest.error = "duplicate capture file '" + spec.file + "'";
                return manifest;
            }
        }
    }

    return manifest;
}

bool fixturesRoot(std::string &outRoot)
{
    const char *override = std::getenv("KISAKCOD_NETCAPTURES_DIR");
    if (override && *override)
    {
        outRoot = override;
        return true;
    }
    // The in-tree directory is compiled in as the SOURCE-tree path by CMake
    // (the test binary itself runs from the build tree).
#ifdef KISAKCOD_NETCAPTURES_IN_TREE_DIR
    outRoot = KISAKCOD_NETCAPTURES_IN_TREE_DIR;
    return true;
#else
    return false;
#endif
}

MaskedDiff compareMasked(const std::vector<std::uint8_t> &expected,
                         const std::vector<std::uint8_t> &actual,
                         const std::vector<VariableField> &variables)
{
    MaskedDiff diff;
    diff.ok = false;
    diff.byte = 0;
    diff.bit = 0;

    const std::size_t common = expected.size() < actual.size() ? expected.size() : actual.size();
    auto inVariable = [&variables](std::size_t byte) -> const VariableField *
    {
        for (const VariableField &v : variables)
            if (byte >= v.byte && byte < v.byte + v.len)
                return &v;
        return nullptr;
    };
    auto boundaryOf = [&variables](std::size_t byte) -> const VariableField *
    {
        for (const VariableField &v : variables)
            if (v.len > 0 && byte == v.byte + v.len)
                return &v;
        return nullptr;
    };

    for (std::size_t i = 0; i < common; ++i)
    {
        if (inVariable(i))
            continue;
        if (expected[i] != actual[i])
        {
            diff.byte = i;
            for (std::size_t b = 0; b < 8; ++b)
            {
                if (((expected[i] >> b) & 1u) != ((actual[i] >> b) & 1u))
                {
                    diff.bit = b;
                    break;
                }
            }
            if (const VariableField *v = boundaryOf(i))
                diff.where = "(boundary of variable '" + v->name + "')";
            else
                diff.where = "(invariant byte " + std::to_string(i) + ")";
            return diff;
        }
    }

    if (expected.size() != actual.size())
    {
        diff.byte = common;
        diff.where = "(size: expected " + std::to_string(expected.size()) + ", actual "
                     + std::to_string(actual.size()) + ")";
        return diff;
    }

    diff.ok = true;
    return diff;
}

} // namespace netcapture
