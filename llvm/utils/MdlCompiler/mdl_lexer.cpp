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
  if (stream) {
    std::getline(stream, current_line);
    line_number++;
    column_number = 0;
    return true;
  }
  return false;
}

//-----------------------------------------------------------------------------
// Get the next token from the current input stream,
// If there are items in the lookahead queue, pop and use the first entry.
// Return the type of the token.
//-----------------------------------------------------------------------------
void MdlLexer::getNext(Token &token, bool peeking /* = false */) {
  // Return first item from the lookahead queue, if not empty.
  if (!peeking && !tokens.empty()) {
    token = tokens.front();
    tokens.pop();
    return;
  }

  // Skip over any blank spaces.
  if (!stream || !skipSpace()) {
    token.type = TK_ENDOFFILE;
    return;
  }

  // Initialize a new token.
  token.SetLocation(file_name, line_number, column_number);
  auto *line_ptr = current_line.c_str();
  token.text = std::string(1, line_ptr[column_number]);

  char ch = line_ptr[column_number++];
  switch (ch) {
    case ';': token.type = TK_SEMI;  return;
    case ',': token.type = TK_COMMA; return;
    case ':': token.type = TK_COLON; return;
    case '(': token.type = TK_LPAREN; return;
    case ')': token.type = TK_RPAREN; return;
    case '[': token.type = TK_LBRACKET; return;
    case ']': token.type = TK_RBRACKET; return;
    case '<': token.type = TK_LT; return;
    case '>': token.type = TK_GT; return;
    case '=': token.type = TK_EQUAL; return;
    case '&': token.type = TK_AND; return;
    case '|': token.type = TK_OR; return;
    case '*': token.type = TK_MUL; return;
    case '+': token.type = TK_PLUS; return;
    case '-': token.type = TK_MINUS;
              if (line_ptr[column_number] == '>') {
                token.type = TK_ARROW; column_number++;
              }
              return;
    case '/': token.type = TK_DIV;
              // Handle comments (like this one)
              if (line_ptr[column_number] == '/') {
                if (!getLine()) break;
                return getNext(token, peeking);
              }
              return;
    case '$': token.type = TK_DOLLAR;
              if (line_ptr[column_number] == '$') {
                token.type = TK_2DOLLAR; column_number++;
              }
              return;
    case '.': token.type = TK_DOT;
              if (line_ptr[column_number] == '.') {
                token.type = TK_DOTDOT; column_number++;
              }
              if (auto ch = line_ptr[column_number]; ch == '.') {
                token.type = TK_ELLIPSIS; column_number++;
              }
              return;
    case '{': token.type = TK_LBRACE;
              if (line_ptr[column_number] == '{') {
                token.type = TK_2LBRACE; column_number++;
              }
              return;
    case '}': token.type = TK_RBRACE;
              if (line_ptr[column_number] == '}') {
                token.type = TK_2RBRACE; column_number++;
              }
              return;

    case '"': return getString(token);

    default: break;
  }

  // Parse decimal numbers
  if (std::isdigit(ch)) return getNumber(token);

  // Parse Identifiers and look up keywords.
  if (std::isalpha(ch) || ch == '_') return getIdent(token); 
}

//-----------------------------------------------------------------------------
// Skip over any blank space until you find a token or EOF.
//-----------------------------------------------------------------------------
bool MdlLexer::skipSpace() {
  auto *line_ptr = current_line.c_str() + column_number;
  while (std::isspace(*line_ptr)) { line_ptr++; column_number++; }

  if (*line_ptr == 0) {
    if (!getLine()) return false;
    return skipSpace();
  }
  return true;
}

//-----------------------------------------------------------------------------
// Scan in Decimal, Hex, Octal or Binary numbers.
//-----------------------------------------------------------------------------
void MdlLexer::getNumber(Token &token) {
  auto *line_ptr = current_line.c_str() + column_number - 1;
  char *end_ptr = nullptr;

  token.type = TK_NUMBER;

  // Look for radix prefixes (0x, 0b, 0)
  int base = 10;
  if (line_ptr[0] == '0') {
    if (line_ptr[1] == 'x') base = 16;
    else if (line_ptr[1] == '0' && line_ptr[1] <= '7') base = 8;
    else if (line_ptr[1] == 'b') { base = 2; line_ptr += 2; }
  }

  // Convert the number in the appropriate radix.
  token.value = strtoul(line_ptr, &end_ptr, base);
  if (end_ptr == line_ptr || errno == ERANGE)
    Error("Invalid number");

  // Copy the text of the number to the token
  token.text = current_line.substr(
                  column_number - 1, column_number + (end_ptr - line_ptr));
  column_number += end_ptr - line_ptr - 1;
}

//-----------------------------------------------------------------------------
// Scan in an identifier.
//-----------------------------------------------------------------------------
void MdlLexer::getIdent(Token &token) {
  token.type = TK_IDENT;

  auto *line_ptr = current_line.c_str() + column_number;
  for (; std::isalnum(*line_ptr) || *line_ptr == '_'; column_number++)
    token.text += std::string(1, *line_ptr++);
  checkKeywords(token);
}

//-----------------------------------------------------------------------------
// Scan in a string literal
// This is a pretty limited string - no escape characters, no newlines.
// We strip the quotes here (rather than everywhere else).
//-----------------------------------------------------------------------------
void MdlLexer::getString(Token &token) {
  token.type = TK_STRING;
  auto *line_ptr = current_line.c_str();
  token.text = "";
  while (auto ch = line_ptr[column_number++]) {
    if (ch == '\n' || ch == '"') break;
    token.text += std::string(1, ch);
  }
  if (line_ptr[column_number - 1] != '"')
    Error(token, "Missing end quote");
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
  if (token.line() != tokens.front().line() ||
      token.column() != tokens.front().column() - 1 ||
      token.file_name() != tokens.front().file_name())
    return false;

  // Scan current line until we find an '}]'.
  std::string code;
  auto *line_ptr = current_line.c_str();
  while (auto ch = line_ptr[column_number++]) {
    if (ch == '}' && line_ptr[column_number] == ']') {
      tokens.pop();         // discard the peeked-at '}'
      column_number++;      // Skip the ']'
      token.text = code;    // replace the current token
      return true;
      break;
    }
    code += std::string(1, ch);
  }
  if (line_ptr[column_number] != ']') return false;

  tokens.pop();         // discard the peeked-at '}'
  token.text = code;    // replace the current token
  return true;
}

std::map<TokenType, std::string> TokenNames = {
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
std::map<std::string, TokenType> Keywords = {
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
std::string MdlLexer::TokenString(TokenType t) {
  if (auto item = TokenNames.find(t); item != TokenNames.end())
    return item->second;
  return "Invalid token";
}

//-----------------------------------------------------------------------------
// Check an identifier to see if its a keyword.
//-----------------------------------------------------------------------------
void MdlLexer::checkKeywords(Token &token) {
  // If we find the name in the table, change the token type to the keyword.
  if (auto item = Keywords.find(token.text); item != Keywords.end())
    token.type = item->second;
}

} // namespace mdl
} // namespace mpact
