//===-- DWARFASTParser.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DWARFASTParser.h"
#include "DWARFAttribute.h"
#include "DWARFDIE.h"
#include "DWARFUnit.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Target/StackFrame.h"

using namespace lldb;
using namespace lldb_private;

llvm::Optional<SymbolFile::ArrayInfo>
DWARFASTParser::ParseChildArrayInfo(const DWARFDIE &parent_die,
                                    const ExecutionContext *exe_ctx) {
  SymbolFile::ArrayInfo array_info;
  if (!parent_die)
    return llvm::None;

  for (DWARFDIE die : parent_die.children()) {
    const dw_tag_t tag = die.Tag();
    if (tag != DW_TAG_subrange_type)
      continue;

    DWARFAttributes attributes;
    const size_t num_child_attributes = die.GetAttributes(attributes);
    if (num_child_attributes > 0) {
      uint64_t num_elements = 0;
      uint64_t lower_bound = 0;
      uint64_t upper_bound = 0;
      bool upper_bound_valid = false;
      uint32_t i;
      for (i = 0; i < num_child_attributes; ++i) {
        const dw_attr_t attr = attributes.AttributeAtIndex(i);
        DWARFFormValue form_value;
        if (attributes.ExtractFormValueAtIndex(i, form_value)) {
          switch (attr) {
          case DW_AT_name:
            break;

          case DW_AT_count:
            if (DWARFDIE var_die = die.GetReferencedDIE(DW_AT_count)) {
              if (var_die.Tag() == DW_TAG_variable)
                if (exe_ctx) {
                  if (auto frame = exe_ctx->GetFrameSP()) {
                    Status error;
                    lldb::VariableSP var_sp;
                    auto valobj_sp = frame->GetValueForVariableExpressionPath(
                        var_die.GetName(), eNoDynamicValues, 0, var_sp, error);
                    if (valobj_sp) {
                      num_elements = valobj_sp->GetValueAsUnsigned(0);
                      break;
                    }
                  }
                }
            } else
              num_elements = form_value.Unsigned();
            break;

          case DW_AT_bit_stride:
            array_info.bit_stride = form_value.Unsigned();
            break;

          case DW_AT_byte_stride:
            array_info.byte_stride = form_value.Unsigned();
            break;

          case DW_AT_lower_bound:
            lower_bound = form_value.Unsigned();
            break;

          case DW_AT_upper_bound:
            upper_bound_valid = true;
            upper_bound = form_value.Unsigned();
            break;

          default:
            break;
          }
        }
      }

      if (num_elements == 0) {
        if (upper_bound_valid && upper_bound >= lower_bound)
          num_elements = upper_bound - lower_bound + 1;
      }

      array_info.element_orders.push_back(num_elements);
    }
  }
  return array_info;
}

AccessType
DWARFASTParser::GetAccessTypeFromDWARF(uint32_t dwarf_accessibility) {
  switch (dwarf_accessibility) {
  case DW_ACCESS_public:
    return eAccessPublic;
  case DW_ACCESS_private:
    return eAccessPrivate;
  case DW_ACCESS_protected:
    return eAccessProtected;
  default:
    break;
  }
  return eAccessNone;
}

ParsedDWARFTypeAttributes::ParsedDWARFTypeAttributes(const DWARFDIE &die) {
  DWARFAttributes attributes;
  size_t num_attributes = die.GetAttributes(attributes);
  for (size_t i = 0; i < num_attributes; ++i) {
    dw_attr_t attr = attributes.AttributeAtIndex(i);
    DWARFFormValue form_value;
    if (!attributes.ExtractFormValueAtIndex(i, form_value))
      continue;
    switch (attr) {
    case DW_AT_abstract_origin:
      abstract_origin = form_value;
      break;

    case DW_AT_accessibility:
      accessibility = DWARFASTParser::GetAccessTypeFromDWARF(form_value.Unsigned());
      break;

    case DW_AT_artificial:
      if (form_value.Boolean())
        setIsArtificial();
      break;

    case DW_AT_bit_stride:
      bit_stride = form_value.Unsigned();
      break;

    case DW_AT_byte_size:
      byte_size = form_value.Unsigned();
      break;

    case DW_AT_byte_stride:
      byte_stride = form_value.Unsigned();
      break;

    case DW_AT_calling_convention:
      calling_convention = form_value.Unsigned();
      break;

    case DW_AT_containing_type:
      containing_type = form_value;
      break;

    case DW_AT_decl_file:
      // die.GetCU() can differ if DW_AT_specification uses DW_FORM_ref_addr.
      decl.SetFile(
          attributes.CompileUnitAtIndex(i)->GetFile(form_value.Unsigned()));
      break;
    case DW_AT_decl_line:
      decl.SetLine(form_value.Unsigned());
      break;
    case DW_AT_decl_column:
      decl.SetColumn(form_value.Unsigned());
      break;

    case DW_AT_declaration:
      if (form_value.Boolean())
        setIsForwardDeclaration();
      break;

    case DW_AT_encoding:
      encoding = form_value.Unsigned();
      break;

    case DW_AT_enum_class:
      if (form_value.Boolean())
        setIsScopedEnum();
      break;

    case DW_AT_explicit:
      if (form_value.Boolean())
        setIsExplicit();
      break;

    case DW_AT_external:
      if (form_value.Unsigned())
        setIsExternal();
      break;

    case DW_AT_inline:
      if (form_value.Boolean())
        setIsInline();
      break;

    case DW_AT_linkage_name:
    case DW_AT_MIPS_linkage_name:
      mangled_name = form_value.AsCString();
      break;

    case DW_AT_name:
      name.SetCString(form_value.AsCString());
      break;

    case DW_AT_object_pointer:
      object_pointer = form_value.Reference();
      break;

    case DW_AT_signature:
      signature = form_value;
      break;

    case DW_AT_specification:
      specification = form_value;
      break;

    case DW_AT_type:
      type = form_value;
      break;

    case DW_AT_virtuality:
      if (form_value.Unsigned())
        setIsVirtual();
      break;

    case DW_AT_APPLE_objc_complete_type:
      if (form_value.Signed())
        setIsObjCCompleteType();
      break;

    case DW_AT_APPLE_objc_direct:
      setIsObjCDirectCall();
      break;

    case DW_AT_APPLE_runtime_class:
      class_language = (LanguageType)form_value.Signed();
      break;

    case DW_AT_GNU_vector:
      if (form_value.Boolean())
        setIsVector();
      break;
    case DW_AT_export_symbols:
      if (form_value.Boolean())
        setIsExportsSymbols();
      break;
    }
  }
}
