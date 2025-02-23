// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include "garnet/bin/zxdb/expr/expr_parser.h"
#include "garnet/bin/zxdb/expr/expr_tokenizer.h"
#include "gtest/gtest.h"

namespace zxdb {

class ExprParserTest : public testing::Test {
 public:
  ExprParserTest() = default;

  // Valid after Parse() is called.
  ExprParser& parser() { return *parser_; }

  fxl::RefPtr<ExprNode> Parse(const char* input) {
    parser_.reset();

    tokenizer_ = std::make_unique<ExprTokenizer>(input);
    if (!tokenizer_->Tokenize()) {
      ADD_FAILURE() << "Tokenization failure: " << input;
      return nullptr;
    }

    parser_ = std::make_unique<ExprParser>(tokenizer_->TakeTokens());
    return parser_->Parse();
  }

  // Does the parse and returns the string dump of the structure.
  std::string GetParseString(const char* input) {
    auto root = Parse(input);
    if (!root) {
      // Expect calls to this to parse successfully.
      if (parser_.get())
        ADD_FAILURE() << "Parse failure: " << parser_->err().msg();
      return std::string();
    }

    std::ostringstream out;
    root->Print(out, 0);
    return out.str();
  }

 private:
  std::unique_ptr<ExprTokenizer> tokenizer_;
  std::unique_ptr<ExprParser> parser_;
};

TEST_F(ExprParserTest, Empty) {
  auto result = Parse("");
  ASSERT_FALSE(result);
  EXPECT_EQ("No input to parse.", parser().err().msg());
}

TEST_F(ExprParserTest, Identifier) {
  auto result = Parse("name");
  ASSERT_TRUE(result);

  const IdentifierExprNode* ident = result->AsIdentifier();
  ASSERT_TRUE(ident);
  EXPECT_EQ("name", ident->ident().GetFullName());
}

TEST_F(ExprParserTest, Dot) {
  auto result = Parse("base.member");
  ASSERT_TRUE(result);

  const MemberAccessExprNode* access = result->AsMemberAccess();
  ASSERT_TRUE(access);
  EXPECT_EQ(ExprToken::kDot, access->accessor().type());
  EXPECT_EQ(".", access->accessor().value());

  // Left side is the "base" identifier.
  const IdentifierExprNode* base = access->left()->AsIdentifier();
  ASSERT_TRUE(base);
  EXPECT_EQ("base", base->ident().GetFullName());

  // Member name.
  EXPECT_EQ("member", access->member().GetFullName());
}

TEST_F(ExprParserTest, AccessorAtEnd) {
  auto result = Parse("base. ");
  ASSERT_FALSE(result);

  EXPECT_EQ("Expected identifier for right-hand-side of \".\".",
            parser().err().msg());

  EXPECT_EQ(4u, parser().error_token().byte_offset());
  EXPECT_EQ(".", parser().error_token().value());
}

TEST_F(ExprParserTest, BadAccessorMemberName) {
  auto result = Parse("base->23");
  ASSERT_FALSE(result);

  EXPECT_EQ("Expected identifier for right-hand-side of \"->\".",
            parser().err().msg());

  // This error reports the "->" as the location, one could also imagine
  // reporting the right-side token (if any) instead.
  EXPECT_EQ(4u, parser().error_token().byte_offset());
  EXPECT_EQ("->", parser().error_token().value());
}

TEST_F(ExprParserTest, Arrow) {
  auto result = Parse("base->member");
  ASSERT_TRUE(result);

  const MemberAccessExprNode* access = result->AsMemberAccess();
  ASSERT_TRUE(access);
  EXPECT_EQ(ExprToken::kArrow, access->accessor().type());
  EXPECT_EQ("->", access->accessor().value());

  // Left side is the "base" identifier.
  const IdentifierExprNode* base = access->left()->AsIdentifier();
  ASSERT_TRUE(base);
  EXPECT_EQ("base", base->ident().GetFullName());

  // Member name.
  EXPECT_EQ("member", access->member().GetFullName());

  // Arrow with no name.
  result = Parse("base->");
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected identifier for right-hand-side of \"->\".",
            parser().err().msg());
}

TEST_F(ExprParserTest, NestedDotArrow) {
  // These should be left-associative so do the "." first, then the "->".
  // When evaluating the tree, one first evaluates the left side of the
  // accessor, then the right, which is why it looks backwards in the dump.
  EXPECT_EQ(
      "ACCESSOR(->)\n"
      " ACCESSOR(.)\n"
      "  IDENTIFIER(\"foo\")\n"
      "  bar\n"
      " baz\n",
      GetParseString("foo.bar->baz"));
}

TEST_F(ExprParserTest, UnexpectedInput) {
  auto result = Parse("foo 5");
  ASSERT_FALSE(result);

  EXPECT_EQ("Unexpected input, did you forget an operator?",
            parser().err().msg());
  EXPECT_EQ(4u, parser().error_token().byte_offset());
}

TEST_F(ExprParserTest, ArrayAccess) {
  EXPECT_EQ(
      "ARRAY_ACCESS\n"
      " ARRAY_ACCESS\n"
      "  ACCESSOR(->)\n"
      "   ACCESSOR(.)\n"
      "    IDENTIFIER(\"foo\")\n"
      "    bar\n"
      "   baz\n"
      "  LITERAL(34)\n"
      " IDENTIFIER(\"bar\")\n",
      GetParseString("foo.bar->baz[34][bar]"));

  // Empty array access is an error.
  auto result = Parse("foo[]");
  ASSERT_FALSE(result);
  EXPECT_EQ("Unexpected token ']'.", parser().err().msg());
}

TEST_F(ExprParserTest, DereferenceAndAddress) {
  EXPECT_EQ(
      "DEREFERENCE\n"
      " IDENTIFIER(\"foo\")\n",
      GetParseString("*foo"));

  EXPECT_EQ(
      "ADDRESS_OF\n"
      " IDENTIFIER(\"foo\")\n",
      GetParseString("&foo"));

  // "*" and "&" should be right-associative with respect to each other but
  // lower precedence than -> and [].
  EXPECT_EQ(
      "ADDRESS_OF\n"
      " DEREFERENCE\n"
      "  ADDRESS_OF\n"
      "   ARRAY_ACCESS\n"
      "    ACCESSOR(->)\n"
      "     IDENTIFIER(\"foo\")\n"
      "     bar\n"
      "    LITERAL(1)\n",
      GetParseString("&*&foo->bar[1]"));

  // "*" by itself is an error.
  auto result = Parse("*");
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected expression for '*'.", parser().err().msg());
  EXPECT_EQ(0u, parser().error_token().byte_offset());
}

// () should override the default precedence of other operators.
TEST_F(ExprParserTest, Parens) {
  EXPECT_EQ(
      "ADDRESS_OF\n"
      " ARRAY_ACCESS\n"
      "  DEREFERENCE\n"
      "   ACCESSOR(->)\n"
      "    ADDRESS_OF\n"
      "     IDENTIFIER(\"foo\")\n"
      "    bar\n"
      "  LITERAL(1)\n",
      GetParseString("(&(*(&foo)->bar)[1])"));
}

TEST_F(ExprParserTest, UnaryMath) {
  EXPECT_EQ(
      "UNARY(-)\n"
      " LITERAL(5)\n",
      GetParseString("-5"));
  EXPECT_EQ(
      "UNARY(-)\n"
      " LITERAL(5)\n",
      GetParseString(" - 5 "));

  EXPECT_EQ(
      "UNARY(-)\n"
      " DEREFERENCE\n"
      "  IDENTIFIER(\"foo\")\n",
      GetParseString("-*foo"));

  // "-" by itself is an error.
  auto result = Parse("-");
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected expression for '-'.", parser().err().msg());
  EXPECT_EQ(0u, parser().error_token().byte_offset());
}

TEST_F(ExprParserTest, Identifiers) {
  EXPECT_EQ("IDENTIFIER(\"foo\")\n", GetParseString("foo"));
  EXPECT_EQ("IDENTIFIER(::,\"foo\")\n", GetParseString("::foo"));
  EXPECT_EQ("IDENTIFIER(::,\"foo\"; ::,\"bar\")\n",
            GetParseString("::foo :: bar"));

  auto result = Parse("::");
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected identifier after '::'.", parser().err().msg());

  result = Parse(":: :: name");
  ASSERT_FALSE(result);
  EXPECT_EQ("Duplicate '::'.", parser().err().msg());

  result = Parse("foo bar");
  ASSERT_FALSE(result);
  EXPECT_EQ("Unexpected input, did you forget an operator?",
            parser().err().msg());

  // It's valid to have identifiers with colons in them to access class members
  // (this is how you provide an explicit base class).
  /* TODO(brettw) convert an accessor to use an Identifier for the name so this
     test works.
  EXPECT_EQ(
      "ACCESSOR(->)\n"
      "  IDENTIFIER(\"foo\")\n",
      GetParseString("foo->Baz::bar"));
  */
}

TEST_F(ExprParserTest, Templates) {
  EXPECT_EQ(R"(IDENTIFIER("foo",<>))"
            "\n",
            GetParseString("foo<>"));
  EXPECT_EQ(R"(IDENTIFIER("foo",<"Foo">))"
            "\n",
            GetParseString("foo<Foo>"));
  EXPECT_EQ(R"(IDENTIFIER("foo",<"Foo", "5">))"
            "\n",
            GetParseString("foo< Foo,5 >"));
  EXPECT_EQ(
      R"(IDENTIFIER("std"; ::,"map",<"Key", "Value", "std::less<Key>", "std::allocator<std::pair<Key const, Value>>">; ::,"insert"))"
      "\n",
      GetParseString("std::map<Key, Value, std::less<Key>, "
                     "std::allocator<std::pair<Key const, Value>>>::insert"));

  // Unmatched "<" error. This is generated by the parser because it's
  // expecting the match for the outer level.
  auto result = Parse("std::map<Key, Value");
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected '>' to match. Hit the end of input instead.",
            parser().err().msg());

  // This unmatched token is generated by the template type skipper which is
  // why the error message is slightly different (both are OK).
  result = Parse("std::map<key[, value");
  ASSERT_FALSE(result);
  EXPECT_EQ("Unmatched '['.", parser().err().msg());

  // Duplicate template spec.
  result = Parse("Foo<Bar><Baz>");
  ASSERT_FALSE(result);
  EXPECT_EQ("Duplicate template specification.", parser().err().msg());
}

TEST_F(ExprParserTest, BinaryOp) {
  EXPECT_EQ(
      "BINARY_OP(=)\n"
      " IDENTIFIER(\"a\")\n"
      " LITERAL(23)\n",
      GetParseString("a = 23"));
}

}  // namespace zxdb
