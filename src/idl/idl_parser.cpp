#include "idl/idl_parser.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <optional>
#include <algorithm>
#include <cctype>

namespace {

inline bool str_ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline bool str_starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

inline std::string str_trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string strip_bom(const std::string& src) {
    if (src.size() >= 3 &&
        static_cast<unsigned char>(src[0]) == 0xEF &&
        static_cast<unsigned char>(src[1]) == 0xBB &&
        static_cast<unsigned char>(src[2]) == 0xBF) {
        return src.substr(3);
    }
    return src;
}

std::vector<std::string> extract_idl_includes(const std::string& src) {
    std::vector<std::string> out;
    std::istringstream stream(src);
    std::string line;
    while (std::getline(stream, line)) {
        auto trimmed = str_trim(line);
        if (trimmed.size() < 8 || trimmed.substr(0, 8) != "#include") continue;
        auto rest = str_trim(trimmed.substr(8));
        if (rest.empty()) continue;

        std::string path;
        if (rest[0] == '"') {
            auto end = rest.find('"', 1);
            if (end == std::string::npos) continue;
            path = str_trim(rest.substr(1, end - 1));
        } else if (rest[0] == '<') {
            auto end = rest.find('>', 1);
            if (end == std::string::npos) continue;
            path = str_trim(rest.substr(1, end - 1));
        } else {
            continue;
        }

        if (str_ends_with(path, ".idl")) {
            out.push_back(path);
        }
    }
    return out;
}

std::string strip_comments(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    size_t i = 0;
    while (i < src.size()) {
        if (i + 1 < src.size() && src[i] == '/' && src[i + 1] == '/') {
            while (i < src.size() && src[i] != '\n') ++i;
        } else if (i + 1 < src.size() && src[i] == '/' && src[i + 1] == '*') {
            i += 2;
            while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/')) ++i;
            i += 2;
        } else {
            out.push_back(src[i]);
            ++i;
        }
    }
    return out;
}

std::string strip_preprocessor(const std::string& src) {
    std::string out;
    std::istringstream stream(src);
    std::string line;
    while (std::getline(stream, line)) {
        auto trimmed = str_trim(line);
        if (!trimmed.empty() && trimmed[0] == '#') continue;
        out += line + '\n';
    }
    return out;
}

std::vector<std::string> tokenize(const std::string& src) {
    std::vector<std::string> tokens;
    std::string buf;
    for (char ch : src) {
        switch (ch) {
            case '{': case '}': case '(': case ')': case ';': case ',':
            case '[': case ']':
                if (!buf.empty()) {
                    tokens.push_back(std::move(buf));
                    buf.clear();
                }
                tokens.emplace_back(1, ch);
                break;
            default:
                if (std::isspace(static_cast<unsigned char>(ch))) {
                    if (!buf.empty()) {
                        tokens.push_back(std::move(buf));
                        buf.clear();
                    }
                } else {
                    buf.push_back(ch);
                }
                break;
        }
    }
    if (!buf.empty()) tokens.push_back(std::move(buf));
    return tokens;
}

bool is_identifier_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == ':';
}

std::optional<std::string> parse_type_name(const std::vector<std::string>& tokens, size_t& pos) {
    if (pos >= tokens.size()) return std::nullopt;
    const auto& first = tokens[pos];

    if (first == "unsigned") {
        ++pos;
        if (pos >= tokens.size()) return std::nullopt;
        const auto& second = tokens[pos];
        ++pos;
        if (second == "long" && pos < tokens.size() && tokens[pos] == "long") {
            ++pos;
            return std::string("unsigned long long");
        }
        return "unsigned " + second;
    }

    if (first == "long") {
        ++pos;
        if (pos < tokens.size() && tokens[pos] == "long") {
            ++pos;
            return std::string("long long");
        }
        return std::string("long");
    }

    // sequence<T> as single token (unbounded, no comma inside)
    if (first.size() > 9 && str_starts_with(first, "sequence<") && first.back() == '>') {
        ++pos;
        std::string inner = first.substr(9, first.size() - 10);
        return "sequence<" + inner + ">";
    }

    // sequence<T  (bounded sequence split by comma: sequence<T, N>)
    if (first.size() > 9 && str_starts_with(first, "sequence<") && first.back() != '>') {
        std::string element_type = first.substr(9);
        ++pos;
        if (pos < tokens.size() && tokens[pos] == ",") {
            ++pos;
            while (pos < tokens.size()) {
                auto& t = tokens[pos]; ++pos;
                if (!t.empty() && t.back() == '>') break;
            }
        }
        return "sequence<" + element_type + ">";
    }

    // bounded string: string<N> / wstring<N>
    if (first.size() > 7 && str_starts_with(first, "string<") && first.back() == '>') {
        ++pos;
        return std::string("string");
    }
    if (first.size() > 8 && str_starts_with(first, "wstring<") && first.back() == '>') {
        ++pos;
        return std::string("wstring");
    }

    // bounded string split by comma: string<N, ... > (rare)
    if (first.size() > 7 && str_starts_with(first, "string<")) {
        ++pos;
        while (pos < tokens.size()) {
            auto& t = tokens[pos]; ++pos;
            if (!t.empty() && t.back() == '>') break;
        }
        return std::string("string");
    }
    if (first.size() > 8 && str_starts_with(first, "wstring<")) {
        ++pos;
        while (pos < tokens.size()) {
            auto& t = tokens[pos]; ++pos;
            if (!t.empty() && t.back() == '>') break;
        }
        return std::string("wstring");
    }

    // fixed<N,M>
    if (first.size() > 6 && str_starts_with(first, "fixed<")) {
        ++pos;
        if (first.back() != '>') {
            while (pos < tokens.size()) {
                auto& t = tokens[pos]; ++pos;
                if (!t.empty() && t.back() == '>') break;
            }
        }
        return std::string("fixed");
    }

    if (first == "void"    || first == "string"  || first == "wstring" ||
        first == "short"   || first == "float"   || first == "double"  ||
        first == "boolean" || first == "char"    || first == "wchar"   ||
        first == "octet"   || first == "any"     || first == "Object"  ||
        first == "fixed") {
        ++pos;
        return first;
    }

    if (!first.empty() && is_identifier_start(first[0])) {
        ++pos;
        return first;
    }
    return std::nullopt;
}

std::string build_module_prefix(const std::vector<std::string>& module_stack) {
    std::string prefix;
    for (const auto& m : module_stack) {
        if (!prefix.empty()) prefix += "::";
        prefix += m;
    }
    return prefix;
}

std::string qualified_name(const std::vector<std::string>& module_stack, const std::string& name) {
    auto prefix = build_module_prefix(module_stack);
    return prefix.empty() ? name : (prefix + "::" + name);
}

void resolve_field_typedef(std::string& ftype,
                           const std::unordered_map<std::string, std::string>& typedefs,
                           const std::vector<std::string>& module_stack) {
    auto it = typedefs.find(ftype);
    if (it != typedefs.end()) { ftype = it->second; return; }

    auto sep = ftype.rfind("::");
    if (sep != std::string::npos) {
        auto alias = ftype.substr(sep + 2);
        auto it2 = typedefs.find(alias);
        if (it2 != typedefs.end()) { ftype = it2->second; return; }
    }

    for (auto it3 = module_stack.rbegin(); it3 != module_stack.rend(); ++it3) {
        auto candidate = *it3 + "::" + ftype;
        auto it4 = typedefs.find(candidate);
        if (it4 != typedefs.end()) { ftype = it4->second; return; }
    }
}

struct ParsedOp {
    OpSignature sig;
    size_t consumed;
};

std::optional<ParsedOp> try_parse_operation(
    const std::vector<std::string>& tokens,
    size_t offset,
    const std::string& module,
    const std::string& iface)
{
    size_t i = offset;

    bool oneway = (i < tokens.size() && tokens[i] == "oneway");
    if (oneway) ++i;

    auto return_type = parse_type_name(tokens, i);
    if (!return_type) return std::nullopt;

    if (i >= tokens.size()) return std::nullopt;
    const auto& op_name = tokens[i];
    if (op_name == ";" || op_name == "{" || op_name == "}" ||
        op_name == "(" || op_name == ")") {
        return std::nullopt;
    }
    ++i;

    if (i >= tokens.size() || tokens[i] != "(") return std::nullopt;
    ++i;

    std::vector<IdlParam> params;
    while (true) {
        if (i >= tokens.size()) return std::nullopt;
        if (tokens[i] == ")") { ++i; break; }
        if (tokens[i] == ",") { ++i; continue; }

        ParamDir dir = ParamDir::In;
        if (i < tokens.size()) {
            if (tokens[i] == "in")         { dir = ParamDir::In;    ++i; }
            else if (tokens[i] == "out")   { dir = ParamDir::Out;   ++i; }
            else if (tokens[i] == "inout") { dir = ParamDir::InOut; ++i; }
        }

        auto type_name = parse_type_name(tokens, i);
        if (!type_name) return std::nullopt;
        if (i >= tokens.size()) return std::nullopt;

        std::string param_name = tokens[i];
        ++i;

        IdlParam p;
        p.name = std::move(param_name);
        p.type_name = std::move(*type_name);
        p.direction = dir;
        params.push_back(std::move(p));
    }

    // Skip raises(...) clause
    if (i < tokens.size() && tokens[i] == "raises") {
        ++i;
        if (i < tokens.size() && tokens[i] == "(") {
            ++i;
            int depth = 1;
            while (i < tokens.size() && depth > 0) {
                if (tokens[i] == "(") ++depth;
                else if (tokens[i] == ")") --depth;
                ++i;
            }
        }
    }

    if (i < tokens.size() && tokens[i] == ";") ++i;

    OpSignature sig;
    sig.name = op_name;
    sig.return_type = std::move(*return_type);
    sig.params = std::move(params);
    sig.oneway = oneway;
    sig.interface_name = iface;
    sig.module_name = module;

    return ParsedOp{std::move(sig), i - offset};
}

StructFields parse_struct_fields(
    const std::vector<std::string>& tokens, size_t& j,
    const std::unordered_map<std::string, std::string>& typedefs,
    const std::vector<std::string>& module_stack)
{
    StructFields fields;
    while (j < tokens.size() && tokens[j] != "}") {
        auto ftype = parse_type_name(tokens, j);
        if (ftype) {
            resolve_field_typedef(*ftype, typedefs, module_stack);
            if (j < tokens.size()) {
                std::string fname = tokens[j];
                ++j;

                // Array dimension: type name[N]
                if (j < tokens.size() && tokens[j] == "[") {
                    ++j;
                    std::string dim;
                    while (j < tokens.size() && tokens[j] != "]") {
                        dim += tokens[j]; ++j;
                    }
                    if (j < tokens.size()) ++j; // skip ]
                    if (!dim.empty()) {
                        *ftype = "array<" + *ftype + "," + dim + ">";
                    }
                }

                if (j < tokens.size() && tokens[j] == ";") ++j;
                fields.push_back({std::move(fname), std::move(*ftype)});
            }
        } else {
            ++j;
        }
    }
    return fields;
}

void skip_brace_block(const std::vector<std::string>& tokens, size_t& i) {
    if (i < tokens.size() && tokens[i] == "{") {
        ++i;
        int depth = 1;
        while (i < tokens.size() && depth > 0) {
            if (tokens[i] == "{") ++depth;
            else if (tokens[i] == "}") --depth;
            ++i;
        }
        if (i < tokens.size() && tokens[i] == ";") ++i;
    }
}

void parse_tokens(
    const std::vector<std::string>& tokens,
    std::unordered_map<std::string, std::vector<OpSignature>>& ops,
    std::unordered_map<std::string, StructFields>& structs,
    std::unordered_map<std::string, std::string>& typedefs,
    std::unordered_set<std::string>& enums,
    std::unordered_map<std::string, EnumDef>& enum_defs,
    std::unordered_map<std::string, UnionDef>& unions,
    std::unordered_map<std::string, StructFields>& exceptions)
{
    size_t i = 0;
    std::vector<std::string> module_stack;
    std::string interface_name;
    std::vector<std::string> brace_depth;

    static const std::unordered_set<std::string> skip_keywords = {
        "abstract", "local", "custom", "truncatable", "public", "private",
        "factory", "finder", "provides", "uses", "emits", "publishes",
        "consumes", "manages", "mirrorport", "porttype", "connector",
        "component", "home", "eventtype", "supports", "import",
        "readonly", "getraises", "setraises"
    };

    while (i < tokens.size()) {
        const auto& tok = tokens[i];

        if (skip_keywords.count(tok)) {
            ++i;
            continue;
        }

        // native TypeName;
        if (tok == "native" && i + 1 < tokens.size()) {
            i += 2;
            if (i < tokens.size() && tokens[i] == ";") ++i;
            continue;
        }

        // valuetype: skip body
        if (tok == "valuetype" && i + 1 < tokens.size()) {
            ++i; // skip "valuetype"
            ++i; // skip name
            if (i < tokens.size() && tokens[i] == ";") { ++i; continue; }
            // skip optional inheritance
            while (i < tokens.size() && tokens[i] != "{" && tokens[i] != ";") ++i;
            if (i < tokens.size() && tokens[i] == ";") { ++i; continue; }
            skip_brace_block(tokens, i);
            continue;
        }

        // enum
        if (tok == "enum" && i + 2 < tokens.size()) {
            std::string enum_name = tokens[i + 1];
            size_t j = i + 2;
            if (j < tokens.size() && tokens[j] == "{") {
                ++j;
                EnumDef def;
                int32_t next_value = 0;
                while (j < tokens.size() && tokens[j] != "}") {
                    if (tokens[j] == ",") { ++j; continue; }
                    std::string enumerator = tokens[j]; ++j;
                    // Check for explicit value: ENUMERATOR = VALUE
                    if (j < tokens.size() && tokens[j] == "=") {
                        ++j;
                        if (j < tokens.size()) {
                            try { next_value = std::stoi(tokens[j]); } catch (...) {}
                            ++j;
                        }
                    }
                    def.values.push_back({enumerator, next_value});
                    next_value++;
                }
                if (j < tokens.size() && tokens[j] == "}") {
                    ++j;
                    if (j < tokens.size() && tokens[j] == ";") ++j;
                    std::string key = qualified_name(module_stack, enum_name);
                    enums.insert(key);
                    enum_defs[key] = std::move(def);
                    i = j;
                    continue;
                }
            }
        }

        // struct
        if (tok == "struct" && i + 1 < tokens.size()) {
            std::string struct_name = tokens[i + 1];
            size_t j = i + 2;

            // Forward declaration: struct Foo;
            if (j < tokens.size() && tokens[j] == ";") {
                i = j + 1;
                continue;
            }

            if (j < tokens.size() && tokens[j] == "{") {
                ++j;
                auto fields = parse_struct_fields(tokens, j, typedefs, module_stack);
                if (j < tokens.size() && tokens[j] == "}") {
                    ++j;
                    if (j < tokens.size() && tokens[j] == ";") ++j;
                    std::string key = qualified_name(module_stack, struct_name);
                    auto existing = structs.find(key);
                    bool should_insert = (existing == structs.end()) ||
                                         (fields.size() > existing->second.size());
                    if (should_insert) {
                        structs[key] = std::move(fields);
                    }
                    i = j;
                    continue;
                }
            }
        }

        // exception (parsed like struct)
        if (tok == "exception" && i + 1 < tokens.size()) {
            std::string exc_name = tokens[i + 1];
            size_t j = i + 2;
            if (j < tokens.size() && tokens[j] == ";") { i = j + 1; continue; }
            if (j < tokens.size() && tokens[j] == "{") {
                ++j;
                auto fields = parse_struct_fields(tokens, j, typedefs, module_stack);
                if (j < tokens.size() && tokens[j] == "}") {
                    ++j;
                    if (j < tokens.size() && tokens[j] == ";") ++j;
                    std::string key = qualified_name(module_stack, exc_name);
                    exceptions[key] = std::move(fields);
                    structs[key] = exceptions[key];
                    i = j;
                    continue;
                }
            }
        }

        // union
        if (tok == "union" && i + 1 < tokens.size()) {
            std::string union_name = tokens[i + 1];
            size_t j = i + 2;

            // Forward declaration
            if (j < tokens.size() && tokens[j] == ";") { i = j + 1; continue; }

            // union Foo switch (DiscType) { ... };
            if (j < tokens.size() && tokens[j] == "switch") {
                ++j;
                std::string disc_type;
                if (j < tokens.size() && tokens[j] == "(") {
                    ++j;
                    auto dt = parse_type_name(tokens, j);
                    if (dt) disc_type = *dt;
                    if (j < tokens.size() && tokens[j] == ")") ++j;
                }

                if (j < tokens.size() && tokens[j] == "{") {
                    ++j;
                    UnionDef def;
                    def.discriminator_type = disc_type;

                    while (j < tokens.size() && tokens[j] != "}") {
                        UnionBranch branch;

                        while (j < tokens.size() && (tokens[j] == "case" || tokens[j] == "default")) {
                            if (tokens[j] == "default") {
                                branch.case_labels.push_back("default");
                                ++j;
                                if (j < tokens.size() && tokens[j] == ":") ++j;
                            } else {
                                ++j; // skip "case"
                                std::string label;
                                while (j < tokens.size() && tokens[j] != ":") {
                                    label += tokens[j]; ++j;
                                }
                                branch.case_labels.push_back(label);
                                if (j < tokens.size()) ++j; // skip ":"
                            }
                        }

                        if (branch.case_labels.empty()) { ++j; continue; }

                        auto btype = parse_type_name(tokens, j);
                        if (btype) {
                            resolve_field_typedef(*btype, typedefs, module_stack);
                            branch.type_name = *btype;
                            if (j < tokens.size()) {
                                branch.field_name = tokens[j]; ++j;
                            }
                            // Skip array dimensions
                            if (j < tokens.size() && tokens[j] == "[") {
                                ++j;
                                std::string dim;
                                while (j < tokens.size() && tokens[j] != "]") { dim += tokens[j]; ++j; }
                                if (j < tokens.size()) ++j;
                                branch.type_name = "array<" + branch.type_name + "," + dim + ">";
                            }
                            if (j < tokens.size() && tokens[j] == ";") ++j;
                            def.branches.push_back(std::move(branch));
                        } else {
                            ++j;
                        }
                    }

                    if (j < tokens.size() && tokens[j] == "}") {
                        ++j;
                        if (j < tokens.size() && tokens[j] == ";") ++j;
                        std::string key = qualified_name(module_stack, union_name);
                        unions[key] = std::move(def);
                        i = j;
                        continue;
                    }
                }
            }
        }

        // typedef
        if (tok == "typedef" && i + 1 < tokens.size()) {
            size_t j = i + 1;
            auto typ = parse_type_name(tokens, j);
            if (typ && j < tokens.size()) {
                std::string alias = tokens[j];
                ++j;

                // typedef type name[N] → array typedef
                if (j < tokens.size() && tokens[j] == "[") {
                    ++j;
                    std::string dim;
                    while (j < tokens.size() && tokens[j] != "]") { dim += tokens[j]; ++j; }
                    if (j < tokens.size()) ++j;
                    *typ = "array<" + *typ + "," + dim + ">";
                }

                if (j < tokens.size() && tokens[j] == ";") ++j;
                typedefs[alias] = *typ;

                std::string fq = qualified_name(module_stack, alias);
                if (fq != alias) typedefs[fq] = *typ;

                i = j;
                continue;
            }
        }

        // const: skip entire declaration
        if (tok == "const" && i + 1 < tokens.size()) {
            ++i;
            while (i < tokens.size() && tokens[i] != ";") ++i;
            if (i < tokens.size()) ++i;
            continue;
        }

        // module
        if (tok == "module" && i + 2 < tokens.size()) {
            module_stack.push_back(tokens[i + 1]);
            if (tokens[i + 2] == "{") {
                brace_depth.push_back("module");
                i += 3;
                continue;
            }
        }

        // interface
        if (tok == "interface" && i + 1 < tokens.size()) {
            std::string iface = tokens[i + 1];
            size_t j = i + 2;

            // Forward declaration: interface Foo;
            if (j < tokens.size() && tokens[j] == ";") {
                i = j + 1;
                continue;
            }

            interface_name = iface;
            // Skip inheritance: interface Foo : Bar, Baz {
            while (j < tokens.size() && tokens[j] != "{") ++j;
            if (j < tokens.size()) {
                brace_depth.push_back("interface");
                i = j + 1;
                continue;
            }
        }

        // attribute: [readonly] attribute Type name [raises(...)];
        if (tok == "attribute" && !interface_name.empty()) {
            size_t j = i + 1;
            auto attr_type = parse_type_name(tokens, j);
            if (attr_type && j < tokens.size()) {
                std::string attr_name = tokens[j]; ++j;

                // getter
                {
                    OpSignature sig;
                    sig.name = "_get_" + attr_name;
                    sig.return_type = *attr_type;
                    sig.interface_name = interface_name;
                    sig.module_name = build_module_prefix(module_stack);
                    ops[sig.name].push_back(sig);
                }
                // setter
                {
                    OpSignature sig;
                    sig.name = "_set_" + attr_name;
                    sig.return_type = "void";
                    sig.params.push_back({attr_name, *attr_type, ParamDir::In});
                    sig.interface_name = interface_name;
                    sig.module_name = build_module_prefix(module_stack);
                    ops[sig.name].push_back(sig);
                }

                // skip raises/getraises/setraises
                while (j < tokens.size() && tokens[j] != ";") ++j;
                if (j < tokens.size()) ++j;
                i = j;
                continue;
            }
        }

        if (tok == "{") {
            brace_depth.push_back("other");
            ++i;
            continue;
        }

        if (tok == "}") {
            if (!brace_depth.empty()) {
                auto kind = brace_depth.back();
                brace_depth.pop_back();
                if (kind == "interface") interface_name.clear();
                else if (kind == "module") {
                    if (!module_stack.empty()) module_stack.pop_back();
                }
            }
            ++i;
            if (i < tokens.size() && tokens[i] == ";") ++i;
            continue;
        }

        // Skip array literals at top level (orphan [...])
        if (tok == "[") {
            ++i;
            while (i < tokens.size() && tokens[i] != "]") ++i;
            if (i < tokens.size()) ++i;
            continue;
        }

        // Try to parse operation inside interface
        if (!interface_name.empty()) {
            auto mod_prefix = build_module_prefix(module_stack);
            auto result = try_parse_operation(tokens, i, mod_prefix, interface_name);
            if (result) {
                auto& sig = result->sig;
                auto td = typedefs.find(sig.return_type);
                if (td != typedefs.end()) sig.return_type = td->second;
                for (auto& p : sig.params) {
                    resolve_field_typedef(p.type_name, typedefs, module_stack);
                }
                ops[sig.name].push_back(std::move(sig));
                i += result->consumed;
                continue;
            }
        }

        ++i;
    }
}

} // anonymous namespace

// ─── IdlRegistry public methods ───────────────────────────────────────────

const OpSignature* IdlRegistry::lookup_operation(const std::string& wire_name) const {
    auto trimmed = str_trim(wire_name);
    if (trimmed.empty()) return nullptr;

    auto it = ops.find(trimmed);
    if (it != ops.end() && !it->second.empty()) return &it->second.front();

    auto pos = trimmed.rfind("::");
    if (pos != std::string::npos) {
        auto suffix = str_trim(trimmed.substr(pos + 2));
        if (!suffix.empty() && suffix != trimmed) {
            auto it2 = ops.find(suffix);
            if (it2 != ops.end() && !it2->second.empty()) return &it2->second.front();
        }
    }
    return nullptr;
}

const StructFields* IdlRegistry::get_struct_fields(const std::string& type_name) const {
    auto it = structs.find(type_name);
    if (it != structs.end()) return &it->second;

    auto pos = type_name.rfind("::");
    std::string suffix = (pos != std::string::npos)
        ? str_trim(type_name.substr(pos + 2))
        : str_trim(type_name);

    if (suffix.empty()) return nullptr;

    auto it2 = structs.find(suffix);
    if (it2 != structs.end()) return &it2->second;

    std::string needle = "::" + suffix;
    const StructFields* first_match = nullptr;
    for (const auto& [k, v] : structs) {
        if (str_ends_with(k, needle)) {
            if (!first_match) first_match = &v;
        }
    }
    return first_match;
}

const std::string* IdlRegistry::resolve_typedef(const std::string& type_name) const {
    std::unordered_set<std::string> visited;
    const std::string* current = &type_name;

    while (true) {
        if (visited.count(*current)) return (current != &type_name) ? current : nullptr;
        visited.insert(*current);

        auto it = typedefs.find(*current);
        if (it != typedefs.end()) { current = &it->second; continue; }

        auto pos = current->rfind("::");
        if (pos != std::string::npos) {
            auto alias = current->substr(pos + 2);
            auto it2 = typedefs.find(alias);
            if (it2 != typedefs.end()) { current = &it2->second; continue; }
        }

        return (current != &type_name) ? current : nullptr;
    }
}

bool IdlRegistry::is_enum(const std::string& type_name) const {
    if (enums.count(type_name)) return true;

    auto pos = type_name.rfind("::");
    if (pos != std::string::npos) {
        auto suffix = type_name.substr(pos + 2);
        if (enums.count(suffix)) return true;
    }

    auto trimmed = str_trim(type_name);
    if (!trimmed.empty()) {
        std::string needle = "::" + trimmed;
        for (const auto& e : enums) {
            if (str_ends_with(e, needle)) return true;
        }
    }
    return false;
}

std::string IdlRegistry::get_enum_name(const std::string& type_name, int32_t value) const {
    auto find_in = [&](const std::string& key) -> std::string {
        auto it = enum_defs.find(key);
        if (it != enum_defs.end()) {
            for (const auto& [name, val] : it->second.values) {
                if (val == value) return name;
            }
        }
        return "";
    };

    auto result = find_in(type_name);
    if (!result.empty()) return result;

    auto pos = type_name.rfind("::");
    if (pos != std::string::npos) {
        result = find_in(type_name.substr(pos + 2));
        if (!result.empty()) return result;
    }

    auto trimmed = str_trim(type_name);
    std::string needle = "::" + trimmed;
    for (const auto& [key, def] : enum_defs) {
        if (str_ends_with(key, needle) || key == trimmed) {
            for (const auto& [name, val] : def.values) {
                if (val == value) return name;
            }
        }
    }

    return std::to_string(value);
}

const UnionDef* IdlRegistry::get_union_def(const std::string& type_name) const {
    auto it = unions.find(type_name);
    if (it != unions.end()) return &it->second;

    auto pos = type_name.rfind("::");
    if (pos != std::string::npos) {
        auto suffix = type_name.substr(pos + 2);
        auto it2 = unions.find(suffix);
        if (it2 != unions.end()) return &it2->second;
    }

    auto trimmed = str_trim(type_name);
    std::string needle = "::" + trimmed;
    for (const auto& [k, v] : unions) {
        if (str_ends_with(k, needle)) return &v;
    }
    return nullptr;
}

const StructFields* IdlRegistry::get_exception_fields(const std::string& type_name) const {
    auto it = exceptions.find(type_name);
    if (it != exceptions.end()) return &it->second;

    auto pos = type_name.rfind("::");
    if (pos != std::string::npos) {
        auto suffix = type_name.substr(pos + 2);
        auto it2 = exceptions.find(suffix);
        if (it2 != exceptions.end()) return &it2->second;
    }

    auto trimmed = str_trim(type_name);
    std::string needle = "::" + trimmed;
    for (const auto& [k, v] : exceptions) {
        if (str_ends_with(k, needle)) return &v;
    }
    return nullptr;
}

bool IdlRegistry::parse_file(const std::string& path) {
    std::unordered_set<std::string> parsed;
    return parse_file_with_includes(path, parsed);
}

bool IdlRegistry::parse_file_with_includes(
    const std::string& path,
    std::unordered_set<std::string>& parsed)
{
    namespace fs = std::filesystem;

    std::string canonical;
    try {
        canonical = fs::canonical(path).string();
    } catch (...) {
        canonical = path;
    }

    if (parsed.count(canonical)) return true;
    parsed.insert(canonical);

    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    content = strip_bom(content);

    fs::path parent = fs::path(path).parent_path();
    if (parent.empty()) parent = ".";

    for (const auto& include_path : extract_idl_includes(content)) {
        auto included = parent / include_path;
        if (fs::exists(included)) {
            parse_file_with_includes(included.string(), parsed);
        }
    }

    parse_str(content);
    return true;
}

size_t IdlRegistry::parse_dir_recursive(const std::string& dir) {
    namespace fs = std::filesystem;
    size_t count = 0;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".idl") {
            if (parse_file(entry.path().string())) {
                ++count;
            }
        }
    }
    return count;
}

void IdlRegistry::parse_str(const std::string& src) {
    auto cleaned = strip_comments(src);
    auto no_preproc = strip_preprocessor(cleaned);
    auto tokens = tokenize(no_preproc);
    parse_tokens(tokens, ops, structs, typedefs, enums, enum_defs, unions, exceptions);
}
