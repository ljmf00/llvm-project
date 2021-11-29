//===-- DWARFASTParser.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSER_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSER_H

#include "DWARFDIE.h"
#include "DWARFDefines.h"
#include "DWARFFormValue.h"
#include "lldb/Core/Declaration.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/CompilerDecl.h"
#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/lldb-enumerations.h"

class DWARFDIE;
namespace lldb_private {
class CompileUnit;
class ExecutionContext;
}
class SymbolFileDWARF;

class DWARFASTParser {
public:
  virtual ~DWARFASTParser() = default;

  virtual lldb::TypeSP ParseTypeFromDWARF(const lldb_private::SymbolContext &sc,
                                          const DWARFDIE &die,
                                          bool *type_is_new_ptr) = 0;

  virtual lldb_private::Function *
  ParseFunctionFromDWARF(lldb_private::CompileUnit &comp_unit,
                         const DWARFDIE &die,
                         const lldb_private::AddressRange &range) = 0;

  virtual bool
  CompleteTypeFromDWARF(const DWARFDIE &die, lldb_private::Type *type,
                        lldb_private::CompilerType &compiler_type) = 0;

  virtual lldb_private::CompilerDecl
  GetDeclForUIDFromDWARF(const DWARFDIE &die) = 0;

  virtual lldb_private::CompilerDeclContext
  GetDeclContextForUIDFromDWARF(const DWARFDIE &die) = 0;

  virtual lldb_private::CompilerDeclContext
  GetDeclContextContainingUIDFromDWARF(const DWARFDIE &die) = 0;

  virtual void EnsureAllDIEsInDeclContextHaveBeenParsed(
      lldb_private::CompilerDeclContext decl_context) = 0;

  static llvm::Optional<lldb_private::SymbolFile::ArrayInfo>
  ParseChildArrayInfo(const DWARFDIE &parent_die,
                      const lldb_private::ExecutionContext *exe_ctx = nullptr);

  static lldb::AccessType GetAccessTypeFromDWARF(uint32_t dwarf_accessibility);
};

/// Parsed form of all attributes that are relevant for type reconstruction.
/// Some attributes are relevant for all kinds of types (declaration), while
/// others are only meaningful to a specific type (is_virtual).
struct ParsedDWARFTypeAttributes {
  explicit ParsedDWARFTypeAttributes(const DWARFDIE &die);

  lldb::AccessType accessibility = lldb::eAccessNone;
  const char *mangled_name = nullptr;
  lldb_private::ConstString name;
  lldb_private::Declaration decl;
  DWARFDIE object_pointer;
  DWARFFormValue abstract_origin;
  DWARFFormValue containing_type;
  DWARFFormValue signature;
  DWARFFormValue specification;
  DWARFFormValue type;
  lldb::LanguageType class_language = lldb::eLanguageTypeUnknown;
  llvm::Optional<uint64_t> byte_size;
  size_t calling_convention = llvm::dwarf::DW_CC_normal;
  uint32_t bit_stride = 0;
  uint32_t byte_stride = 0;
  uint32_t encoding = 0;
  uint32_t attr_flags = 0;

  // clang-format off
  enum DWARFAttributeFlags : unsigned {
    /// Represents no attributes.
    eDWARFAttributeNone             = 0u,
    /// Whether it is an artificially generated symbol.
    eDWARFAttributeArtificial       = 1u << 0,
    /// Whether it has explicit property of member function.
    eDWARFAttributeExplicit         = 1u << 1,
    /// Whether it is a forward declaration.
    eDWARFAttributeForwardDecl      = 1u << 2,
    /// Whether it is an inlined symbol.
    eDWARFAttributeInline           = 1u << 3,
    /// Whether it is a scoped enumeration (class enum).
    eDWARFAttributeScopedEnum       = 1u << 4,
    /// Whether it has vector attribute.
    eDWARFAttributeVector           = 1u << 5,
    /// Whether it has virtuality attribute.
    eDWARFAttributeVirtual          = 1u << 6,
    /// Whether it is an external symbol.
    eDWARFAttributeExternal         = 1u << 7,
    /// Whether it export symbols to the containing scope.
    eDWARFAttributeExportSymbols    = 1u << 8,
    /// Whether it is an Objective-C direct call.
    eDWARFAttributeObjCDirect       = 1u << 9,
    /// Whether it is an Objective-C complete type.
    eDWARFAttributeObjCCompleteType = 1u << 10,
    LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/eDWARFAttributeObjCCompleteType),
  };
  // clang-format on

  void setIsArtificial() {
    attr_flags |= eDWARFAttributeArtificial;
  }
  bool isArtificial() const {
    return attr_flags & eDWARFAttributeArtificial;
  }

  void setIsExplicit() {
    attr_flags |= eDWARFAttributeExplicit;
  }
  bool isExplicit() const {
    return attr_flags & eDWARFAttributeExplicit;
  }

  void setIsForwardDeclaration() {
    attr_flags |= eDWARFAttributeForwardDecl;
  }
  bool isForwardDeclaration() const {
    return attr_flags & eDWARFAttributeForwardDecl;
  }

  void setIsInline() {
    attr_flags |= eDWARFAttributeInline;
  }
  bool isInline() const {
    return attr_flags & eDWARFAttributeInline;
  }

  void setIsScopedEnum() {
    attr_flags |= eDWARFAttributeScopedEnum;
  }
  bool isScopedEnum() const {
    return attr_flags & eDWARFAttributeScopedEnum;
  }

  void setIsVector() {
    attr_flags |= eDWARFAttributeVector;
  }
  bool isVector() const {
    return attr_flags & eDWARFAttributeVector;
  }

  void setIsVirtual() {
    attr_flags |= eDWARFAttributeVirtual;
  }
  bool isVirtual() const {
    return attr_flags & eDWARFAttributeVirtual;
  }

  void setIsExternal() {
    attr_flags |= eDWARFAttributeExternal;
  }
  bool isExternal() const {
    return attr_flags & eDWARFAttributeExternal;
  }

  void setIsExportsSymbols() {
    attr_flags |= eDWARFAttributeExportSymbols;
  }
  bool isExportsSymbols() const {
    return attr_flags & eDWARFAttributeExportSymbols;
  }

  void setIsObjCDirectCall() {
    attr_flags |= eDWARFAttributeObjCDirect;
  }
  bool isObjCDirectCall() const {
    return attr_flags & eDWARFAttributeObjCDirect;
  }

  void setIsObjCCompleteType() {
    attr_flags |= eDWARFAttributeObjCCompleteType;
  }
  bool isObjCCompleteType() const {
    return attr_flags & eDWARFAttributeObjCCompleteType;
  }
};

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSER_H
