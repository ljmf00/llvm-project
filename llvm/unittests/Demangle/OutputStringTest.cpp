//===- llvm/unittest/OutputStreamTest.cpp - OutputStream unit tests -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Demangle/MicrosoftDemangleNodes.h"
#include "llvm/Demangle/Utility.h"
#include "gtest/gtest.h"
#include <string>

using namespace llvm;
using llvm::itanium_demangle::OutputString;

static std::string toString(OutputString &OS) {
  return {OS.getBuffer(), OS.getCurrentPosition()};
}

template <typename T> static std::string printToString(const T &Value) {
  OutputString OS;
  OS << Value;
  std::string s = toString(OS);
  std::free(OS.getBuffer());
  return s;
}

TEST(OutputStringTest, Format) {
  EXPECT_EQ("0", printToString(0));
  EXPECT_EQ("1", printToString(1));
  EXPECT_EQ("-1", printToString(-1));
  EXPECT_EQ("-90", printToString(-90));
  EXPECT_EQ("109", printToString(109));
  EXPECT_EQ("400", printToString(400));

  EXPECT_EQ("a", printToString('a'));
  EXPECT_EQ("?", printToString('?'));

  EXPECT_EQ("abc", printToString("abc"));
}

TEST(OutputStringTest, Insert) {
  OutputString OS;

  OS.insert(0, "", 0);
  EXPECT_EQ("", toString(OS));

  OS.insert(0, "abcd", 4);
  EXPECT_EQ("abcd", toString(OS));

  OS.insert(0, "x", 1);
  EXPECT_EQ("xabcd", toString(OS));

  OS.insert(5, "y", 1);
  EXPECT_EQ("xabcdy", toString(OS));

  OS.insert(3, "defghi", 6);
  EXPECT_EQ("xabdefghicdy", toString(OS));

  std::free(OS.getBuffer());
}

TEST(OutputStringTest, Prepend)
{
  OutputString OS;

  OS << "abc";
  OS.prepend("def");
  EXPECT_EQ("defabc", toString(OS));
}
