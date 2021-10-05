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
  ///
  /// \return The remaining string on success or nullptr on failure.
  ///
  /// \see https://dlang.org/spec/abi.html#QualifiedName .
  const char *parseQualified(OutputBuffer *Demangled, const char *Mangled);

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

  /// The string we are demangling.
  const char *Str;
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

bool Demangler::isSymbolName(const char *Mangled) {
  if (std::isdigit(*Mangled))
    return true;

  // TODO: Handle symbol back references and template instances.
  return false;
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

  Mangled = parseQualified(Demangled, Mangled);

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
                                      const char *Mangled) {
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

  } while (Mangled && isSymbolName(Mangled));

  return Mangled;
}

const char *Demangler::parseIdentifier(OutputBuffer *Demangled,
                                       const char *Mangled) {
  unsigned long Len;

  if (Mangled == nullptr || *Mangled == '\0')
    return nullptr;

  // TODO: Parse back references and lengthless template instances.

  const char *Endptr = decodeNumber(Mangled, &Len);

  if (Endptr == nullptr || Len == 0)
    return nullptr;

  if (strlen(Endptr) < Len)
    return nullptr;

  Mangled = Endptr;

  // TODO: Parse template instances with a length prefix.

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

    // TODO: Parse function types.

    [[clang::fallthrough]];
  case 'C': // class T
  case 'S': // struct T
  case 'E': // enum T
  case 'T': // typedef T
    ++Mangled;
    // TODO: Handle type modifiers and type functions in qualifiers.
    return parseQualified(Demangled, Mangled);

  // TODO: Parse delegate types.
  // TODO: Parse tuple types.

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

    // TODO: Parse back referenced types.

  default: // unhandled.
    return nullptr;
  }
}

const char *Demangler::parseLName(OutputBuffer *Demangled, const char *Mangled,
                                  unsigned long Len) {
  switch (Len) {
  case 6:
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

Demangler::Demangler(const char *Mangled) : Str(Mangled) {}

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
