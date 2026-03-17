// IDL registry for ORBM.
//
// `IdlRegistry` owns the parsed view of one or more CORBA IDL files:
// - operations grouped by interface (`ops`)
// - struct / exception layouts (`structs`, `exceptions`)
// - enum and union metadata (`enum_defs`, `unions`)
// - typedef chains.
//
// The capture and CDR decoding layers query this registry at runtime to
// map incoming GIOP traffic back to high‑level types.
//
#pragma once

#include "core/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// Full definition of an enum, with explicit numeric values.
struct EnumDef {
    std::vector<std::pair<std::string, int32_t>> values;
};

// Single branch of a CORBA union.
struct UnionBranch {
    std::vector<std::string> case_labels;
    std::string type_name;
    std::string field_name;
};

// Discriminated union definition, including discriminator type and branches.
struct UnionDef {
    std::string discriminator_type;
    std::vector<UnionBranch> branches;
};

class IdlRegistry {
public:
    std::unordered_map<std::string, std::vector<OpSignature>> ops;
    std::unordered_map<std::string, StructFields> structs;
    std::unordered_map<std::string, std::string> typedefs;
    std::unordered_set<std::string> enums;
    std::unordered_map<std::string, EnumDef> enum_defs;
    std::unordered_map<std::string, UnionDef> unions;
    std::unordered_map<std::string, StructFields> exceptions;

    const OpSignature* lookup_operation(const std::string& wire_name) const;
    const StructFields* get_struct_fields(const std::string& type_name) const;
    const std::string* resolve_typedef(const std::string& type_name) const;
    bool is_enum(const std::string& type_name) const;
    std::string get_enum_name(const std::string& type_name, int32_t value) const;
    const UnionDef* get_union_def(const std::string& type_name) const;
    const StructFields* get_exception_fields(const std::string& type_name) const;

    bool parse_file(const std::string& path);
    size_t parse_dir_recursive(const std::string& dir);
    void parse_str(const std::string& src);

private:
    bool parse_file_with_includes(const std::string& path,
                                  std::unordered_set<std::string>& parsed);
};
