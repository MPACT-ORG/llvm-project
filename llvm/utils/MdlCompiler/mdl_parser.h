//===- mdl_parser.h - Definitions for the MDL Parser ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Definitions for lexing and parsing the machine description language.
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

enum TokenKind {
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
  TK_BEGINGROUP, TK_ENDGROUP, TK_SINGLEISSUE, TK_RETIREOOO,
  TK_USE, TK_DEF, TK_KILL, TK_RES, TK_HOLD,
  TK_FUS, TK_USEDEF, TK_IF, TK_ELSE,

  // Delimiters
  TK_SEMI,
  TK_COLON,
  TK_SHARP,
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
struct TokenType : public MdlItem {
  TokenType() : MdlItem(), Type(TK_NONE), Value(0) {}
  TokenKind Type = TK_NONE;         // Type of current token
  std::string Text;                 // Text of current token
  long Value = 0;                   // Numerical value of a NUMBER token
};

//----------------------------------------------------------------------------
// Define a lexical analysis object.
// We create an MdlLexer for each input and import file. An MdlLexer has its
// own context about where it is in the current file. Each token has its
// own location information.  We implement a queue of token objects to
// implement lookahead for the parser.
//----------------------------------------------------------------------------
class MdlLexer {
  MdlSpec &Spec;
  std::fstream &Stream;           // Input stream
  std::string *FileName;          // Input file name.
  int LineNumber = 0;             // Current line number in the input file.
  int ColumnNumber = -1;          // Current column number in the current line.
  std::string CurrentLine;        // Text of current line in the input file.

  TokenType Token;                // Current token.
  std::queue<TokenType> Tokens;   // Used for lookahead
  TokenType Accepted;             // TODO? - maybe implement this

 public:
   MdlLexer(MdlSpec &Spec, std::string *FileName, std::fstream *Stream)
       : Spec(Spec), Stream(*Stream), FileName(FileName),
         LineNumber(0), ColumnNumber(-1)
         { getLine(); getNext(); }
  ~MdlLexer() { Stream.close(); }

 public:
  // Overload a lexer's () operator to get a reference to the token.
  MdlItem &operator() () { return Token; }

  // Fetch the next input line.
  bool getLine();
  bool skipSpace();
  // Get the next token;
  void getNext() { getNext(Token);}
  void getNext(TokenType &Token, bool Peeking = false);

  // Return the type, text, or  numerical value of the current token.
  TokenKind getType() const { return Token.Type; }
  std::string getText() const { return Token.Text; }
  long getValue() const { return Token.Value; }
  std::string TokenString(TokenKind T);

  void getString(TokenType &Token);
  void getIdent(TokenType &Token);
  void checkKeywords(TokenType &Token);
  void getNumber(TokenType &Token);
  bool getCodeEscape();

  template <typename ... Ts>
  void Error(const char *Fmt, Ts... Args) {
    Spec.ErrorLog(&Token, Fmt, Args...);
  }
  template <typename ... Ts>
  void Error(MdlItem &Item, const char *Fmt, Ts... Args) {
    Spec.ErrorLog(&Item, Fmt, Args...);
  }

  // Return true if the current token is one of specified token types.
  bool check(TokenKind T) { return check(Token, T); }
  template <typename... Ts>
     bool check(TokenKind T1, Ts... T2) {
       return check(Token, T1) || check(Token, T2...); }

  bool check(TokenType &Token, TokenKind T) { return Token.Type == T; }
  template <typename... Ts>
     bool check(TokenType &Token, TokenKind T1, Ts... T2) {
       return check(Token, T1) || check(Token, T2...); }

  // Accept a token if it is one of the specified types.
  TokenKind accept(TokenKind T) {
    if (T != Token.Type) return TK_NONE;
    getNext();
    return T;
  }
  template <typename... Ts> TokenKind accept(TokenKind T1, Ts... T2) {
    return accept(T1) ? T1 : accept(T2...);
  }

  // Accept a token if it is the proper type, otherwise issue an error message.
  bool expect(const TokenKind& T) {
    if (T == Token.Type) { getNext(); return true; }
    Error(Token, "{0} expected", TokenString(T));
    return false;
  }

  // Skip tokens until one of an input set is found.  Recur on braces and
  // parens to handle nested constructs.
  template <typename... Ts>
  void skipUntil(Ts... Args) {
    for (; !check(Args...); getNext()) {
      if (Token.Type == TK_LBRACE) {
        getNext();
        skipUntil(TK_RBRACE);
        continue;
      }
      if (Token.Type == TK_2LBRACE) {
        getNext();
        skipUntil(TK_2RBRACE);
        continue;
      }
      if (Token.Type == TK_LPAREN) {
        getNext();
        skipUntil(TK_RPAREN);
        continue;
      }
    }
  }

  //-------------------------------------------------------------------------
  // Implement a lookahead capability to peek at future tokens.
  //-------------------------------------------------------------------------
  bool peek(TokenKind T) {
    // Push token into queue
    TokenType Next;
    getNext(Next, true);                // Don't peek at current queue!
    Tokens.push(Next);
    return check(Tokens.back(), T);     // Check the last item in the queue.
  }
  template <typename... T> bool peek(T... Args) {
    // Push token into queue
    TokenType Next;
    getNext(Next, true);                // Don't peek at current queue!
    Tokens.push(Next);
    return check(Tokens.back(), Args...);  // Check the last item in the queue.
  }

  MdlLexer &operator++() { getNext(); return *this; }     // Prefix operator

  bool EndOfFile() { return Token.Type == TK_ENDOFFILE; }
};

//----------------------------------------------------------------------------
// Perform the lexing and parsing of the input stream. A representation of
// the input is returned in spec.  Return false if syntax errors found.
// This function is called recursively for imported input files.
//----------------------------------------------------------------------------
bool ProcessInputFile(MdlSpec &Spec, std::string ImportPath,
                      std::string FileName);

//----------------------------------------------------------------------------
// Define a parser class that implements a recursive descent parser.
//----------------------------------------------------------------------------
class MdlParser {
private:
  MdlSpec &Spec;
  std::string ImportPath;
  MdlLexer Token;                 // The lexical analyser

 public:
  explicit MdlParser(MdlSpec &Spec, std::string ImportPath,
                     std::string *FileName, std::fstream *MdlStream)
      : Spec(Spec), ImportPath(ImportPath),
        Token(Spec, FileName, MdlStream) {}

  template <typename ... Ts>
  void ParseError(const char *Fmt, Ts... Args) {
    Spec.ErrorLog(&Token(), Fmt, Args...);
  }
  template <typename ... Ts>
  void ParseError(const MdlItem *Item, const char *Fmt, Ts... Args) {
    Spec.ErrorLog(Item, Fmt, Args...);
  }
  int ErrorsSeen() { return Spec.ErrorsSeen(); }

  // Parser methods for all rules in the grammar.
  bool parseArchitectureSpec();
  Identifier *parseFamilySpec();
  bool parseImportFile();

  // Functions that parse components of a CPU definition.
  CpuInstance *parseCpuDef();
  std::vector<std::string> parseCpuDefNames();
  ClusterInstance *parseClusterInstantiation();
  std::tuple<Identifier *, int, bool> parseFuncUnitInstType(std::string Type);
  std::pair<IdList *, IdList *> parsePins();
  FuncUnitInstance *parseFuncUnitInstantiation(ResourceDefList *Resources);
  ForwardStmt *parseForwardStmt();

  // Functions that parse components of a functional unit template.
  FuncUnitTemplate *parseFuncUnitTemplate();
  FuncUnitGroup *parseFuncUnitGroup();
  ParamsList *parseFuncUnitParams();
  ParamsList *parseFuDeclItem();
  IdList *parsePortDef(ConnectList *Connects);
  Identifier *parsePortDecl(ConnectList *Connects);
  Connect *parseConnectStmt();
  SubUnitInstList *parseSubunitInstantiation(ResourceDefList *Resources);
  SubUnitInstList *parseSubunitStatement(IdList *Predicate,
                                         ResourceDefList *Resources);

  // Functions that parse components of subunit templates.
  SubUnitTemplate *parseSubunitTemplate();
  ParamsList *copySuDeclItems(ParamsList *Parameters);
  ParamsList *parseSuDeclItems();
  ParamsList *parseSuDeclItem();
  LatencyInstList *parseLatencyInstance();
  LatencyInstance *parseLatencyStatement(IdList *Predicates);

  // Functions that parse components of latency templates.
  LatencyTemplate *parseLatencyTemplate();
  ReferenceList *parseLatencyBlock(IdList *Predicates);
  ReferenceList *parseLatencyItems(IdList *Predicates);
  ReferenceList *parseLatencyItem(IdList *Predicates);
  ConditionalRef *parseConditionalRef(IdList *Predicates);

  // Functions that parse resource references.
  Reference *parseLatencyRef(IdList *Predicates);
  RefType parseRefType();
  ResourceRefList *parseLatencyResourceRefs();
  ResourceRef *parseLatencyResourceRef();
  ResourceRefList *parseResourceRefs(ResourceDefList *Resources = nullptr);
  ResourceRef *parseResourceRef(ResourceDefList *Resources = nullptr);
  ReferenceList *parseFusStatement(IdList *Predicates);
  Reference *parseFusItem(IdList *Predicates, MdlItem &Item);

  // Phase expression parser functions.
  PhaseExpr *parseExpr();
  PhaseExpr *parseTerm();
  PhaseExpr *parseFactor();
  OperandRef *parseOperand();
  bool isOperand();

  // Functions that parse pipeline definitions.
  PipePhases *parsePipeDef();
  PhaseNameList *parsePipePhases(bool IsProtected,
                                 bool IsHard, PhaseName *&ExePhase);
  PhaseNameList *parsePhaseId(bool IsProtected, bool IsHard, bool &IsFirst);

  // Functions that parse resource definitions.
  ResourceDefList *parseResourceDef();
  ResourceDef *parseResourceDecl(Identifier *Start, Identifier *End);
  ResourceDefList *parseIssueStatement();
  int parseReorder();

  // Parsers for various lists of things.
  bool parseRange(long &First, long &Last);
  bool parsePhaseRange(Identifier *&Start, Identifier *&End);
  IdList *parseNameList(std::string Type, TokenKind Separator);
  IdList *parsePredicateList();
  IdList *parseGroupList(GroupType &GroupType);
  IdList *parseBaseList(std::string Type);
  void parseSuBaseList(IdList *&Bases, StringList *&Regex);

  // Functions that parse register definitions.
  RegisterDefList *parseRegisterDef();
  RegisterClass *parseRegisterClass();
  RegisterDefList *parseRegisterDecl();
  RegisterDefList *parseRegisterList();

  // Helpers for parsing terminals.
  bool isIdent();
  Identifier *parseIdent(std::string Type);
  std::string parseString(std::string Type);
  bool parseNumber(long &value, std::string Type);
  bool parseSignedNumber(long &value, std::string Type);

  // Functions for parsing instruction and operand definitions.
  InstructionDef *parseInstructionDef();
  OperandDecl *parseOperandDecl(int OpndId);
  bool parseInputOutput(bool &IsInput, bool &IsOutput);
  OperandDef *parseOperandDef();
  Identifier *parseOperandType(Identifier *Type);
  OperandAttributeList *parseOperandAttribute();
  OperandAttribute *parseOperandAttributeStmt(IdList *Predicate);
  PredValue *parsePredValue();

  bool parsePredicateDef();
  PredExpr *parsePredicateOp();
  PredExpr *parsePredicateOpnd();
  std::string parseCodeEscape();

  MdlSpec &spec() { return Spec; }
};

} // namespace mdl
} // namespace mpact

#endif // MDL_COMPILER_MDL_VISITOR_H_
