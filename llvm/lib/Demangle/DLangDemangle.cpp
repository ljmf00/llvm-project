//===--- DLangDemangle.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines a demangler for the D programming language as specified
/// in the ABI specification, available at:
/// https://dlang.org/spec/abi.html#name_mangling
///
//===----------------------------------------------------------------------===//

#include "llvm/Demangle/Demangle.h"
#include "llvm/Demangle/StringView.h"
#include "llvm/Demangle/Utility.h"

#include <cctype>
#include <cstring>
#include <limits>

using namespace llvm;
using llvm::itanium_demangle::OutputBuffer;
using llvm::itanium_demangle::StringView;

namespace {

/// Demangle information structure.
struct Demangler {
  /// Initialize the information structure we use to pass around information.
  ///
  /// \param Mangled String to demangle.
  Demangler(const char *Mangled);

  /// Extract and demangle the mangled symbol and append it to the output
  /// string.
  ///
  /// \param Demangled Output buffer to write the demangled name.
  ///
  /// \return The remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#name_mangling .
  /// \see https://dlang.org/spec/abi.html#MangledName .
  const char *parseMangle(OutputBuffer *Demangled);

private:
  /// Extract and demangle a given mangled symbol and append it to the output
  /// string.
  ///
  /// \param Demangled output buffer to write the demangled name.
  /// \param Mangled mangled symbol to be demangled.
  ///
  /// \return The remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#name_mangling .
  /// \see https://dlang.org/spec/abi.html#MangledName .
  const char *parseMangle(OutputBuffer *Demangled, const char *Mangled);

  /// Extract the number from a given string.
  ///
  /// \param Mangled string to extract the number.
  /// \param Ret assigned result value.
  ///
  /// \return The remaining string on success or nullptr on failure.
  ///
  /// \note A result larger than UINT_MAX is considered a failure.
  ///
  /// \see https://dlang.org/spec/abi.html#Number .
  const char *decodeNumber(const char *Mangled, unsigned long *Ret);

  /// Extract the hex-digit from a given string.
  ///
  /// \param Mangled string to extract the hex-digit.
  /// \param Ret assigned result value.
  ///
  /// \return the remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#HexDigits .
  const char *decodeHexdigit(const char *Mangled, char *Ret);

  /// Extract the back reference position from a given string.
  ///
  /// \param Mangled string to extract the back reference position.
  /// \param Ret assigned result value.
  ///
  /// \return the remaining string on success or nullptr on failure.
  ///
  /// \note a result <= 0 is a failure.
  ///
  /// \see https://dlang.org/spec/abi.html#back_ref .
  /// \see https://dlang.org/spec/abi.html#NumberBackRef .
  const char *decodeBackrefPos(const char *Mangled, long *Ret);

  /// Extract the symbol pointed by the back reference form a given string.
  ///
  /// \param Mangled string to extract the back reference position.
  /// \param Ret assigned result value.
  ///
  /// \return the remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#back_ref .
  const char *decodeBackref(const char *Mangled, const char **Ret);

  /// Extract and demangle backreferenced symbol from a given mangled symbol
  /// and append it to the output string.
  ///
  /// \param Demangled output buffer to write the demangled name.
  /// \param Mangled mangled symbol to be demangled.
  ///
  /// \return the remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#back_ref .
  /// \see https://dlang.org/spec/abi.html#IdentifierBackRef .
  const char *parseSymbolBackref(OutputBuffer *Demangled, const char *Mangled);

  /// Extract and demangle backreferenced type from a given mangled symbol
  /// and append it to the output string.
  ///
  /// \param Demangled output buffer to write the demangled name.
  /// \param Mangled mangled symbol to be demangled.
  /// \param IsFunction whether the backreferenced type is expected to be a
  ///                   function.
  ///
  /// \return the remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#back_ref .
  /// \see https://dlang.org/spec/abi.html#TypeBackRef .
  const char *parseTypeBackref(OutputBuffer *Demangled, const char *Mangled,
                               bool IsFunction);

  /// Extract and demangle calling convention from a given mangled symbol and
  /// append it to the output string.
  ///
  /// \param Demangled output buffer to write the demangled name.
  /// \param Mangled mangled symbol to be demangled.
  ///
  /// \return the remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#CallConvention .
  /// \see https://dlang.org/spec/abi.html#function_calling_conventions .
  const char *parseCallConvention(OutputBuffer *Demangled, const char *Mangled);

  /// Check whether it is a function calling convention.
  ///
  /// \param Mangled string to extract the function calling convention.
  ///
  /// \return True on success, false otherwise.
  ///
  /// \see https://dlang.org/spec/abi.html#CallConvention .
  /// \see https://dlang.org/spec/abi.html#function_calling_conventions .
  bool isCallConvention(const char *Mangled);

  /// Check whether it is the beginning of a symbol name.
  ///
  /// \param Mangled string to extract the symbol name.
  ///
  /// \return true on success, false otherwise.
  ///
  /// \see https://dlang.org/spec/abi.html#SymbolName .
  bool isSymbolName(const char *Mangled);

  /// Extract and demangle an identifier from a given mangled symbol append it
  /// to the output string.
  ///
  /// \param Demangled Output buffer to write the demangled name.
  /// \param Mangled Mangled symbol to be demangled.
  ///
  /// \return The remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#SymbolName .
  const char *parseIdentifier(OutputBuffer *Demangled, const char *Mangled);

  /// Extract and demangle the plain identifier from a given mangled symbol and
  /// prepend/append it to the output string, with a special treatment for some
  /// magic compiler generated symbols.
  ///
  /// \param Demangled Output buffer to write the demangled name.
  /// \param Mangled Mangled symbol to be demangled.
  /// \param Len Length of the mangled symbol name.
  ///
  /// \return The remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#LName .
  const char *parseLName(OutputBuffer *Demangled, const char *Mangled,
                         unsigned long Len);

  /// Extract and demangle the qualified symbol from a given mangled symbol
  /// append it to the output string.
  ///
  /// \param Demangled Output buffer to write the demangled name.
  /// \param Mangled Mangled symbol to be demangled.
  /// \param SuffixModifiers True if we are printing the modifiers after the
  ///                        symbol, false otherwise.
  ///
  /// \return The remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#QualifiedName .
  const char *parseQualified(OutputBuffer *Demangled, const char *Mangled,
                             bool SuffixModifiers);

  /// Extract and demangle the D function attributes from a given mangled
  /// symbol append it to the output string.
  ///
  /// \param Demangled output buffer to write the demangled name.
  /// \param Mangled mangled symbol to be demangled.
  ///
  /// \return the remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#FuncAttr .
  const char *parseAttributes(OutputBuffer *Demangled, const char *Mangled);

  /// Extract and demangle the function type from a given mangled symbol
  /// without the return type and append it to the arguments, calling
  /// convention and attribute output strings, respectively.
  ///
  /// \param Args output buffer to write the demangled arguments.
  /// \param Call output buffer to write the demangled calling convention.
  /// \param Attr output buffer to write the demangled attributes.
  /// \param Mangled mangled symbol to be demangled.
  ///
  /// \return the remaining string on success or nullptr on failure.
  ///
  /// \note Any of the output strings can be nullptr to throw the information
  ///       away.
  ///
  /// \see https://dlang.org/spec/abi.html#TypeFunctionNoReturn .
  const char *parseFunctionTypeNoreturn(OutputBuffer *Args, OutputBuffer *Call,
                                        OutputBuffer *Attr,
                                        const char *Mangled);

  /// Extract and demangle the function type from a given mangled symbol
  /// append it to the output string.
  ///
  /// \param Demangled output buffer to write the demangled name.
  /// \param Mangled mangled symbol to be demangled.
  ///
  /// \return the remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#TypeFunction .
  const char *parseFunctionType(OutputBuffer *Demangled, const char *Mangled);

  /// Extract and demangle the function arguments list from a given mangled
  /// symbol append it to the output string.
  ///
  /// \param Demangled output buffer to write the demangled name.
  /// \param Mangled mangled symbol to be demangled.
  ///
  /// \return the remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#Parameters .
  const char *parseFunctionArgs(OutputBuffer *Demangled, const char *Mangled);

  /// Extract and demangle a type from a given mangled symbol append it to
  /// the output string.
  ///
  /// \param Demangled output buffer to write the demangled name.
  /// \param Mangled mangled symbol to be demangled.
  ///
  /// \return the remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#Type .
  const char *parseType(OutputBuffer *Demangled, const char *Mangled);

  /// Extract and demangle the type modifiers from a given mangled symbol
  /// append it to the output string.
  ///
  /// \param Demangled output buffer to write the demangled name.
  /// \param Mangled mangled symbol to be demangled.
  ///
  /// \return the remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#TypeModifiers .
  const char *parseTypeModifiers(OutputBuffer *Demangled, const char *Mangled);

  /// Extract and demangle a tuple value from a given mangled symbol append it
  /// to the output string.
  ///
  /// \param Demangled output buffer to write the demangled name.
  /// \param Mangled mangled symbol to be demangled.
  ///
  /// \return the remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#TypeTuple .
  const char *parseTuple(OutputBuffer *Demangled, const char *Mangled);

  /// Extract and demangle the template symbol parameter from a given mangled
  /// symbol append it to the output string
  ///
  /// \param Demangled output buffer to write the demangled name
  /// \param Mangled mangled symbol to be demangled
  ///
  /// \return the remaining string on success or nullptr on failure
  ///
  /// \see https://dlang.org/spec/abi.html#TemplateArgX
  const char *parseTemplateSymbolParameter(OutputBuffer *Demangled,
                                           const char *Mangled);

  /// Extract and demangle the template arguments list from a given mangled
  /// symbol append it to the output string
  ///
  /// \param Demangled output buffer to write the demangled name
  /// \param Mangled mangled symbol to be demangled
  ///
  /// \return the remaining string on success or nullptr on failure
  ///
  /// \see https://dlang.org/spec/abi.html#TemplateArgs
  const char *parseTemplateArgs(OutputBuffer *Demangled, const char *Mangled);

  /// Extract and demangle a template from a given mangled symbol and append it
  /// to the output string
  ///
  /// \param Demangled output buffer to write the demangled name
  /// \param Mangled mangled symbol to be demangled
  /// \param Len expected characters length (default to -1 if unknown length)
  ///
  /// \return the remaining string on success or nullptr on failure
  ///
  /// \see https://dlang.org/spec/abi.html#TemplateInstanceName
  const char *parseTemplate(OutputBuffer *Demangled, const char *Mangled,
                            unsigned long Len = -1);

  /// Extract and demangle an integer value from a given mangled symbol append
  /// it to the output string.
  ///
  /// \param Demangled output buffer to write the demangled name.
  /// \param Mangled mangled symbol to be demangled.
  /// \param Type mangled type character in which the type should be
  ///             represented as.
  ///
  /// \return the remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#Value .
  const char *parseInteger(OutputBuffer *Demangled, const char *Mangled,
                           char Type);

  /// Extract and demangle an array literal value from a given mangled symbol
  /// append it to the output string.
  ///
  /// \param Demangled output buffer to write the demangled name.
  /// \param Mangled mangled symbol to be demangled.
  ///
  /// \return the remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#Value .
  const char *parseArrayLiteral(OutputBuffer *Demangled, const char *Mangled);

  /// Extract and demangle an associative array value from a given mangled
  /// symbol append it to the output string.
  ///
  /// \param Demangled output buffer to write the demangled name.
  /// \param Mangled mangled symbol to be demangled.
  ///
  /// \return the remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#Value .
  const char *parseAssocArray(OutputBuffer *Demangled, const char *Mangled);

  /// Extract and demangle a string value from a given mangled symbol append it
  /// to the output string.
  ///
  /// \param Demangled output buffer to write the demangled name.
  /// \param Mangled mangled symbol to be demangled.
  ///
  /// \return the remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#Value .
  const char *parseString(OutputBuffer *Demangled, const char *Mangled);

  /// Extract and demangle a floating-point value from a given mangled symbol
  /// append it to the output string.
  ///
  /// \param Demangled output buffer to write the demangled name.
  /// \param Mangled mangled symbol to be demangled.
  ///
  /// \return the remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#Value .
  const char *parseReal(OutputBuffer *Demangled, const char *Mangled);

  /// Extract and demangle any value from a given mangled symbol append it to
  /// the output string.
  ///
  /// \param Demangled output buffer to write the demangled name.
  /// \param Mangled mangled symbol to be demangled.
  /// \param Type mangled type character in which the type should be
  ///             represented as, if needed.
  ///
  /// \return the remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#Value .
  const char *parseValue(OutputBuffer *Demangled, const char *Mangled,
                         char Type);

  /// The string we are demangling.
  const char *Str;
  /// The index of the last back reference.
  int LastBackref;
};

} // namespace

const char *Demangler::decodeNumber(const char *Mangled, unsigned long *Ret) {
  // Return nullptr if trying to extract something that isn't a digit.
  if (Mangled == nullptr || !std::isdigit(*Mangled))
    return nullptr;

  unsigned long Val = 0;

  do {
    unsigned long Digit = Mangled[0] - '0';

    // Check for overflow.
    if (Val > (std::numeric_limits<unsigned int>::max() - Digit) / 10)
      return nullptr;

    Val = Val * 10 + Digit;
    ++Mangled;
  } while (std::isdigit(*Mangled));

  if (*Mangled == '\0')
    return nullptr;

  *Ret = Val;
  return Mangled;
}

const char *Demangler::decodeHexdigit(const char *Mangled, char *Ret) {
  char C;

  // Return nullptr if trying to extract something that isn't a hexdigit
  if (Mangled == nullptr || !std::isxdigit(Mangled[0]) ||
      !std::isxdigit(Mangled[1]))
    return nullptr;

  C = Mangled[0];
  if (!std::isdigit(C))
    *Ret = C - (std::isupper(C) ? 'A' : 'a') + 10;
  else
    *Ret = C - '0';

  C = Mangled[1];
  if (!std::isdigit(C))
    *Ret = (*Ret << 4) | (C - (std::isupper(C) ? 'A' : 'a') + 10);
  else
    *Ret = (*Ret << 4) | (C - '0');

  Mangled += 2;

  return Mangled;
}

const char *Demangler::decodeBackrefPos(const char *Mangled, long *Ret) {
  // Return nullptr if trying to extract something that isn't a digit
  if (Mangled == nullptr || !std::isalpha(*Mangled))
    return nullptr;

  // Any identifier or non-basic type that has been emitted to the mangled
  // symbol before will not be emitted again, but is referenced by a special
  // sequence encoding the relative position of the original occurrence in the
  // mangled symbol name.
  // Numbers in back references are encoded with base 26 by upper case letters
  // A-Z for higher digits but lower case letters a-z for the last digit.
  //    NumberBackRef:
  //        [a-z]
  //        [A-Z] NumberBackRef
  //        ^
  unsigned long Val = 0;

  while (std::isalpha(*Mangled)) {
    // Check for overflow
    if (Val > (std::numeric_limits<unsigned long>::max() - 25) / 26)
      break;

    Val *= 26;

    if (Mangled[0] >= 'a' && Mangled[0] <= 'z') {
      Val += Mangled[0] - 'a';
      if ((long)Val <= 0)
        break;
      *Ret = Val;
      return Mangled + 1;
    }

    Val += Mangled[0] - 'A';
    ++Mangled;
  }

  return nullptr;
}

const char *Demangler::decodeBackref(const char *Mangled, const char **Ret) {
  *Ret = nullptr;

  if (Mangled == nullptr || *Mangled != 'Q')
    return nullptr;

  // Position of 'Q'
  const char *Qpos = Mangled;
  long RefPos;
  ++Mangled;

  Mangled = decodeBackrefPos(Mangled, &RefPos);
  if (Mangled == nullptr)
    return nullptr;

  if (RefPos > Qpos - Str)
    return nullptr;

  // Set the position of the back reference.
  *Ret = Qpos - RefPos;

  return Mangled;
}

const char *Demangler::parseSymbolBackref(OutputBuffer *Demangled,
                                          const char *Mangled) {
  // An identifier back reference always points to a digit 0 to 9.
  //    IdentifierBackRef:
  //        Q NumberBackRef
  //        ^
  const char *Backref;
  unsigned long Len;

  // Get position of the back reference
  Mangled = decodeBackref(Mangled, &Backref);

  // Must point to a simple identifier
  Backref = decodeNumber(Backref, &Len);
  if (Backref == nullptr || strlen(Backref) < Len)
    return nullptr;

  Backref = parseLName(Demangled, Backref, Len);
  if (Backref == nullptr)
    return nullptr;

  return Mangled;
}

const char *Demangler::parseTypeBackref(OutputBuffer *Demangled,
                                        const char *Mangled, bool IsFunction) {
  // A type back reference always points to a letter.
  //    TypeBackRef:
  //        Q NumberBackRef
  //        ^
  const char *Backref;

  // If we appear to be moving backwards through the mangle string, then
  // bail as this may be a recursive back reference.
  if (Mangled - Str >= LastBackref)
    return nullptr;

  int SaveRefPos = LastBackref;
  LastBackref = Mangled - Str;

  // Get position of the back reference.
  Mangled = decodeBackref(Mangled, &Backref);

  // Must point to a type.
  if (IsFunction)
    Backref = parseFunctionType(Demangled, Backref);
  else
    Backref = parseType(Demangled, Backref);

  LastBackref = SaveRefPos;

  if (Backref == nullptr)
    return nullptr;

  return Mangled;
}

bool Demangler::isCallConvention(const char *Mangled) {
  switch (*Mangled) {
  case 'F':
  case 'U':
  case 'V':
  case 'W':
  case 'R':
  case 'Y':
    return true;

  default:
    return false;
  }
}

bool Demangler::isSymbolName(const char *Mangled) {
  long Ret;
  const char *Qref = Mangled;

  if (std::isdigit(*Mangled))
    return true;

  if (Mangled[0] == '_' && Mangled[1] == '_' &&
      (Mangled[2] == 'T' || Mangled[2] == 'U'))
    return true;

  if (*Mangled != 'Q')
    return false;

  Mangled = decodeBackrefPos(Mangled + 1, &Ret);
  if (Mangled == nullptr || Ret > Qref - Str)
    return false;

  return std::isdigit(Qref[-Ret]);
}

const char *Demangler::parseCallConvention(OutputBuffer *Demangled,
                                           const char *Mangled) {
  if (Mangled == nullptr || *Mangled == '\0')
    return nullptr;

  switch (*Mangled) {
  case 'F': // D
    ++Mangled;
    break;

  case 'U': // C
    ++Mangled;
    *Demangled << "extern(C) ";
    break;

  case 'W': // Windows
    ++Mangled;
    *Demangled << "extern(Windows) ";
    break;

  case 'V': // Pascal
    ++Mangled;
    *Demangled << "extern(Pascal) ";
    break;

  case 'R': // C++
    ++Mangled;
    *Demangled << "extern(C++) ";
    break;

  case 'Y': // Objective-C
    ++Mangled;
    *Demangled << "extern(Objective-C) ";
    break;

  default:
    return nullptr;
  }

  return Mangled;
}

const char *Demangler::parseAttributes(OutputBuffer *Demangled,
                                       const char *Mangled) {
  if (Mangled == nullptr || *Mangled == '\0')
    return nullptr;

  while (*Mangled == 'N') {
    ++Mangled;
    switch (*Mangled) {
    case 'a': // pure
      ++Mangled;
      *Demangled << "pure ";
      continue;

    case 'b': // nothrow
      ++Mangled;
      *Demangled << "nothrow ";
      continue;

    case 'c': // ref
      ++Mangled;
      *Demangled << "ref ";
      continue;

    case 'd': // @property
      ++Mangled;
      *Demangled << "@property ";
      continue;

    case 'e': // @trusted
      ++Mangled;
      *Demangled << "@trusted ";
      continue;

    case 'f': // @safe
      ++Mangled;
      *Demangled << "@safe ";
      continue;

    case 'g':
    case 'h':
    case 'k':
    case 'n':
      // inout parameter is represented as 'Ng'.
      // vector parameter is represented as 'Nh'.
      // return parameter is represented as 'Nk'.
      // typeof(*null) parameter is represented as 'Nn'.
      // If we see this, then we know we're really in the
      // parameter list. Rewind and break.
      Mangled--;
      break;

    case 'i': // @nogc
      ++Mangled;
      *Demangled << "@nogc ";
      continue;

    case 'j': // return
      ++Mangled;
      *Demangled << "return ";
      continue;

    case 'l': // scope
      ++Mangled;
      *Demangled << "scope ";
      continue;

    case 'm': // @live
      ++Mangled;
      *Demangled << "@live ";
      continue;

    default: // unknown attribute
      return nullptr;
    }
    break;
  }

  return Mangled;
}

const char *Demangler::parseFunctionTypeNoreturn(OutputBuffer *Args,
                                                 OutputBuffer *Call,
                                                 OutputBuffer *Attr,
                                                 const char *Mangled) {
  OutputBuffer Dump;
  if (!initializeOutputBuffer(nullptr, nullptr, Dump, 32))
    return nullptr;

  // Skip over calling convention and attributes
  Mangled = parseCallConvention(Call ? Call : &Dump, Mangled);
  Mangled = parseAttributes(Attr ? Attr : &Dump, Mangled);

  if (Args)
    *Args << '(';

  Mangled = parseFunctionArgs(Args ? Args : &Dump, Mangled);
  if (Args)
    *Args << ')';

  std::free(Dump.getBuffer());
  return Mangled;
}

const char *Demangler::parseFunctionType(OutputBuffer *Demangled,
                                         const char *Mangled) {
  OutputBuffer Attr, Args, Type;

  if (Mangled == nullptr || *Mangled == '\0')
    return nullptr;

  // The order of the mangled string is:
  //    CallConvention FuncAttrs Arguments ArgClose Type
  // The demangled string is re-ordered as:
  //    CallConvention Type Arguments FuncAttrs
  if (!initializeOutputBuffer(nullptr, nullptr, Attr, 32) ||
      !initializeOutputBuffer(nullptr, nullptr, Args, 32) ||
      !initializeOutputBuffer(nullptr, nullptr, Type, 32))
    return nullptr;

  Mangled = parseFunctionTypeNoreturn(&Args, Demangled, &Attr, Mangled);

  // Function return type
  Mangled = parseType(&Type, Mangled);

  // Append to decl in order
  *Demangled << StringView(Type.getBuffer(), Type.getCurrentPosition());
  *Demangled << StringView(Args.getBuffer(), Args.getCurrentPosition());
  *Demangled << ' ';
  *Demangled << StringView(Attr.getBuffer(), Attr.getCurrentPosition());

  std::free(Attr.getBuffer());
  std::free(Args.getBuffer());
  std::free(Type.getBuffer());
  return Mangled;
}

const char *Demangler::parseMangle(OutputBuffer *Demangled,
                                   const char *Mangled) {
  // A D mangled symbol is comprised of both scope and type information.
  //    MangleName:
  //        _D QualifiedName Type
  //        _D QualifiedName Z
  //        ^
  // The caller should have guaranteed that the start pointer is at the
  // above location.
  // Note that type is never a function type, but only the return type of
  // a function or the type of a variable.
  Mangled += 2;

  Mangled = parseQualified(Demangled, Mangled, true);

  if (Mangled != nullptr) {
    // Artificial symbols end with 'Z' and have no type.
    if (*Mangled == 'Z')
      ++Mangled;
    else {
      // Discard the declaration or return type.
      OutputBuffer Type;
      if (!initializeOutputBuffer(nullptr, nullptr, Type, 32))
        return nullptr;

      Mangled = parseType(&Type, Mangled);
      std::free(Type.getBuffer());
    }
  }

  return Mangled;
}

const char *Demangler::parseQualified(OutputBuffer *Demangled,
                                      const char *Mangled,
                                      bool SuffixModifiers) {
  // Qualified names are identifiers separated by their encoded length.
  // Nested functions also encode their argument types without specifying
  // what they return.
  //    QualifiedName:
  //        SymbolFunctionName
  //        SymbolFunctionName QualifiedName
  //        ^
  //    SymbolFunctionName:
  //        SymbolName
  //        SymbolName TypeFunctionNoReturn
  //        SymbolName M TypeFunctionNoReturn
  //        SymbolName M TypeModifiers TypeFunctionNoReturn
  // The start pointer should be at the above location.

  // Whether it has more than one symbol
  size_t NotFirst = false;
  do {
    // Skip over anonymous symbols.
    if (*Mangled == '0') {
      do
        ++Mangled;
      while (*Mangled == '0');

      continue;
    }

    if (NotFirst)
      *Demangled << '.';
    NotFirst = true;

    Mangled = parseIdentifier(Demangled, Mangled);

    if (Mangled && (*Mangled == 'M' || isCallConvention(Mangled))) {
      const char *Start = Mangled;
      int Saved = Demangled->getCurrentPosition();

      // Save the type modifiers for appending at the end if needed.
      OutputBuffer Mods;
      if (!initializeOutputBuffer(nullptr, nullptr, Mods, 32))
        return nullptr;

      // Skip over 'this' parameter and type modifiers.
      if (*Mangled == 'M') {
        ++Mangled;
        Mangled = parseTypeModifiers(&Mods, Mangled);
        Demangled->setCurrentPosition(Saved);
      }

      Mangled = parseFunctionTypeNoreturn(Demangled, nullptr, nullptr, Mangled);
      if (SuffixModifiers)
        *Demangled << StringView(Mods.getBuffer(), Mods.getCurrentPosition());

      if (Mangled == nullptr || *Mangled == '\0') {
        // Did not match the rule we were looking for.
        Mangled = Start;
        Demangled->setCurrentPosition(Saved);
      }

      std::free(Mods.getBuffer());
    }
  } while (Mangled && isSymbolName(Mangled));

  return Mangled;
}

const char *Demangler::parseTypeModifiers(OutputBuffer *Demangled,
                                          const char *Mangled) {
  if (Mangled == nullptr || *Mangled == '\0')
    return nullptr;

  switch (*Mangled) {
  case 'x': // const
    ++Mangled;
    *Demangled << " const";
    return Mangled;

  case 'y': // immutable
    ++Mangled;
    *Demangled << " immutable";
    return Mangled;

  case 'O': // shared
    ++Mangled;
    *Demangled << " shared";
    return parseTypeModifiers(Demangled, Mangled);

  case 'N':
    ++Mangled;
    if (*Mangled == 'g') // wild
    {
      ++Mangled;
      *Demangled << " inout";
      return parseTypeModifiers(Demangled, Mangled);
    }

    return nullptr;

  default:
    return Mangled;
  }
}

const char *Demangler::parseIdentifier(OutputBuffer *Demangled,
                                       const char *Mangled) {
  unsigned long Len;

  if (Mangled == nullptr || *Mangled == '\0')
    return nullptr;

  if (*Mangled == 'Q')
    return parseSymbolBackref(Demangled, Mangled);

  // May be a template instance without a length prefix.
  if (Mangled[0] == '_' && Mangled[1] == '_' &&
      (Mangled[2] == 'T' || Mangled[2] == 'U'))
    return parseTemplate(Demangled, Mangled);

  const char *Endptr = decodeNumber(Mangled, &Len);

  if (Endptr == nullptr || Len == 0)
    return nullptr;

  if (strlen(Endptr) < Len)
    return nullptr;

  Mangled = Endptr;

  // May be a template instance with a length prefix.
  if (Len >= 5 && Mangled[0] == '_' && Mangled[1] == '_' &&
      (Mangled[2] == 'T' || Mangled[2] == 'U'))
    return parseTemplate(Demangled, Mangled, Len);

  // There can be multiple different declarations in the same function that
  // have the same mangled name.  To make the mangled names unique, a fake
  // parent in the form `__Sddd' is added to the symbol.
  if (Len >= 4 && Mangled[0] == '_' && Mangled[1] == '_' && Mangled[2] == 'S') {
    const char *NumPtr = Mangled + 3;
    while (NumPtr < (Mangled + Len) && std::isdigit(*NumPtr))
      ++NumPtr;

    if (Mangled + Len == NumPtr) {
      // Skip over the fake parent.
      Mangled += Len;
      return parseIdentifier(Demangled, Mangled);
    }

    // Else demangle it as a plain identifier.
  }

  return parseLName(Demangled, Mangled, Len);
}

const char *Demangler::parseFunctionArgs(OutputBuffer *Demangled,
                                         const char *Mangled) {
  size_t N = 0;

  while (Mangled && *Mangled != '\0') {
    switch (*Mangled) {
    case 'X': // (variadic T t...) style
      ++Mangled;
      *Demangled << "...";
      return Mangled;
    case 'Y': // (variadic T t, ...) style
      ++Mangled;
      *Demangled << ", ...";
      return Mangled;
    case 'Z': // Normal function
      ++Mangled;
      return Mangled;
    }

    if (N++)
      *Demangled << ", ";

    if (*Mangled == 'M') // scope(T)
    {
      ++Mangled;
      *Demangled << "scope ";
    }

    if (Mangled[0] == 'N' && Mangled[1] == 'k') // return(T)
    {
      Mangled += 2;
      *Demangled << "return ";
    }

    switch (*Mangled) {
    case 'I': // in(T)
      ++Mangled;
      *Demangled << "in ";
      if (*Mangled == 'K') // in ref(T)
      {
        ++Mangled;
        *Demangled << "ref ";
      }
      break;
    case 'J': // out(T)
      ++Mangled;
      *Demangled << "out ";
      break;
    case 'K': // ref(T)
      ++Mangled;
      *Demangled << "ref ";
      break;
    case 'L': // lazy(T)
      ++Mangled;
      *Demangled << "lazy ";
      break;
    }
    Mangled = parseType(Demangled, Mangled);
  }

  return Mangled;
}

const char *Demangler::parseType(OutputBuffer *Demangled, const char *Mangled) {
  if (Mangled == nullptr || *Mangled == '\0')
    return nullptr;

  switch (*Mangled) {
  case 'O': // shared(T)
    ++Mangled;
    *Demangled << "shared(";
    Mangled = parseType(Demangled, Mangled);
    *Demangled << ')';
    return Mangled;

  case 'x': // const(T)
    ++Mangled;
    *Demangled << "const(";
    Mangled = parseType(Demangled, Mangled);
    *Demangled << ')';
    return Mangled;

  case 'y': // immutable(T)
    ++Mangled;
    *Demangled << "immutable(";
    Mangled = parseType(Demangled, Mangled);
    *Demangled << ')';
    return Mangled;

  case 'N':
    ++Mangled;

    switch (*Mangled) {
    case 'g': // wild(T)
      ++Mangled;
      *Demangled << "inout(";
      Mangled = parseType(Demangled, Mangled);
      *Demangled << ')';
      return Mangled;

    case 'h': // vector(T)
      ++Mangled;
      *Demangled << "__vector(";
      Mangled = parseType(Demangled, Mangled);
      *Demangled << ')';
      return Mangled;

    case 'n': // typeof(*null)
      ++Mangled;
      *Demangled << "typeof(*null)";
      return Mangled;
    }

    // invalid.
    return nullptr;

  case 'A': // dynamic array (T[])
    ++Mangled;
    Mangled = parseType(Demangled, Mangled);
    *Demangled << "[]";
    return Mangled;

  case 'G': // static array (T[N])
  {
    const char *NumPtr;
    size_t Num = 0;
    ++Mangled;

    NumPtr = Mangled;
    while (std::isdigit(*Mangled)) {
      ++Num;
      ++Mangled;
    }
    Mangled = parseType(Demangled, Mangled);
    *Demangled << '[';
    *Demangled << StringView(NumPtr, Num);
    *Demangled << ']';
    return Mangled;
  }

  case 'H': // associative array (T[T])
  {
    OutputBuffer Type;
    if (!initializeOutputBuffer(nullptr, nullptr, Type, 32))
      return nullptr;

    size_t Sztype;
    ++Mangled;

    Mangled = parseType(&Type, Mangled);
    Sztype = Type.getCurrentPosition();

    Mangled = parseType(Demangled, Mangled);
    *Demangled << '[';
    *Demangled << StringView(Type.getBuffer(), Sztype);
    *Demangled << ']';

    std::free(Type.getBuffer());
    return Mangled;
  }

  case 'P': // pointer (T*)
    ++Mangled;
    if (!isCallConvention(Mangled)) {
      Mangled = parseType(Demangled, Mangled);
      *Demangled << '*';
      return Mangled;
    }

    [[clang::fallthrough]];
  case 'F': // function T (D)
  case 'U': // function T (C)
  case 'W': // function T (Windows)
  case 'V': // function T (Pascal)
  case 'R': // function T (C++)
  case 'Y': // function T (Objective-C)
    // Function pointer types don't include the trailing asterisk.
    Mangled = parseFunctionType(Demangled, Mangled);
    *Demangled << "function";
    return Mangled;

  case 'C': // class T
  case 'S': // struct T
  case 'E': // enum T
  case 'T': // typedef T
    ++Mangled;
    return parseQualified(Demangled, Mangled, false);

  case 'D': // delegate T
  {
    OutputBuffer Mods;
    if (!initializeOutputBuffer(nullptr, nullptr, Mods, 32))
      return nullptr;

    size_t Szmods;
    ++Mangled;

    Mangled = parseTypeModifiers(&Mods, Mangled);
    Szmods = Mods.getCurrentPosition();

    // Back referenced function type.
    if (Mangled && *Mangled == 'Q')
      Mangled = parseTypeBackref(Demangled, Mangled, true);
    else
      Mangled = parseFunctionType(Demangled, Mangled);

    *Demangled << "delegate";
    *Demangled << StringView(Mods.getBuffer(), Szmods);

    std::free(Mods.getBuffer());
    return Mangled;
  }

  case 'B': // tuple T
    ++Mangled;
    return parseTuple(Demangled, Mangled);

  // Basic types.
  case 'n':
    ++Mangled;
    *Demangled << "typeof(null)";
    return Mangled;

  case 'v':
    ++Mangled;
    *Demangled << "void";
    return Mangled;

  case 'g':
    ++Mangled;
    *Demangled << "byte";
    return Mangled;
  case 'h':
    ++Mangled;
    *Demangled << "ubyte";
    return Mangled;

  case 's':
    ++Mangled;
    *Demangled << "short";
    return Mangled;
  case 't':
    ++Mangled;
    *Demangled << "ushort";
    return Mangled;

  case 'i':
    ++Mangled;
    *Demangled << "int";
    return Mangled;
  case 'k':
    ++Mangled;
    *Demangled << "uint";
    return Mangled;

  case 'l':
    ++Mangled;
    *Demangled << "long";
    return Mangled;
  case 'm':
    ++Mangled;
    *Demangled << "ulong";
    return Mangled;

  case 'f':
    ++Mangled;
    *Demangled << "float";
    return Mangled;
  case 'd':
    ++Mangled;
    *Demangled << "double";
    return Mangled;
  case 'e':
    ++Mangled;
    *Demangled << "real";
    return Mangled;

  // Imaginary types.
  case 'o':
    ++Mangled;
    *Demangled << "ifloat";
    return Mangled;
  case 'p':
    ++Mangled;
    *Demangled << "idouble";
    return Mangled;
  case 'j':
    ++Mangled;
    *Demangled << "ireal";
    return Mangled;

  // Complex types.
  case 'q':
    ++Mangled;
    *Demangled << "cfloat";
    return Mangled;
  case 'r':
    ++Mangled;
    *Demangled << "cdouble";
    return Mangled;
  case 'c':
    ++Mangled;
    *Demangled << "creal";
    return Mangled;

  // Other types.
  case 'b':
    ++Mangled;
    *Demangled << "bool";
    return Mangled;

  case 'a':
    ++Mangled;
    *Demangled << "char";
    return Mangled;
  case 'u':
    ++Mangled;
    *Demangled << "wchar";
    return Mangled;
  case 'w':
    ++Mangled;
    *Demangled << "dchar";
    return Mangled;

  case 'z':
    ++Mangled;

    switch (*Mangled) {
    case 'i':
      ++Mangled;
      *Demangled << "cent";
      return Mangled;
    case 'k':
      ++Mangled;
      *Demangled << "ucent";
      return Mangled;
    }
    return nullptr;

  // Back referenced type.
  case 'Q':
    return parseTypeBackref(Demangled, Mangled, false);

  default: // unhandled.
    return nullptr;
  }
}

const char *Demangler::parseTuple(OutputBuffer *Demangled,
                                  const char *Mangled) {
  unsigned long Elements;

  Mangled = decodeNumber(Mangled, &Elements);
  if (Mangled == nullptr)
    return nullptr;

  *Demangled << "tuple(";

  while (Elements--) {
    Mangled = parseType(Demangled, Mangled);
    if (Mangled == nullptr)
      return nullptr;

    if (Elements != 0)
      *Demangled << ", ";
  }

  *Demangled << ')';
  return Mangled;
}

const char *Demangler::parseLName(OutputBuffer *Demangled, const char *Mangled,
                                  unsigned long Len) {
  switch (Len) {
  case 6:
    if (strncmp(Mangled, "__ctor", Len) == 0) {
      // Constructor symbol for a class/struct.
      *Demangled << "this";
      Mangled += Len;
      return Mangled;
    }
    if (strncmp(Mangled, "__dtor", Len) == 0) {
      // Destructor symbol for a class/struct.
      *Demangled << "~this";
      Mangled += Len;
      return Mangled;
    }
    if (strncmp(Mangled, "__initZ", Len + 1) == 0) {
      // The static initializer for a given symbol.
      Demangled->prepend("initializer for ");
      Demangled->setCurrentPosition(Demangled->getCurrentPosition() - 1);
      Mangled += Len;
      return Mangled;
    }
    if (strncmp(Mangled, "__vtblZ", Len + 1) == 0) {
      // The vtable symbol for a given class.
      Demangled->prepend("vtable for ");
      Demangled->setCurrentPosition(Demangled->getCurrentPosition() - 1);
      Mangled += Len;
      return Mangled;
    }
    break;

  case 7:
    if (strncmp(Mangled, "__ClassZ", Len + 1) == 0) {
      // The classinfo symbol for a given class.
      Demangled->prepend("ClassInfo for ");
      Demangled->setCurrentPosition(Demangled->getCurrentPosition() - 1);
      Mangled += Len;
      return Mangled;
    }
    break;

  case 10:
    if (strncmp(Mangled, "__postblitMFZ", Len + 3) == 0) {
      // Postblit symbol for a struct.
      *Demangled << "this(this)";
      Mangled += Len + 3;
      return Mangled;
    }
    break;

  case 11:
    if (strncmp(Mangled, "__InterfaceZ", Len + 1) == 0) {
      // The interface symbol for a given class.
      Demangled->prepend("Interface for ");
      Demangled->setCurrentPosition(Demangled->getCurrentPosition() - 1);
      Mangled += Len;
      return Mangled;
    }
    break;

  case 12:
    if (strncmp(Mangled, "__ModuleInfoZ", Len + 1) == 0) {
      // The ModuleInfo symbol for a given module.
      Demangled->prepend("ModuleInfo for ");
      Demangled->setCurrentPosition(Demangled->getCurrentPosition() - 1);
      Mangled += Len;
      return Mangled;
    }
    break;
  }

  *Demangled << StringView(Mangled, Len);
  Mangled += Len;

  return Mangled;
}

const char *Demangler::parseTemplateSymbolParameter(OutputBuffer *Demangled,
                                                    const char *Mangled) {
  if (strncmp(Mangled, "_D", 2) == 0 && isSymbolName(Mangled + 2))
    return parseMangle(Demangled, Mangled);

  if (*Mangled == 'Q')
    return parseQualified(Demangled, Mangled, false);

  unsigned long Len;
  const char *EndPtr = decodeNumber(Mangled, &Len);

  if (EndPtr == nullptr || Len == 0)
    return nullptr;

  // In template parameter symbols generated by the frontend up to 2.076,
  // the symbol length is encoded and the first character of the mangled
  // name can be a digit.  This causes ambiguity issues because the digits
  // of the two numbers are adjacent.
  long PtrSize = Len;
  const char *PtrEnd;
  int Saved = Demangled->getCurrentPosition();

  // Work backwards until a match is found.
  for (PtrEnd = EndPtr; EndPtr != nullptr; PtrEnd--) {
    Mangled = PtrEnd;

    // Reached the beginning of the pointer to the name length,
    // try parsing the entire symbol.
    if (PtrSize == 0) {
      PtrSize = Len;
      PtrEnd = EndPtr;
      EndPtr = nullptr;
    }

    // Check whether template parameter is a function with a valid
    // return type or an untyped identifier.
    if (isSymbolName(Mangled))
      Mangled = parseQualified(Demangled, Mangled, false);
    else if (strncmp(Mangled, "_D", 2) == 0 && isSymbolName(Mangled + 2))
      Mangled = parseMangle(Demangled, Mangled);

    // Check for name length mismatch.
    if (Mangled && (EndPtr == nullptr || (Mangled - PtrEnd) == PtrSize))
      return Mangled;

    PtrSize /= 10;
    Demangled->setCurrentPosition(Saved);
  }

  // No match on any combinations.
  return nullptr;
}

const char *Demangler::parseTemplateArgs(OutputBuffer *Demangled,
                                         const char *Mangled) {
  size_t N = 0;

  while (Mangled && *Mangled != '\0') {
    switch (*Mangled) {
    case 'Z': // End of parameter list.
      ++Mangled;
      return Mangled;
    }

    if (N++)
      *Demangled << ", ";

    // Skip over specialised template prefix.
    if (*Mangled == 'H')
      ++Mangled;

    switch (*Mangled) {
    case 'S': // Symbol parameter.
      ++Mangled;
      Mangled = parseTemplateSymbolParameter(Demangled, Mangled);
      break;
    case 'T': // Type parameter.
      ++Mangled;
      Mangled = parseType(Demangled, Mangled);
      break;
    case 'V': // Value parameter.
    {
      OutputBuffer Name;
      char Type;

      // Peek at the type.
      ++Mangled;
      Type = *Mangled;

      if (Type == 'Q') {
        // Value type is a back reference, peek at the real type.
        const char *Backref;
        if (decodeBackref(Mangled, &Backref) == nullptr)
          return nullptr;

        Type = *Backref;
      }

      // In the few instances where the type is actually desired in
      // the output, it should precede the value from dlang_value.
      Name = OutputBuffer();
      if (!initializeOutputBuffer(nullptr, nullptr, Name, 32))
        return nullptr;

      Mangled = parseType(&Name, Mangled);
      Name << '\0';
      Name.setCurrentPosition(Name.getCurrentPosition() - 1);

      Mangled = parseValue(Demangled, Mangled, Type);
      std::free(Name.getBuffer());
      break;
    }

    case 'X': // Externally mangled parameter.
    {
      unsigned long Len;
      const char *EndPtr;

      ++Mangled;
      EndPtr = decodeNumber(Mangled, &Len);
      if (EndPtr == nullptr || strlen(EndPtr) < Len)
        return nullptr;

      *Demangled << StringView(EndPtr, Len);
      Mangled = EndPtr + Len;
      break;
    }
    default:
      return nullptr;
    }
  }

  return Mangled;
}

const char *Demangler::parseTemplate(OutputBuffer *Demangled,
                                     const char *Mangled, unsigned long Len) {
  const char *Start = Mangled;
  OutputBuffer Args;

  // Template instance names have the types and values of its parameters
  // encoded into it.
  //    TemplateInstanceName:
  //        Number __T LName TemplateArgs Z
  //        Number __U LName TemplateArgs Z
  //               ^
  // The start pointer should be at the above location, and LEN should be
  // the value of the decoded number.

  // Template symbol.
  if (!isSymbolName(Mangled + 3) || Mangled[3] == '0')
    return nullptr;

  Mangled += 3;

  // Template identifier.
  Mangled = parseIdentifier(Demangled, Mangled);

  if (!initializeOutputBuffer(nullptr, nullptr, Args, 32))
    return nullptr;

  // Template arguments.
  Mangled = parseTemplateArgs(&Args, Mangled);

  *Demangled << "!(";
  *Demangled << StringView(Args.getBuffer(), Args.getCurrentPosition());
  *Demangled << ')';

  std::free(Args.getBuffer());

  // Check for template name length mismatch.
  if (Len != -1UL && Mangled && (unsigned long)(Mangled - Start) != Len)
    return nullptr;

  return Mangled;
}

const char *Demangler::parseValue(OutputBuffer *Demangled, const char *Mangled,
                                  char Type) {
  if (Mangled == nullptr || *Mangled == '\0')
    return nullptr;

  switch (*Mangled) {
  // Integral values.
  case 'N':
    ++Mangled;
    *Demangled << '-';
    Mangled = parseInteger(Demangled, Mangled, Type);
    break;

  case 'i':
    ++Mangled;

    [[clang::fallthrough]];
    // There really should always be an `i' before encoded numbers, but there
    // wasn't in early versions of D2, so this case range must remain for
    // backwards compatibility.
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
    Mangled = parseInteger(Demangled, Mangled, Type);
    break;

  // Real value.
  case 'e':
    ++Mangled;
    Mangled = parseReal(Demangled, Mangled);
    break;

  // Complex value.
  case 'c':
    ++Mangled;
    Mangled = parseReal(Demangled, Mangled);
    *Demangled << '+';
    if (Mangled == nullptr || *Mangled != 'c')
      return nullptr;
    ++Mangled;
    Mangled = parseReal(Demangled, Mangled);
    *Demangled << 'i';
    break;

  // String values.
  case 'a': // UTF8
  case 'w': // UTF16
  case 'd': // UTF32
    Mangled = parseString(Demangled, Mangled);
    break;

  // Array values.
  case 'A':
    ++Mangled;
    if (Type == 'H')
      Mangled = parseAssocArray(Demangled, Mangled);
    else
      Mangled = parseArrayLiteral(Demangled, Mangled);
    break;

  default:
    return nullptr;
  }

  return Mangled;
}

const char *Demangler::parseString(OutputBuffer *Demangled,
                                   const char *Mangled) {
  char Type = *Mangled;
  unsigned long Len;

  ++Mangled;
  Mangled = decodeNumber(Mangled, &Len);
  if (Mangled == nullptr || *Mangled != '_')
    return nullptr;

  ++Mangled;
  *Demangled << '\"';
  while (Len--) {
    char Val;
    const char *Endptr = decodeHexdigit(Mangled, &Val);

    if (Endptr == nullptr)
      return nullptr;

    // Sanitize white and non-printable characters.
    switch (Val) {
    case ' ':
      *Demangled << ' ';
      break;
    case '\t':
      *Demangled << "\\t";
      break;
    case '\n':
      *Demangled << "\\n";
      break;
    case '\r':
      *Demangled << "\\r";
      break;
    case '\f':
      *Demangled << "\\f";
      break;
    case '\v':
      *Demangled << "\\v";
      break;

    default:
      if (std::isprint(Val))
        *Demangled << Val;
      else {
        *Demangled << "\\x";
        *Demangled << StringView(Mangled, 2);
      }
    }

    Mangled = Endptr;
  }
  *Demangled << '\"';

  if (Type != 'a')
    *Demangled << Type;

  return Mangled;
}

const char *Demangler::parseInteger(OutputBuffer *Demangled,
                                    const char *Mangled, char Type) {
  if (Type == 'a' || Type == 'u' || Type == 'w') {
    // Parse character value
    char Value[20];
    int Pos = sizeof(Value);
    int Width = 0;
    unsigned long Val;

    Mangled = decodeNumber(Mangled, &Val);
    if (Mangled == nullptr)
      return nullptr;

    *Demangled << '\'';

    if (Type == 'a' && Val >= 0x20 && Val < 0x7F) {
      // Represent as a character literal
      *Demangled << static_cast<char>(Val);
    } else {
      // Represent as a hexadecimal value
      switch (Type) {
      case 'a': // char
        *Demangled << "\\x";
        Width = 2;
        break;
      case 'u': // wchar
        *Demangled << "\\u";
        Width = 4;
        break;
      case 'w': // dchar
        *Demangled << "\\U";
        Width = 8;
        break;
      }

      while (Val > 0) {
        int Digit = Val % 16;

        if (Digit < 10)
          Value[--Pos] = (char)(Digit + '0');
        else
          Value[--Pos] = (char)((Digit - 10) + 'a');

        Val /= 16;
        Width--;
      }

      for (; Width > 0; Width--)
        Value[--Pos] = '0';

      *Demangled << StringView(&(Value[Pos]), sizeof(Value) - Pos);
    }
    *Demangled << '\'';
  } else if (Type == 'b') {
    // Parse boolean value
    unsigned long Val;

    Mangled = decodeNumber(Mangled, &Val);
    if (Mangled == nullptr)
      return nullptr;

    *Demangled << (Val ? "true" : "false");
  } else {
    // Parse integer value
    const char *NumPtr = Mangled;
    size_t Num = 0;

    if (!std::isdigit(*Mangled))
      return nullptr;

    while (std::isdigit(*Mangled)) {
      ++Num;
      ++Mangled;
    }
    *Demangled << StringView(NumPtr, Num);

    // Append suffix
    switch (Type) {
    case 'h': // ubyte
    case 't': // ushort
    case 'k': // uint
      *Demangled << 'u';
      break;
    case 'l': // long
      *Demangled << 'L';
      break;
    case 'm': // ulong
      *Demangled << "uL";
      break;
    }
  }

  return Mangled;
}

const char *Demangler::parseReal(OutputBuffer *Demangled, const char *Mangled) {
  // Handle NAN and +-INF.
  if (strncmp(Mangled, "NAN", 3) == 0) {
    *Demangled << "NaN";
    Mangled += 3;
    return Mangled;
  }

  if (strncmp(Mangled, "INF", 3) == 0) {
    *Demangled << "Inf";
    Mangled += 3;
    return Mangled;
  }

  if (strncmp(Mangled, "NINF", 4) == 0) {
    *Demangled << "-Inf";
    Mangled += 4;
    return Mangled;
  }

  // Hexadecimal prefix and leading bit.
  if (*Mangled == 'N') {
    *Demangled << '-';
    ++Mangled;
  }

  if (!std::isxdigit(*Mangled))
    return nullptr;

  *Demangled << "0x";
  *Demangled << StringView(Mangled, 1);
  *Demangled << '.';
  ++Mangled;

  // Significand.
  while (std::isxdigit(*Mangled)) {
    *Demangled << StringView(Mangled, 1);
    ++Mangled;
  }

  // Exponent.
  if (*Mangled != 'P')
    return nullptr;

  *Demangled << 'p';
  ++Mangled;

  if (*Mangled == 'N') {
    *Demangled << '-';
    ++Mangled;
  }

  while (std::isdigit(*Mangled)) {
    *Demangled << StringView(Mangled, 1);
    ++Mangled;
  }

  return Mangled;
}

const char *Demangler::parseArrayLiteral(OutputBuffer *Demangled,
                                         const char *Mangled) {
  unsigned long Elements;

  Mangled = decodeNumber(Mangled, &Elements);
  if (Mangled == nullptr)
    return nullptr;

  *Demangled << '[';
  while (Elements--) {
    Mangled = parseValue(Demangled, Mangled, '\0');
    if (Mangled == nullptr)
      return nullptr;

    if (Elements != 0)
      *Demangled << ", ";
  }

  *Demangled << ']';
  return Mangled;
}

const char *Demangler::parseAssocArray(OutputBuffer *Demangled,
                                       const char *Mangled) {
  unsigned long Elements;

  Mangled = decodeNumber(Mangled, &Elements);
  if (Mangled == nullptr)
    return nullptr;

  *Demangled << '[';
  while (Elements--) {
    Mangled = parseValue(Demangled, Mangled, '\0');
    if (Mangled == nullptr)
      return nullptr;

    *Demangled << ':';
    Mangled = parseValue(Demangled, Mangled, '\0');
    if (Mangled == nullptr)
      return nullptr;

    if (Elements != 0)
      *Demangled << ", ";
  }

  *Demangled << ']';
  return Mangled;
}

Demangler::Demangler(const char *Mangled)
    : Str(Mangled), LastBackref(strlen(Mangled)) {}

const char *Demangler::parseMangle(OutputBuffer *Demangled) {
  return parseMangle(Demangled, this->Str);
}

char *llvm::dlangDemangle(const char *MangledName) {
  if (MangledName == nullptr || strncmp(MangledName, "_D", 2) != 0)
    return nullptr;

  OutputBuffer Demangled;
  if (!initializeOutputBuffer(nullptr, nullptr, Demangled, 1024))
    return nullptr;

  if (strcmp(MangledName, "_Dmain") == 0) {
    Demangled << "D main";
  } else {

    Demangler D = Demangler(MangledName);
    MangledName = D.parseMangle(&Demangled);

    // Check that the entire symbol was successfully demangled.
    if (MangledName == nullptr || *MangledName != '\0') {
      std::free(Demangled.getBuffer());
      return nullptr;
    }
  }

  // OutputBuffer's internal buffer is not null terminated and therefore we need
  // to add it to comply with C null terminated strings.
  if (Demangled.getCurrentPosition() > 0) {
    Demangled << '\0';
    Demangled.setCurrentPosition(Demangled.getCurrentPosition() - 1);
    return Demangled.getBuffer();
  }

  std::free(Demangled.getBuffer());
  return nullptr;
}
