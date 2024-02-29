//===- mdl_lexer.cpp - Lexical analyser for MDL files -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Lexer for the machine description language.
//
//===----------------------------------------------------------------------===//

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ostream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "llvm/Support/Error.h"

#include "mdl.h"
#include "mdl_parser.h"

namespace mpact {
namespace mdl {

//-----------------------------------------------------------------------------
// Read the next line from the input file.  Return true if you find one.
//-----------------------------------------------------------------------------
bool MdlLexer::getLine() {
  if (Stream) {
    std::getline(Stream, CurrentLine);
    LineNumber++;
    ColumnNumber = 0;
    return true;
  }
  return false;
}

//-----------------------------------------------------------------------------
// Get the next token from the current input stream,
// If there are items in the lookahead queue, pop and use the first entry.
// Return the type of the token.
//-----------------------------------------------------------------------------
void MdlLexer::getNext(TokenType &Token, bool Peeking /* = false */) {
  // Return first item from the lookahead queue, if not empty.
  if (!Peeking && !Tokens.empty()) {
    Token = Tokens.front();
    Tokens.pop();
    return;
  }

  // Skip over any blank spaces.
  if (!Stream || !skipSpace()) {
    Token.Type = TK_ENDOFFILE;
    return;
  }

  // Initialize a new token.
  Token.setLocation(FileName, LineNumber, ColumnNumber);
  auto *LinePtr = CurrentLine.c_str();
  Token.Text = std::string(1, LinePtr[ColumnNumber]);

  char ch = LinePtr[ColumnNumber++];
  switch (ch) {
    case ';': Token.Type = TK_SEMI;  return;
    case ',': Token.Type = TK_COMMA; return;
    case ':': Token.Type = TK_COLON; return;
    case '(': Token.Type = TK_LPAREN; return;
    case ')': Token.Type = TK_RPAREN; return;
    case '[': Token.Type = TK_LBRACKET; return;
    case ']': Token.Type = TK_RBRACKET; return;
    case '<': Token.Type = TK_LT; return;
    case '>': Token.Type = TK_GT; return;
    case '=': Token.Type = TK_EQUAL; return;
    case '&': Token.Type = TK_AND; return;
    case '|': Token.Type = TK_OR; return;
    case '*': Token.Type = TK_MUL; return;
    case '+': Token.Type = TK_PLUS; return;
    case '-': Token.Type = TK_MINUS;
              if (LinePtr[ColumnNumber] == '>') {
                Token.Type = TK_ARROW;
                ColumnNumber++;
              }
              return;
    case '/': Token.Type = TK_DIV;
              if (LinePtr[ColumnNumber] != '/')
                 return;
              // Handle comments (like this one)
              if (!getLine()) break;
              return getNext(Token, Peeking);
    case '$': Token.Type = TK_DOLLAR;
              if (LinePtr[ColumnNumber] == '$') {
                Token.Type = TK_2DOLLAR; ColumnNumber++;
              }
              return;
    case '.': Token.Type = TK_DOT;
              if (LinePtr[ColumnNumber] == '.') {
                Token.Type = TK_DOTDOT; ColumnNumber++;
              }
              if (auto ch = LinePtr[ColumnNumber]; ch == '.') {
                Token.Type = TK_ELLIPSIS; ColumnNumber++;
              }
              return;
    case '{': Token.Type = TK_LBRACE;
              if (LinePtr[ColumnNumber] == '{') {
                Token.Type = TK_2LBRACE; ColumnNumber++;
              }
              return;
    case '}': Token.Type = TK_RBRACE;
              if (LinePtr[ColumnNumber] == '}') {
                Token.Type = TK_2RBRACE; ColumnNumber++;
              }
              return;

    case '"': return getString(Token);

    default: break;
  }

  // Parse decimal numbers
  if (std::isdigit(ch)) return getNumber(Token);

  // Parse Identifiers and look up keywords.
  if (std::isalpha(ch) || ch == '_') return getIdent(Token);
}

//-----------------------------------------------------------------------------
// Skip over any blank space until you find a Token or EOF.
//-----------------------------------------------------------------------------
bool MdlLexer::skipSpace() {
  auto *LinePtr = CurrentLine.c_str() + ColumnNumber;
  while (std::isspace(*LinePtr)) { LinePtr++; ColumnNumber++; }

  if (*LinePtr == 0) {
    if (!getLine()) return false;
    return skipSpace();
  }
  return true;
}

//-----------------------------------------------------------------------------
// Scan in Decimal, Hex, Octal or Binary numbers.
//-----------------------------------------------------------------------------
void MdlLexer::getNumber(TokenType &Token) {
  auto *LinePtr = CurrentLine.c_str() + ColumnNumber - 1;
  char *EndPtr = nullptr;

  Token.Type = TK_NUMBER;

  // Look for radix prefixes (0x, 0b, 0)
  int Base = 10;
  if (LinePtr[0] == '0') {
    if (LinePtr[1] == 'x') Base = 16;
    else if (LinePtr[1] == '0' && LinePtr[1] <= '7') Base = 8;
    else if (LinePtr[1] == 'b') { Base = 2; LinePtr += 2; }
  }

  // Convert the number in the appropriate radix.
  Token.Value = strtoul(LinePtr, &EndPtr, Base);
  if (EndPtr == LinePtr || errno == ERANGE)
    Error("Invalid number");

  // Copy the text of the number to the token
  Token.Text = CurrentLine.substr(
                  ColumnNumber - 1, ColumnNumber + (EndPtr - LinePtr));
  ColumnNumber += EndPtr - LinePtr - 1;
}

//-----------------------------------------------------------------------------
// Scan in an identifier.
//-----------------------------------------------------------------------------
void MdlLexer::getIdent(TokenType &Token) {
  Token.Type = TK_IDENT;

  auto *LinePtr = CurrentLine.c_str() + ColumnNumber;
  for (; std::isalnum(*LinePtr) || *LinePtr == '_'; ColumnNumber++)
    Token.Text += std::string(1, *LinePtr++);
  checkKeywords(Token);
}

//-----------------------------------------------------------------------------
// Scan in a string literal
// This is a pretty limited string - no escape characters, no newlines.
// We strip the quotes here (rather than everywhere else).
//-----------------------------------------------------------------------------
void MdlLexer::getString(TokenType &Token) {
  Token.Type = TK_STRING;
  auto *LinePtr = CurrentLine.c_str();
  Token.Text = "";
  while (auto ch = LinePtr[ColumnNumber++]) {
    if (ch == '\n' || ch == '"') break;
    Token.Text += std::string(1, ch);
  }
  if (LinePtr[ColumnNumber - 1] != '"')
    Error(Token, "Missing end quote");
}

//-----------------------------------------------------------------------------
// Scan a code escape object. This is a string of the form:
//       '[{' <arbitrary C code> '}]'
// The parser will have already checked the '[' '{', but not accepted them.
// We limit these to a single physical line, and expect the brackets/braces
// are adjacent (and in the same file), to avoid parsing problems.
//-----------------------------------------------------------------------------
bool MdlLexer::getCodeEscape() {
  // First make sure the leading brackets/braces are adjacent.
  if (Token.getLine() != Tokens.front().getLine() ||
      Token.getColumn() != Tokens.front().getColumn() - 1 ||
      Token.getFileName() != Tokens.front().getFileName())
    return false;

  // Scan current line until we find an '}]'.
  std::string code;
  auto *LinePtr = CurrentLine.c_str();
  while (auto ch = LinePtr[ColumnNumber++]) {
    if (ch == '}' && LinePtr[ColumnNumber] == ']') {
      Tokens.pop();         // discard the peeked-at '}'
      ColumnNumber++;      // Skip the ']'
      Token.Text = code;    // replace the current token
      return true;
      break;
    }
    code += std::string(1, ch);
  }
  if (LinePtr[ColumnNumber] != ']') return false;

  Tokens.pop();         // discard the peeked-at '}'
  Token.Text = code;    // replace the current token
  return true;
}

std::map<TokenKind, std::string> TokenNames = {
  { TK_FAMILY, "family" },
  { TK_PIPE_PHASES, "phases" },
  { TK_PROTECTED, "protected" },
  { TK_UNPROTECTED, "unprotected" },
  { TK_RESOURCE, "resource" },
  { TK_HARD, "hard" },
  { TK_ISSUE, "issue" },
  { TK_CPU, "cpu" },
  { TK_CLUSTER, "cluster" },
  { TK_FUNCUNIT, "func_unit" },
  { TK_FUNCGROUP, "func_group" },
  { TK_SUBUNIT, "subunit" },
  { TK_LATENCY, "latency" },
  { TK_REGISTER, "register" },
  { TK_REGCLASS, "register_class" },
  { TK_INSTRUCT, "instruction" },
  { TK_OPERAND, "operand" },
  { TK_PREDICATE, "predicate" },
  { TK_IMPORT, "import" },
  { TK_ATTRIBUTE, "attribute" },
  { TK_TYPE, "type" },
  { TK_DERIVED, "derived" },
  { TK_REORDER_BUFFER, "reorder_buffer" },
  { TK_FORWARD, "forward" },
  { TK_PORT, "port" },
  { TK_CLASS, "class" },
  { TK_CONNECT, "connect" },
  { TK_TO, "to" },
  { TK_VIA, "via" },

  { TK_BEGINGROUP, "BeginGroup" },
  { TK_ENDGROUP, "EndGroup" },
  { TK_SINGLEISSUE, "SingleIssue" },
  { TK_RETIREOOO, "RetireOOO" },

  { TK_USE, "use" },
  { TK_DEF, "def" },
  { TK_KILL, "kill" },
  { TK_RES, "res",  },
  { TK_HOLD, "hold" },
  { TK_HOLD, "fus" },
  { TK_USEDEF, "usedef" },
  { TK_IF, "if" },
  { TK_ELSE, "else" },

  { TK_NONE, "<none>" },
  { TK_ENDOFFILE, "<end-of-file>" },
  { TK_IDENT, "<Identifier>" },
  { TK_NUMBER, "<Number>" },
  { TK_STRING, "<String>" },

  { TK_SEMI, "';'"  },
  { TK_COLON, "':'" },
  { TK_SHARP, "'#'" },
  { TK_COMMA, "','" },
  { TK_LPAREN, "'('" },
  { TK_RPAREN, "')'" },
  { TK_LBRACE, "'{'" },
  { TK_RBRACE, "'}'" },
  { TK_2LBRACE, "'{{'" },
  { TK_2RBRACE, "'}}'" },
  { TK_LBRACKET, "'['" },
  { TK_RBRACKET, "']'" },
  { TK_LT, "'<'" },
  { TK_GT, "'>'" },
  { TK_EQUAL, "'='" },
  { TK_AND, "'&'" },
  { TK_OR, "'|'" },
  { TK_PLUS, "'+'" },
  { TK_MINUS, "'-'" },
  { TK_MUL, "'*'" },
  { TK_DIV, "'/'" },
  { TK_ARROW, "'->'" },
  { TK_DOLLAR, "'$'" },
  { TK_2DOLLAR, "'$$'" },
  { TK_DOT, "'.'" },
  { TK_DOTDOT, "'..'" },
  { TK_ELLIPSIS, "'...'" },
};

//-----------------------------------------------------------------------------
// Check an identifier to see if its a keyword.
//-----------------------------------------------------------------------------
std::map<std::string, TokenKind> Keywords = {
  { "family", TK_FAMILY },
  { "phases", TK_PIPE_PHASES },
  { "protected", TK_PROTECTED },
  { "unprotected", TK_UNPROTECTED },
  { "resource", TK_RESOURCE },
  { "hard", TK_HARD },
  { "issue", TK_ISSUE },
  { "cpu", TK_CPU },
  { "cluster", TK_CLUSTER },
  { "func_unit", TK_FUNCUNIT },
  { "func_group", TK_FUNCGROUP },
  { "subunit", TK_SUBUNIT },
  { "latency", TK_LATENCY },
  { "register", TK_REGISTER },
  { "register_class", TK_REGCLASS },
  { "instruction", TK_INSTRUCT },
  { "operand", TK_OPERAND },
  { "predicate", TK_PREDICATE },
  { "import", TK_IMPORT },
  { "attribute", TK_ATTRIBUTE },
  { "type", TK_TYPE },
  { "derived", TK_DERIVED },
  { "reorder_buffer", TK_REORDER_BUFFER },
  { "forward", TK_FORWARD },
  { "port", TK_PORT },
  { "class", TK_CLASS },
  { "connect", TK_CONNECT },
  { "to", TK_TO },
  { "via", TK_VIA },

  { "BeginGroup", TK_BEGINGROUP },
  { "EndGroup", TK_ENDGROUP },
  { "SingleIssue", TK_SINGLEISSUE },
  { "RetireOOO", TK_RETIREOOO },

  { "use", TK_USE },
  { "def", TK_DEF },
  { "kill", TK_KILL },
  { "res", TK_RES, },
  { "hold", TK_HOLD },
  { "fus", TK_FUS },
  { "usedef", TK_USEDEF },
  { "if", TK_IF },
  { "else", TK_ELSE }
};

//-----------------------------------------------------------------------------
// Return the printable name of a token type.
//-----------------------------------------------------------------------------
std::string MdlLexer::TokenString(TokenKind t) {
  if (auto item = TokenNames.find(t); item != TokenNames.end())
    return item->second;
  return "Invalid token";
}

//-----------------------------------------------------------------------------
// Check an identifier to see if its a keyword.
//-----------------------------------------------------------------------------
void MdlLexer::checkKeywords(TokenType &Token) {
  // If we find the name in the table, change the token type to the keyword.
  if (auto item = Keywords.find(Token.Text); item != Keywords.end())
    Token.Type = item->second;
}

} // namespace mdl
} // namespace mpact
