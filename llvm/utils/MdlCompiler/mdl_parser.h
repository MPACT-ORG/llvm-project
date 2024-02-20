//===- mdl_parser.h - Definitions for the MDL Parser ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Definitions for parsing the machine description language.
//
//===----------------------------------------------------------------------===//
//
#ifndef MDL_COMPILER_MDL_PARSER_H_
#define MDL_COMPILER_MDL_PARSER_H_

#include <fstream>
#include <iostream>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "mdl.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"

namespace mpact {
namespace mdl {

enum TokenType {
  TK_NONE = 0,
  TK_ENDOFFILE,

  // Keywords
  TK_FAMILY,
  TK_PIPE_PHASES, TK_PROTECTED, TK_UNPROTECTED, TK_HARD,
  TK_RESOURCE, TK_ISSUE,
  TK_CPU,
  TK_CLUSTER,
  TK_FUNCUNIT,
  TK_FUNCGROUP,
  TK_SUBUNIT,
  TK_LATENCY,
  TK_REGISTER,
  TK_REGCLASS,
  TK_INSTRUCT,
  TK_OPERAND,
  TK_PREDICATE,
  TK_IMPORT,
  TK_ATTRIBUTE,
  TK_TYPE,
  TK_DERIVED,
  TK_REORDER_BUFFER,
  TK_FORWARD,
  TK_PORT,
  TK_CLASS,
  TK_CONNECT,
  TK_TO,
  TK_VIA,

  // Keywords that can probably not be keywords
  TK_BEGINGROUP, TK_ENDGROUP, TK_SINGLEISSUE, TK_RETIREOOO,   // TODO
  TK_USE, TK_DEF, TK_KILL, TK_RES, TK_HOLD,
  TK_FUS, TK_USEDEF, TK_IF, TK_ELSE,

  // Tokens with 1, 2, or 3 special characters
  TK_SEMI,
  TK_COLON,
  TK_SHARP,

  // Delimters
  TK_COMMA,
  TK_LPAREN, TK_RPAREN,
  TK_LBRACKET, TK_RBRACKET,
  TK_LBRACE, TK_RBRACE,
  TK_2LBRACE, TK_2RBRACE,

  TK_LT, TK_GT,
  TK_EQUAL,
  TK_AND, TK_OR, TK_PLUS, TK_MINUS, TK_MUL, TK_DIV,
  TK_ARROW,
  TK_DOLLAR, TK_2DOLLAR,
  TK_DOT, TK_DOTDOT, TK_ELLIPSIS,

  TK_IDENT,
  TK_NUMBER,
  TK_STRING,
};

//----------------------------------------------------------------------------
// Define a single Token, which independently records its location info.
//----------------------------------------------------------------------------
struct Token : public MdlItem {
  Token() : MdlItem(), type(TK_NONE), value(0) {}
  TokenType type = TK_NONE; // Type of current token
  std::string text;         // Text of current token
  long value;               // Numerical value of token
};

//----------------------------------------------------------------------------
// Define a lexical analysis object.
// We create an MdlLexer for each input and import file. An MdlLexer has its
// own context about where it is in the current file. Each token has its
// own location information.  We implement a queue of token objects to
// implement lookahead for the parser.
//----------------------------------------------------------------------------
class MdlLexer {
 public:
   MdlLexer(MdlSpec &spec, std::string *file_name,
            std::fstream *stream)
       : spec(spec), stream(*stream), file_name(file_name),
         line_number(0), column_number(-1)
         { getLine(); getNext(); }
  ~MdlLexer() { stream.close(); }

 private:
  MdlSpec &spec;
  std::fstream &stream;       // Input stream
  std::string *file_name;     // Input file name.
  int line_number = 0;        // Current line number in the input file.
  int column_number = -1;     // Current column number in the current line.
  std::string current_line;   // Text of current line in the input file.

  Token token;                // Current token.
  std::queue<Token> tokens;   // Used for lookahead
  Token accepted;             // TODO - maybe implement this

 public:
  // Overload a lexer's () operator to get a reference to the token.
  MdlItem &operator() () { return token; }

  // Fetch the next input line.
  bool getLine();
  bool skipSpace();
  // Get the next token;
  void getNext() { getNext(token);}
  void getNext(Token &token, bool peeking = false);

  // Return the type, text, or  numerical value of the current token.
  TokenType getType() const { return token.type; }
  std::string getText() const { return token.text; }
  long getValue() const { return token.value; }
  std::string TokenString(TokenType t);

  void getString(Token &token);
  void getIdent(Token &token);
  void checkKeywords(Token &token);
  void getNumber(Token &token);
  bool getCodeEscape();

  template <typename ... Ts>
  void Error(const char *fmt, Ts... params) {
    spec.ErrorLog(&token, fmt, params...);
  }
  template <typename ... Ts>
  void Error(MdlItem &item, const char *fmt, Ts... params) {
    spec.ErrorLog(&item, fmt, params...);
  }

  // Return true if the current token is one of specified token types.
  bool check(TokenType t) { return check(token, t); }
  template <typename... T2>
     bool check(TokenType t1, T2... t2) {
       return check(token, t1) || check(token, t2...); }

  bool check(Token &token, TokenType t) { return token.type == t; }
  template <typename... T2>
     bool check(Token &token, TokenType t1, T2... t2) {
       return check(token, t1) || check(token, t2...); }

  // Accept a token if it is one of the specified types.
  TokenType accept(TokenType t) {
    if (t != token.type) return TK_NONE;
    getNext();
    return t;
  }
  template <typename... T2> TokenType accept(TokenType t1, T2... t2) {
    return accept(t1) ? t1 : accept(t2...);
  }

  // Accept a token if it is the proper type, otherwise issue an error message.
  bool expect(const TokenType& t) {
    if (t == token.type) { getNext(); return true; }
    Error(token, "{0} expected", TokenString(t));
    return false;
  }

  // Skip tokens until one of an input set is found.  Recur on braces and
  // parens to handle nested objects.
  template <typename... Ts>
  void SkipUntil(Ts... params) {
    for (; !check(params...); getNext()) {
      if (token.type == TK_LBRACE) {
        getNext();
        SkipUntil(TK_RBRACE);
        continue;
      }
      if (token.type == TK_2LBRACE) {
        getNext();
        SkipUntil(TK_2RBRACE);
        continue;
      }
      if (token.type == TK_LPAREN) {
        getNext();
        SkipUntil(TK_RPAREN);
        continue;
      }
    }
  }

  //-------------------------------------------------------------------------
  // Implement a lookahead capability to peek at future tokens.
  //-------------------------------------------------------------------------
  bool peek(TokenType t) {
    // Push token into queue
    Token next;
    getNext(next, true);                // Don't peek at current queue!
    tokens.push(next);
    return check(tokens.back(), t);     // Check the last item in the queue.
  }
  template <typename... T> bool peek(T... t) {
    // Push token into queue
    Token next;
    getNext(next, true);                // Don't peek at current queue!
    tokens.push(next);
    return check(tokens.back(), t...);  // Check the last item in the queue.
  }

  MdlLexer &operator++() { getNext(); return *this; }     // Prefix operator

  bool EndOfFile() { return token.type == TK_ENDOFFILE; }
};

//----------------------------------------------------------------------------
// Perform the lexing and parsing of the input stream. A representation of
// the input is returned in spec.  Return false if syntax errors found.
// This function is called recursively for imported input files.
//----------------------------------------------------------------------------
bool ProcessInputFile(MdlSpec &spec, std::string import_path,
                      std::string file_name);

//----------------------------------------------------------------------------
// Define a parser class that implements a recursive descent parser.
//----------------------------------------------------------------------------
class MdlParser {
private:
  MdlSpec &spec_;
  std::string import_path_;
  MdlLexer Token;             // The lexical analyser

 public:
  explicit MdlParser(MdlSpec &spec, std::string import_path,
                     std::string *file_name, std::fstream *mdl_stream)
      : spec_(spec), import_path_(import_path),
        Token(spec, file_name, mdl_stream) {}

  template <typename ... Ts>
  void Error(const char *fmt, Ts... params) {
    spec_.ErrorLog(&Token(), fmt, params...);
  }
  template <typename ... Ts>
  void Error(const MdlItem *item, const char *fmt, Ts... params) {
    spec_.ErrorLog(item, fmt, params...);
  }

  // Parser methods for all rules in the grammar.
  bool ParseArchitectureSpec();
  Identifier *ParseFamilySpec();
  bool ParseImportFile();

  // Functions that parse components of a CPU definition.
  CpuInstance *ParseCpuDef();
  std::vector<std::string> ParseCpuDefNames();
  ClusterInstance *ParseClusterInstantiation();
  std::tuple<Identifier *, int, bool> ParseFuncUnitInstType(std::string type);
  std::pair<IdList *, IdList *> ParsePins();
  FuncUnitInstance *ParseFuncUnitInstantiation(ResourceDefList *resources);
  ForwardStmt *ParseForwardStmt();

  // Functions that parse components of a functional unit template.
  FuncUnitTemplate *ParseFuncUnitTemplate();
  FuncUnitGroup *ParseFuncUnitGroup();
  ParamsList *ParseFuncUnitParams();
  ParamsList *ParseFuDeclItem();
  IdList *ParsePortDef(ConnectList *connects);
  Identifier *ParsePortDecl(ConnectList *connects);
  Connect *ParseConnectStmt();
  SubUnitInstList *ParseSubunitInstantiation(ResourceDefList *resources);
  SubUnitInstList *ParseSubunitStatement(IdList *predicate,
                                         ResourceDefList *resources);

  // Functions that parse components of subunit templates.
  SubUnitTemplate *ParseSubunitTemplate();
  ParamsList *CopySuDeclItems(ParamsList *params);
  ParamsList *ParseSuDeclItems();
  ParamsList *ParseSuDeclItem();
  LatencyInstList *ParseLatencyInstance();
  LatencyInstance *ParseLatencyStatement(IdList *predicates);

  // Functions that parse components of latency templates.
  LatencyTemplate *ParseLatencyTemplate();
  ReferenceList *ParseLatencyBlock(IdList *predicates);
  ReferenceList *ParseLatencyItems(IdList *predicates);
  ReferenceList *ParseLatencyItem(IdList *predicates);
  ConditionalRef *ParseConditionalRef(IdList *predicates);

  // Functions that parse resource references.
  Reference *ParseLatencyRef(IdList *predicates);
  RefType ParseRefType();
  ResourceRefList *ParseLatencyResourceRefs();
  ResourceRef *ParseLatencyResourceRef();
  ResourceRefList *ParseResourceRefs(ResourceDefList *resources = nullptr);
  ResourceRef *ParseResourceRef(ResourceDefList *resources = nullptr);
  ReferenceList *ParseFusStatement(IdList *predicates);
  Reference *ParseFusItem(IdList *predicates, MdlItem &item);

  // Phase expression parser functions.
  PhaseExpr *ParseExpr();
  PhaseExpr *ParseTerm();
  PhaseExpr *ParseFactor();
  OperandRef *ParseOperand();
  bool isOperand();

  // Functions that parse pipeline definitions.
  PipePhases *ParsePipeDef();
  PhaseNameList *ParsePipePhases(bool is_protected,
                                 bool is_hard, PhaseName *&exe_phase);
  PhaseNameList *ParsePhaseId(bool is_protected, bool is_hard, bool &is_first);

  // Functions that parse resource definitions.
  ResourceDefList *ParseResourceDef();
  ResourceDef *ParseResourceDecl(Identifier *start, Identifier *end);
  ResourceDefList *ParseIssueStatement();
  int ParseReorder();

  // Parsers for various lists of things.
  bool ParseRange(long &first, long &last);
  bool ParsePhaseRange(Identifier *&start, Identifier *&end);
  IdList *ParseNameList(std::string type, TokenType separator);
  IdList *ParsePredicateList();
  IdList *ParseGroupList(GroupType &group_type);
  IdList *ParseBaseList(std::string type);
  void ParseSuBaseList(IdList *&bases, StringList *&regex);

  // Functions that parse register definitions.
  RegisterDefList *ParseRegisterDef();
  RegisterClass *ParseRegisterClass();
  RegisterDefList *ParseRegisterDecl();
  RegisterDefList *ParseRegisterList();

  // Helpers for parsing terminals.
  bool isIdent();
  Identifier *ParseIdent(std::string type);
  std::string ParseString(std::string type);
  bool ParseNumber(long &value, std::string type);
  bool ParseSignedNumber(long &value, std::string type);

  // Functions for parsing instruction and operand definitions.
  InstructionDef *ParseInstructionDef();
  OperandDecl *ParseOperandDecl(int opnd_id);
  bool ParseInputOutput(bool &is_input, bool &is_output);
  OperandDef *ParseOperandDef();
  Identifier *ParseOperandType(Identifier *type);
  OperandAttributeList *ParseOperandAttribute();
  OperandAttribute *ParseOperandAttributeStmt(IdList *predicate);
  PredValue *ParsePredValue();

  bool ParsePredicateDef();
  PredExpr *ParsePredicateOp();
  PredExpr *ParsePredicateOpnd();
  std::string ParseCodeEscape();

  MdlSpec &spec() { return spec_; }
};

} // namespace mdl
} // namespace mpact

#endif // MDL_COMPILER_MDL_VISITOR_H_
