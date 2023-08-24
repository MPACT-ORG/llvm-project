//===- mdl.g4 - Antlr4 grammar for the MDL language -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//---------------------------------------------------------------------------
// Grammar for the MPACT Machine Description Language.
//
// This file is used by ANTLR4 to create a recursive descent parser for the
// specified input language, which produces a parse tree representing a
// parsed input file. See README for more information.
//
// IF YOU CHANGE THIS FILE, YOU SHOULD ASSUME THAT YOU WILL HAVE TO MAKE
// CHANGES IN DOWNSTREAM CLIENTS (in particular, mdl_visitor.*) THAT REFLECT
// THE STRUCTURE AND TYPE OF THE PARSE TREE.
//
// For more infomation about ANTLR4 grammars, see:
//      github.com/antlr/antlr4/blob/master/doc/index.md
// For a good tutorial of how to use ANTLR4, see "The ANTLR Mega Tutorial"
//      tomassetti.me/antlr-mega-tutorial
//---------------------------------------------------------------------------

grammar mdl;

//---------------------------------------------------------------------------
// Top level production for entire file.
//---------------------------------------------------------------------------
architecture_spec       : architecture_item* EOF
                        ;
architecture_item       : family_name
                        | cpu_def
                        | register_def
                        | register_class
                        | resource_def
                        | pipe_def
                        | func_unit_template
                        | func_unit_group
                        | subunit_template
                        | latency_template
                        | instruction_def
                        | operand_def
                        | derived_operand_def
                        | import_file
                        | predicate_def
                        ;

//---------------------------------------------------------------------------
// We support import files at the top-level in the grammar. These will be
// handled by the visitor, so the containing file is completely parsed
// before handling any imported files.
//---------------------------------------------------------------------------
import_file             : IMPORT STRING_LITERAL
                        ;

//---------------------------------------------------------------------------
// Define a processor family name, used to interact with target compiler.
//---------------------------------------------------------------------------
family_name             : FAMILY ident ';'
                        ;

//---------------------------------------------------------------------------
// Top-level CPU instantiation.
//---------------------------------------------------------------------------
cpu_def                 : (CPU | CORE) ident
                             ('(' STRING_LITERAL (',' STRING_LITERAL)* ')')?
                             '{' cpu_stmt* '}' ';'?
                        ;
cpu_stmt                : pipe_def
                        | resource_def
                        | reorder_buffer_def
                        | issue_statement
                        | cluster_instantiation
                        | func_unit_instantiation
                        | forward_stmt
                        ;

reorder_buffer_def      : REORDER_BUFFER '<' size=number '>' ';'
                        ;

//---------------------------------------------------------------------------
// Cluster specification.
//---------------------------------------------------------------------------
cluster_instantiation   : CLUSTER cluster_name=ident '{' cluster_stmt+ '}' ';'?
                        ;
cluster_stmt            : resource_def
                        | issue_statement
                        | func_unit_instantiation
                        | forward_stmt
                        ;
issue_statement         : ISSUE '(' start=ident ('..' end=ident)? ')'
                          name_list ';'
                        ;

//---------------------------------------------------------------------------
// Functional unit instantiation (in CPUs and Clusters).
//---------------------------------------------------------------------------
func_unit_instantiation : FUNCUNIT type=func_unit_instance
                          bases=func_unit_bases*
                          name=ident '(' resource_refs? ')'
                          ('->' (one=pin_one | any=pin_any | all=pin_all))?  ';'
                        ;
pin_one                 : ident              // avoid ambiguity with any/all.
                        ;
pin_any                 : ident ('|' ident)+
                        ;
pin_all                 : ident ('&' ident)+
                        ;
func_unit_instance      : ident (unreserved='<>' | ('<' buffered=number '>'))?
                        ;
func_unit_bases         : ':' func_unit_instance
                        ;

//---------------------------------------------------------------------------
// A single forwarding specification (in CPUs and Clusters).
//---------------------------------------------------------------------------
forward_stmt            : FORWARD from_unit=ident '->'
                                      forward_to_unit (',' forward_to_unit)* ';'
                        ;
forward_to_unit         : ident ('(' cycles=snumber ')')?
                        ;

//---------------------------------------------------------------------------
// Functional unit template definition.
//---------------------------------------------------------------------------
func_unit_template      : FUNCUNIT type=ident base=base_list
                                '(' func_unit_params? ')'
                                '{' func_unit_template_stmt* '}' ';'?
                        ;
func_unit_params        : fu_decl_item (';' fu_decl_item)*
                        ;
fu_decl_item            : RESOURCE  name_list
                        | CLASS     name_list
                        ;
func_unit_template_stmt : resource_def
                        | port_def
                        | connect_stmt
                        | subunit_instantiation
                        ;
port_def                : PORT port_decl (',' port_decl)* ';'
                        ;
port_decl               : name=ident ('<' reg_class=ident '>')?
                             ('(' ref=resource_ref ')')?
                        ;

connect_stmt            : CONNECT port=ident
                             (TO reg_class=ident)? (VIA resource_ref)? ';'
                        ;


//---------------------------------------------------------------------------
// Functional unit group definition.
//---------------------------------------------------------------------------
func_unit_group         : FUNCGROUP name=ident ('<' buffered=number '>')?
                                  ':' members=name_list ';'
                        ;

//---------------------------------------------------------------------------
// Other FU statements, we may not need these.
//---------------------------------------------------------------------------
// local_connect <port> TO <regclass_name>.

//---------------------------------------------------------------------------
// Definition of subunit template instantiation.
//---------------------------------------------------------------------------
subunit_instantiation   : (predicate=name_list ':')? subunit_statement
                        | predicate=name_list ':'
                             '{' subunit_statement* '}' ';'?
                        ;

subunit_statement       : SUBUNIT subunit_instance (',' subunit_instance)* ';'
                        ;

subunit_instance        : ident  '(' resource_refs? ')'
                        ;

//---------------------------------------------------------------------------
// Definition of subunit template definition.
//---------------------------------------------------------------------------
subunit_template        : SUBUNIT name=ident base=su_base_list
                             '(' su_decl_items? ')'
                             (('{' body = subunit_body* '}' ';'?) |
                              ('{{' latency_items* '}}' ';'? ))
                        ;
su_decl_items           : su_decl_item (';' su_decl_item)*
                        ;
su_decl_item            : RESOURCE name_list
                        | PORT     name_list
                        ;
su_base_list            : (':' (unit=ident | regex=STRING_LITERAL))*
                        ;

//---------------------------------------------------------------------------
// Subunit template statements.
//---------------------------------------------------------------------------
subunit_body            : latency_instance
                        ;
latency_instance        : (predicate=name_list ':')? latency_statement
                        | predicate=name_list ':'
                             '{' latency_statement* '}' ';'?
                        ;
latency_statement       : LATENCY ident '(' resource_refs? ')' ';'
                        ;

//---------------------------------------------------------------------------
// Latency template definition.
//---------------------------------------------------------------------------
latency_template        : LATENCY name=ident base=base_list
                             '(' su_decl_items? ')'
                             '{' latency_items* '}' ';'?
                        ;
latency_items           : (predicate=name_list ':')?
                               (latency_item | ('{' latency_item* '}' ';'?))
                        ;
latency_item            : latency_ref
                        | conditional_ref
                        | fus_statement
                        ;

//---------------------------------------------------------------------------
// Conditional references
//---------------------------------------------------------------------------
conditional_ref         : 'if' ident '{' latency_item* '}'
                               (conditional_elseif | conditional_else)?
                        ;
conditional_elseif      : 'else' 'if' ident '{' latency_item* '}'
                               (conditional_elseif | conditional_else)?
                        ;
conditional_else        : 'else' '{' latency_item* '}'
                        ;

//---------------------------------------------------------------------------
// Basic references
//---------------------------------------------------------------------------
latency_ref             : ref_type '(' latency_spec ')' ';'
                        ;
ref_type                : (USE | DEF | USEDEF | KILL | HOLD | RES | PREDICATE)
                        ;
latency_spec            : expr (':' cycles=number)? ',' latency_resource_refs
                        | expr ('[' repeat=number (',' delay=number)? ']')?
                               ',' operand
                        | expr ',' operand ',' latency_resource_refs
                        ;
expr                    : '-' negate=expr
                        | left=expr mop=('*' | '/') right=expr
                        | left=expr aop=('+' | '-') right=expr
                        | '{' posexpr=expr '}'
                        | '(' subexpr=expr ')'
                        | phase_name=ident
                        | num=number
                        | opnd=operand
                        ;

//---------------------------------------------------------------------------
// Shorthand for a reference that uses functional units.
//---------------------------------------------------------------------------
fus_statement           : FUS '(' (fus_item ('&' fus_item)* ',')?
                                  micro_ops=snumber (',' fus_attribute)* ')' ';'
                        ;
fus_item                : name=ident ('<' (expr ':')? number '>')?
                        ;
fus_attribute           : BEGINGROUP | ENDGROUP | SINGLEISSUE | RETIREOOO
                        ;

//---------------------------------------------------------------------------
// Latency resource references allow resource allocation and value masking.
// Member references and index referencing don't allow allocation, but do
// allow masking.  This is checked semantically in the visitor, not here.
//---------------------------------------------------------------------------
latency_resource_refs   : latency_resource_ref (',' latency_resource_ref)*
                        ;
latency_resource_ref    : resource_ref ':' count=number    (':' value=ident)?
                        | resource_ref ':' countname=ident (':' value=ident)?
                        | resource_ref ':' ':' value=ident   // no allocation
                        | resource_ref ':' all='*'
                        | resource_ref
                        ;
operand                 : (type=ident ':')? '$' opnd=ident ('.' operand_ref)*
                        | (type=ident ':')? '$' opnd_id=number
                        | (type=ident ':')? '$$' var_opnd_id=number
                        ;
operand_ref             : ident | number
                        ;

//---------------------------------------------------------------------------
// Pipeline phase names definitions.
//---------------------------------------------------------------------------
pipe_def                : protection? PIPE_PHASES ident '{' pipe_phases '}' ';'?
                        ;
protection              : PROTECTED | UNPROTECTED | HARD
                        ;
pipe_phases             : phase_id (',' phase_id)*
                        ;
phase_id                : (first_exe='#')? ident ('[' range ']')? ('=' number)?
                        ;

//---------------------------------------------------------------------------
// Resource definitions: global in scope, CPU- or Cluster- or FU-level.
//---------------------------------------------------------------------------
resource_def            : RESOURCE ( '(' start=ident ('..' end=ident)? ')' )?
                              resource_decl (',' resource_decl)*  ';'
                        ;
resource_decl           : name=ident (':' bits=number)? ('[' count=number ']')?
                        | name=ident (':' bits=number)? '{' name_list '}'
                        | name=ident (':' bits=number)? '{' group_list '}'
                        ;
resource_refs           : resource_ref (',' resource_ref)*
                        ;
resource_ref            : name=ident ('[' range ']')?
                        | name=ident '.' member=ident
                        | name=ident '[' index=number ']'
                        | group_or=ident ('|' ident)+
                        | group_and=ident ('&' ident)+
                        ;

//---------------------------------------------------------------------------
// List of identifiers.
//---------------------------------------------------------------------------
name_list               : ident (',' ident)*
                        ;
group_list              : group_or=ident ('|' ident)+
                        | group_and=ident ('&' ident)+
                        ;
//---------------------------------------------------------------------------
// List of template bases
//---------------------------------------------------------------------------
base_list               : (':' ident)*
                        ;

//---------------------------------------------------------------------------
// Register definitions.
//---------------------------------------------------------------------------
register_def            : REGISTER register_decl (',' register_decl)* ';'
                        ;
register_decl           : name=ident ('[' range ']')?
                        ;
register_class          : REGCLASS ident
                            '{' register_decl (',' register_decl)* '}' ';'?
                        | REGCLASS ident '{' '}' ';'?
                        ;

//---------------------------------------------------------------------------
// Instruction definition.
//---------------------------------------------------------------------------
instruction_def         : INSTRUCTION name=ident
                             '(' (operand_decl (',' operand_decl)*)? ')'
                             '{'
                                 (SUBUNIT '(' subunit=name_list ')' ';' )?
                                 (DERIVED '(' derived=name_list ')' ';' )?
                             '}' ';'?
                        ;

//---------------------------------------------------------------------------
// Operand definition.
//---------------------------------------------------------------------------
operand_def             : OPERAND name=ident
                             '(' (operand_decl (',' operand_decl)*)? ')'
                             '{' (operand_type | operand_attribute)* '}' ';'?
                        ;
operand_decl            : ((type=ident (name=ident)?) | ellipsis='...')
                              (input='(I)' | output='(O)')?
                        ;

operand_type            : TYPE '(' type=ident ')' ';'
                        ;
operand_attribute       : (predicate=name_list ':')? operand_attribute_stmt
                        | predicate=name_list ':'
                              '{' operand_attribute_stmt* '}' ';'?
                        ;
operand_attribute_stmt  : ATTRIBUTE name=ident '='
                          (value=snumber | values=tuple)
                           (IF type=ident
                                   ('[' pred_value (',' pred_value)* ']' )? )?
                            ';'
                        ;
pred_value              : value=snumber
                        | low=snumber '..' high=snumber
                        | '{' mask=number '}'
                        ;

//---------------------------------------------------------------------------
// Derived Operand definition.
//---------------------------------------------------------------------------
derived_operand_def     : OPERAND name=ident base_list  ('(' ')')?
                              '{' (operand_type | operand_attribute)* '}' ';'?
                        ;

//---------------------------------------------------------------------------
// Predicate definition.
//---------------------------------------------------------------------------
predicate_def           : PREDICATE ident ':' predicate_op? ';'
                        ;

predicate_op            : pred_opcode '<' pred_opnd (',' pred_opnd)* ','? '>'
                        | pred_opcode (('<' '>') | '<>')
                        | code=code_escape
                        | ident
                        ;
code_escape             : '[' '{' .*? '}' ']'
                        ;

pred_opnd               : name=ident
                        | snumber
                        | string=STRING_LITERAL
                        | '[' opcode_list=ident (',' ident)* ']'
                        | pred=predicate_op
                        | operand
                        ;

pred_opcode             : 'CheckAny' | 'CheckAll' | 'CheckNot'
                        | 'CheckOpcode'
                        | 'CheckIsRegOperand' | 'CheckRegOperand'
                        | 'CheckSameRegOperand' | 'CheckNumOperands'
                        | 'CheckIsImmOperand' | 'CheckImmOperand'
                        | 'CheckZeroOperand' | 'CheckInvalidRegOperand'
                        | 'CheckFunctionPredicate'
                        | 'CheckFunctionPredicateWithTII'
                        | 'TIIPredicate'
                        | 'OpcodeSwitchStatement' | 'OpcodeSwitchCase'
                        | 'ReturnStatement'
                        | 'MCSchedPredicate'
                        ;

//---------------------------------------------------------------------------
// ANTLR hack to allow some identifiers to override some keywords in some
// circumstances (wherever "ident" is used).  For the most part, we just
// allow overriding "short" keywords for resources, registers, operands,
// pipeline names, and ports.
//---------------------------------------------------------------------------
ident                   : 'use' | 'def' | 'kill' | 'usedef' | 'hold' | 'res'
                        | 'port' | 'to' | 'via' | 'core' | 'cpu' | 'issue'
                        | 'class' | 'type' | 'hard' | 'if' | 'family'
                        | 'fus' | 'BeginGroup' | 'EndGroup' | 'SingleIssue'
                        | 'RetireOOO' | 'register' | IDENT
                        ;

//---------------------------------------------------------------------------
// Match and convert a number.
//---------------------------------------------------------------------------
number returns [int64_t value]
                         : NUMBER { $value = std::stoul($NUMBER.text, 0, 0); }
                         ;
snumber returns [int64_t value]
                         : NUMBER { $value = std::stoul($NUMBER.text, 0, 0); }
                         | '-' NUMBER
                                { $value = -std::stoul($NUMBER.text, 0, 0); }
                         ;

//---------------------------------------------------------------------------
// Match a set of numbers.
//---------------------------------------------------------------------------
tuple                    : '[' snumber (',' snumber)* ']'
                         ;

//---------------------------------------------------------------------------
// A constrained range - both must be non-negative numbers.
//---------------------------------------------------------------------------
range                   : first=number '..' last=number
                        ;

//---------------------------------------------------------------------------
// Token definitions.
//---------------------------------------------------------------------------
FAMILY                  : 'family';
CPU                     : 'cpu';
CORE                    : 'core';
CLUSTER                 : 'cluster';
REORDER_BUFFER          : 'reorder_buffer';
ISSUE                   : 'issue';
FUNCUNIT                : 'func_unit';
FORWARD                 : 'forward';
FUNCGROUP               : 'func_group';
CONNECT                 : 'connect';
SUBUNIT                 : 'subunit';
FUS                     : 'fus';
BEGINGROUP              : 'BeginGroup';
ENDGROUP                : 'EndGroup';
SINGLEISSUE             : 'SingleIssue';
RETIREOOO               : 'RetireOOO';
MICROOPS                : 'micro_ops';
DERIVED                 : 'derived';
LATENCY                 : 'latency';
PIPE_PHASES             : 'phases';
PROTECTED               : 'protected';
UNPROTECTED             : 'unprotected';
HARD                    : 'hard';
RESOURCE                : 'resource';
PORT                    : 'port';
TO                      : 'to';
VIA                     : 'via';
REGISTER                : 'register';
REGCLASS                : 'register_class';
CLASS                   : 'class';
IMPORT                  : 'import';
INSTRUCTION             : 'instruction';
OPERAND                 : 'operand';
TYPE                    : 'type';
ATTRIBUTE               : 'attribute';
IF                      : 'if';
USE                     : 'use';
DEF                     : 'def';
USEDEF                  : 'usedef';
KILL                    : 'kill';
HOLD                    : 'hold';
RES                     : 'res';
PREDICATE               : 'predicate';

IDENT                   : [_a-zA-Z][_a-zA-Z0-9]*;

NUMBER                  : HEX_NUMBER | OCT_NUMBER | BIN_NUMBER | DEC_NUMBER;
DEC_NUMBER              : '0' | [1-9][0-9]*;
HEX_NUMBER              : '0x' HEX_DIGIT (HEX_DIGIT | '\'')*;
HEX_DIGIT               : [0-9a-fA-F];
OCT_NUMBER              : '0' OCT_DIGIT (OCT_DIGIT | '\'')*;
OCT_DIGIT               : [0-7];
BIN_NUMBER              : '0b' [0-1] ([0-1] | '\'')*;

STRING_LITERAL          : UNTERMINATED_STRING_LITERAL '"';
UNTERMINATED_STRING_LITERAL : '"' (~["\\\r\n] | '\\' (. | EOF))*;

BLOCK_COMMENT           : '/*' .*? '*/' -> channel(HIDDEN);
LINE_COMMENT            : '//' .*?[\n\r] -> channel(HIDDEN);
WS                      : [ \t\r\n]     -> channel(HIDDEN);

