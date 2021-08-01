//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/tl/tl_writer.h"

#include <cassert>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace td {
namespace tl {

class TlWriterDotNet : public TL_writer {
 public:
  bool is_header_;
  std::string prefix_;
  TlWriterDotNet(const std::string &name, bool is_header, const std::string &prefix = "")
      : TL_writer(name), is_header_(is_header), prefix_(prefix) {
  }
  int get_max_arity(void) const override {
    return 0;
  }

  bool is_built_in_simple_type(const std::string &name) const override {
    return name == "Bool" || name == "Int32" || name == "Int53" || name == "Int64" || name == "Double" ||
           name == "String" || name == "Bytes";
  }
  bool is_built_in_complex_type(const std::string &name) const override {
    return name == "Vector";
  }
  bool is_type_bare(const tl_type *t) const override {
    return t->simple_constructors <= 1 || (is_built_in_simple_type(t->name) && t->name != "Bool") ||
           is_built_in_complex_type(t->name);
  }

  std::vector<std::string> get_parsers(void) const override {
    return {"FromUnmanaged"};
  }
  int get_parser_type(const tl_combinator *t, const std::string &name) const override {
    return 0;
  }
  Mode get_parser_mode(int type) const override {
    return All;  // Server;
  }
  std::vector<std::string> get_storers(void) const override {
    return {"ToUnmanaged", "ToString"};
  }
  std::vector<std::string> get_additional_functions(void) const override {
    return {"ToUnmanaged", "FromUnmanaged"};
  }
  int get_storer_type(const tl_combinator *t, const std::string &name) const override {
    return name == "ToString";
  }
  Mode get_storer_mode(int type) const override {
    return type <= 1 ? All : Server;
  }

  std::string gen_base_tl_class_name(void) const override {
    return "BaseObject";
  }
  std::string gen_base_type_class_name(int arity) const override {
    assert(arity == 0);
    return "Object";
  }
  std::string gen_base_function_class_name(void) const override {
    return "Function";
  }

  static std::string to_camelCase(const std::string &name) {
    return to_cCamelCase(name, false);
  }
  static std::string to_CamelCase(const std::string &name) {
    return to_cCamelCase(name, true);
  }
  static std::string to_cCamelCase(const std::string &name, bool flag) {
    bool next_to_upper = flag;
    std::string result;
    for (int i = 0; i < (int)name.size(); i++) {
      if (!is_alnum(name[i])) {
        next_to_upper = true;
        continue;
      }
      if (next_to_upper) {
        result += (char)to_upper(name[i]);
        next_to_upper = false;
      } else {
        result += name[i];
      }
    }
    return result;
  }

  std::string gen_native_field_name(std::string name) const {
    for (int i = 0; i < (int)name.size(); i++) {
      if (!is_alnum(name[i])) {
        name[i] = '_';
      }
    }
    assert(name.size() > 0);
    assert(name[name.size() - 1] != '_');
    return name + "_";
  }

  std::string gen_native_class_name(std::string name) const {
    if (name == "Object") {
      assert(0);
    }
    if (name == "#") {
      return "int32_t";
    }
    for (int i = 0; i < (int)name.size(); i++) {
      if (!is_alnum(name[i])) {
        name[i] = '_';
      }
    }
    return name;
  }

  std::string gen_class_name(std::string name) const override {
    if (name == "Object" || name == "#") {
      assert(0);
    }
    return to_CamelCase(name);
  }
  std::string gen_field_name(std::string name) const override {
    assert(name.size() > 0);
    assert(is_alnum(name.back()));
    return to_CamelCase(name);
  }

  std::string gen_type_name(const tl_tree_type *tree_type) const override {
    const tl_type *t = tree_type->type;
    const std::string &name = t->name;

    if (name == "#") {
      assert(0);
    }
    if (name == "Bool") {
      return "bool";
    }
    if (name == "Int32") {
      return "int32";
    }
    if (name == "Int53" || name == "Int64") {
      return "int64";
    }
    if (name == "Double") {
      return "float64";
    }
    if (name == "String") {
      return "String^";
    }
    if (name == "Bytes") {
      return "Array<byte>^";
    }

    if (name == "Vector") {
      assert(t->arity == 1);
      assert((int)tree_type->children.size() == 1);
      assert(tree_type->children[0]->get_type() == NODE_TYPE_TYPE);
      const tl_tree_type *child = static_cast<const tl_tree_type *>(tree_type->children[0]);

      return "Array<" + gen_type_name(child) + ">^";
    }

    assert(!is_built_in_simple_type(name) && !is_built_in_complex_type(name));

    for (int i = 0; i < (int)tree_type->children.size(); i++) {
      assert(tree_type->children[i]->get_type() == NODE_TYPE_NAT_CONST);
    }

    return gen_main_class_name(t) + "^";
  }
  std::string gen_output_begin(void) const override {
    return prefix_ +
           "#include \"td/tl/tl_dotnet_object.h\"\n\n"
           "namespace Telegram {\n"
           "namespace Td {\n"
           "namespace Api {\n";
  }
  std::string gen_output_end() const override {
    return "}\n"
           "}\n"
           "}\n";
  }

  std::string gen_forward_class_declaration(const std::string &class_name, bool is_proxy) const override {
    if (!is_header_) {
      return "";
    }
    std::stringstream ss;
    ss << (is_proxy ? "interface" : "ref") << " class " << class_name << ";\n";
    return ss.str();
  }

  std::string gen_class_begin(const std::string &class_name, const std::string &base_class_name,
                              bool is_proxy) const override {
    if (!is_header_) {
      return "";
    }

    std::stringstream ss;
    ss << "\npublic " << (is_proxy ? "interface" : "ref") << " class " << class_name << (is_proxy ? "" : " sealed")
       << (class_name == gen_base_tl_class_name() ? "" : " : " + base_class_name) << " {\n"
       << " public:\n";
    return ss.str();
  }
  std::string gen_class_end(void) const override {
    return "";
  }

  std::string gen_field_definition(const std::string &class_name, const std::string &type_name,
                                   const std::string &field_name) const override {
    if (!is_header_) {
      return "";
    }
    auto fixed_field_name = field_name;
    if (field_name == class_name) {
      fixed_field_name += "Value";
    }
    if (type_name.substr(0, field_name.size()) == field_name) {
      auto fixed_type_name = "::Telegram::Td::Api::" + type_name;
      std::stringstream ss;
      ss << "private:\n";
      ss << "  " << fixed_type_name << " " << fixed_field_name << "PrivateField;\n";
      ss << "public:\n";
      ss << "  property " << fixed_type_name << " " << fixed_field_name << " {\n";
      ss << "    " << fixed_type_name << " get() {\n";
      ss << "      return " << fixed_field_name << "PrivateField;\n";
      ss << "    }\n";
      ss << "    void set(" << fixed_type_name << " newValue) {\n";
      ss << "      " << fixed_field_name << "PrivateField = newValue;\n";
      ss << "    }\n";
      ss << "  }\n";
      return ss.str();
    }
    return "  property " + type_name + " " + fixed_field_name + ";\n";
  }

  std::string gen_store_function_begin(const std::string &storer_name, const std::string &class_name, int arity,
                                       std::vector<var_description> &vars, int storer_type) const override {
    if (storer_type < 0) {
      return "";
    }
    std::stringstream ss;
    ss << "\n";
    if (storer_type) {
      ss << (is_header_ ? "  virtual " : "") << "String^ " << (is_header_ ? "" : gen_class_name(class_name) + "::")
         << "ToString()" << (is_header_ ? " override;" : " {\n  return ::Telegram::Td::Api::ToString(this);\n}")
         << "\n";
    } else {
      ss << (is_header_ ? "  virtual " : "") << "NativeObject^ "
         << (is_header_ ? "" : gen_class_name(class_name) + "::") << "ToUnmanaged()";
      if (is_header_) {
        ss << ";\n";
      } else {
        ss << " {\n  return REF_NEW NativeObject(::Telegram::Td::Api::ToUnmanaged(this).release());\n}\n";
      }
    }
    return ss.str();
  }
  std::string gen_store_function_end(const std::vector<var_description> &vars, int storer_type) const override {
    return "";
  }

  std::string gen_constructor_begin(int field_count, const std::string &class_name, bool is_default) const override {
    std::stringstream ss;
    ss << "\n";
    ss << (is_header_ ? "  " : gen_class_name(class_name) + "::") << gen_class_name(class_name) << "(";
    return ss.str();
  }
  std::string gen_constructor_parameter(int field_num, const std::string &class_name, const arg &a,
                                        bool is_default) const override {
    if (is_default) {
      return "";
    }
    std::stringstream ss;
    ss << (field_num == 0 ? "" : ", ");
    auto field_type = gen_field_type(a);
    auto pos = 0;
    while (field_type.substr(pos, 6) == "Array<") {
      pos += 6;
    }
    if (field_type.substr(pos, 6) != "String" && to_upper(field_type[pos]) == field_type[pos]) {
      field_type = field_type.substr(0, pos) + "::Telegram::Td::Api::" + field_type.substr(pos);
    }
    ss << field_type << " " << to_camelCase(a.name);
    return ss.str();
  }
  std::string gen_constructor_field_init(int field_num, const std::string &class_name, const arg &a,
                                         bool is_default) const override {
    if (is_default || is_header_) {
      return "";
    }

    std::stringstream ss;
    if (field_num == 0) {
      ss << ") {\n";
    }
    auto field_name = gen_field_name(a.name);
    if (field_name == class_name) {
      field_name += "Value";
    }
    ss << "  " << field_name << " = " << to_camelCase(a.name) << ";\n";

    return ss.str();
  }
  std::string gen_constructor_end(const tl_combinator *t, int field_count, bool is_default) const override {
    if (is_header_) {
      return ");\n";
    }
    std::stringstream ss;
    if (field_count == 0) {
      ss << ") {\n";
    }
    ss << "}\n";
    return ss.str();
  }
  std::string gen_additional_function(const std::string &function_name, const tl_combinator *t,
                                      bool is_function) const override {
    std::stringstream ss;
    if (is_header_ && function_name == "ToUnmanaged") {
      ss << "};\n";
    }
    ss << "\n";
    if (function_name == "ToUnmanaged") {
      gen_to_unmanaged(ss, t);
    } else {
      gen_from_unmanaged(ss, t);
    }
    return ss.str();
  }
  void gen_to_unmanaged(std::stringstream &ss, const tl_combinator *t) const {
    auto native_class_name = gen_native_class_name(t->name);
    auto class_name = gen_class_name(t->name);
    ss << "td::td_api::object_ptr<td::td_api::" << native_class_name << "> ToUnmanaged(" << class_name << "^ from)";
    if (is_header_) {
      ss << ";\n";
      return;
    }
    ss << " {\n"
       << "  if (!from) {\n"
       << "    return nullptr;\n"
       << "  }\n"
       << "  return td::td_api::make_object<td::td_api::" << native_class_name << ">(";
    bool is_first = true;
    for (auto &it : t->args) {
      if (is_first) {
        is_first = false;
      } else {
        ss << ", ";
      }
      auto field_name = gen_field_name(it.name);
      if (field_name == class_name) {
        field_name += "Value";
      }
      ss << "ToUnmanaged(from->" << field_name << ")";
    }
    ss << ");\n}\n";
  }

  void gen_from_unmanaged(std::stringstream &ss, const tl_combinator *t) const {
    auto native_class_name = gen_native_class_name(t->name);
    auto class_name = gen_class_name(t->name);
    ss << class_name << "^ FromUnmanaged(td::td_api::" << native_class_name << " &from)";
    if (is_header_) {
      ss << ";\n";
      return;
    }
    ss << " {\n"
       << "  return REF_NEW " << class_name << "(";
    bool is_first = true;
    for (auto &it : t->args) {
      if (is_first) {
        is_first = false;
      } else {
        ss << ", ";
      }
      bool need_bytes = gen_field_type(it) == "Array<byte>^" || gen_field_type(it) == "Array<Array<byte>^>^";
      ss << (need_bytes ? "Bytes" : "") << "FromUnmanaged(from." << gen_native_field_name(it.name) << ")";
    }
    ss << ");\n}\n";
  }

  std::string gen_array_type_name(const tl_tree_array *arr, const std::string &field_name) const override {
    assert(0);
    return std::string();
  }
  std::string gen_var_type_name(void) const override {
    assert(0);
    return std::string();
  }

  std::string gen_int_const(const tl_tree *tree_c, const std::vector<var_description> &vars) const override {
    assert(0);
    return std::string();
  }

  std::string gen_var_name(const var_description &desc) const override {
    assert(0);
    return "";
  }
  std::string gen_parameter_name(int index) const override {
    assert(0);
    return "";
  }

  std::string gen_class_alias(const std::string &class_name, const std::string &alias_name) const override {
    return "";
  }

  std::string gen_vars(const tl_combinator *t, const tl_tree_type *result_type,
                       std::vector<var_description> &vars) const override {
    assert(vars.empty());
    return "";
  }
  std::string gen_function_vars(const tl_combinator *t, std::vector<var_description> &vars) const override {
    assert(vars.empty());
    return "";
  }
  std::string gen_uni(const tl_tree_type *result_type, std::vector<var_description> &vars,
                      bool check_negative) const override {
    assert(result_type->children.empty());
    return "";
  }
  std::string gen_constructor_id_store(std::int32_t id, int storer_type) const override {
    return "";
  }
  std::string gen_field_fetch(int field_num, const arg &a, std::vector<var_description> &vars, bool flat,
                              int parser_type) const override {
    return "";
    // std::stringstream ss;
    // ss << gen_field_name(a.name) << " = from_unmanaged(from->" <<
    // gen_native_field_name(a.name) << ");\n"; return ss.str();
  }
  std::string gen_field_store(const arg &a, std::vector<var_description> &vars, bool flat,
                              int storer_type) const override {
    return "";
    // std::stringstream ss;
    // ss << "to_unmanaged(" << gen_field_name(a.name) << ")";
    // return ss.str();
  }
  std::string gen_type_fetch(const std::string &field_name, const tl_tree_type *tree_type,
                             const std::vector<var_description> &vars, int parser_type) const override {
    assert(vars.empty());
    return "";
  }

  std::string gen_type_store(const std::string &field_name, const tl_tree_type *tree_type,
                             const std::vector<var_description> &vars, int storer_type) const override {
    return "";
  }
  std::string gen_var_type_fetch(const arg &a) const override {
    assert(0);
    return "";
  }

  std::string gen_get_id(const std::string &class_name, std::int32_t id, bool is_proxy) const override {
    return "";
  }

  std::string gen_function_result_type(const tl_tree *result) const override {
    return "";
  }

  std::string gen_fetch_function_begin(const std::string &parser_name, const std::string &class_name,
                                       const std::string &parent_class_name, int arity, int field_count,
                                       std::vector<var_description> &vars, int parser_type) const override {
    return "";
  }
  std::string gen_fetch_function_end(bool has_parent, int field_count, const std::vector<var_description> &vars,
                                     int parser_type) const override {
    return "";
  }

  std::string gen_fetch_function_result_begin(const std::string &parser_name, const std::string &class_name,
                                              const tl_tree *result) const override {
    return "";
  }
  std::string gen_fetch_function_result_end(void) const override {
    return "";
  }
  std::string gen_fetch_function_result_any_begin(const std::string &parser_name, const std::string &class_name,
                                                  bool is_proxy) const override {
    return "";
  }
  std::string gen_fetch_function_result_any_end(bool is_proxy) const override {
    return "";
  }

  std::string gen_fetch_switch_begin(void) const override {
    return "";
  }
  std::string gen_fetch_switch_case(const tl_combinator *t, int arity) const override {
    return "";
  }
  std::string gen_fetch_switch_end(void) const override {
    return "";
  }

  std::string gen_additional_proxy_function_begin(const std::string &function_name, const tl_type *type,
                                                  const std::string &name, int arity, bool is_function) const override {
    std::stringstream ss;
    if (is_header_ && function_name == "ToUnmanaged") {
      ss << "};\n";
    }
    if (type == nullptr) {
      return ss.str();
    }
    auto native_class_name = gen_native_class_name(type->name);
    auto class_name = gen_class_name(type->name);
    if (function_name == "ToUnmanaged") {
      ss << "td::td_api::object_ptr<td::td_api::" << native_class_name << "> ToUnmanaged(" << class_name << "^ from)";
      if (is_header_) {
        ss << ";\n";
        return ss.str();
      }
      ss << " {\n"
         << "  if (!from) {\n"
         << "    return nullptr;\n"
         << "  }\n"
         << "  return td::td_api::move_object_as<td::td_api::" << native_class_name
         << ">(from->ToUnmanaged()->get_object_ptr());\n}\n";
    } else {
      ss << class_name << "^ FromUnmanaged(td::td_api::" << native_class_name << " &from)";
      if (is_header_) {
        ss << ";\n";
        return ss.str();
      }
      ss << " {\n";
      ss << "  return DoFromUnmanaged<" << class_name << "^>(from);\n";
      ss << "}\n";
    }
    return ss.str();
  }
  std::string gen_additional_proxy_function_case(const std::string &function_name, const tl_type *type,
                                                 const std::string &class_name, int arity) const override {
    return "";
  }
  std::string gen_additional_proxy_function_case(const std::string &function_name, const tl_type *type,
                                                 const tl_combinator *t, int arity, bool is_function) const override {
    return "";
  }
  std::string gen_additional_proxy_function_end(const std::string &function_name, const tl_type *type,
                                                bool is_function) const override {
    return "";
  }
};

}  // namespace tl
}  // namespace td
