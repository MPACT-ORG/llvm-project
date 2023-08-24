//===- register.cpp - Process tablegen register definitions ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Process register definitions in the tablegen file. We capture register
// definitions and register class definitions.  Register classes can use
// expressions to specify class members so we need to process these
// expressions:
//      expr : '(' 'add' expr (',' expr )* ')'
//           | '(' 'sub' expr (',' expr)* ')'
//           | '(' 'and' expr (',' expr)* ')'
//           | '(' 'shl' expr ',' <count> ')'
//           | '(' 'rotl' expr ',' <count> ')'
//           | '(' 'rotr' expr ',' <count> ')'
//           | '(' 'trunc' expr ',' <count> ')'
//           | '(' 'sequence' "prefix", <first>, <last> ')'
//           | '(' 'decimate' expr  <number> ')'
//           | register_name
//           | register_class_name
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <string>

#include "scan.h"

namespace mpact {
namespace mdl {
namespace scan {

using DagExprFunc = RegSet (MachineDescription::*)(char *&);

struct DagExpressions {
  const char *operation;
  RegSet (MachineDescription::*func)(char *&);
};

DagExpressions DagExpr[] = {
    {"add", &MachineDescription::RegClassAdd},
    {"sub", &MachineDescription::RegClassSub},
    {"and", &MachineDescription::RegClassAnd},
    {"shl", &MachineDescription::RegClassAnd},
    {"rotl", &MachineDescription::RegClassRotl},
    {"rotr", &MachineDescription::RegClassRotr},
    {"trunc", &MachineDescription::RegClassTrunc},
    {"sequence", &MachineDescription::RegClassSequence},
    {"decimate", &MachineDescription::RegClassDecimate},
    {nullptr, nullptr},
};

//----------------------------------------------------------------------------
// Parse a MemberList expression and generate the specified register list.
//----------------------------------------------------------------------------
RegSet MachineDescription::ParseExpr(char *&input) {
  RegSet regs;

  if (strlen(input) == 0)
    return regs;
  for (; *input && isspace(*input); input++) {
  }

  // If we have a subexpression, look up the operator and perform the function.
  if (input[0] == '(') {
    char *name = input + 1;
    int len = strcspn(input, " )");
    if (input[len] == ' ') {
      input[len] = 0;
      input += len + 1;
      for (int i = 0; DagExpr[i].operation != nullptr; i++)
        if (!strcmp(name, DagExpr[i].operation))
          return (this->*DagExpr[i].func)(input);
      return regs;
    }
    // If this an expression of the form (op), just return an empty set.
    input += len + 1;
    return regs;
  }

  // If the expression isn't a subexpression, parse the name.
  int len = strcspn(input, " ,)");
  char delim = input[len];
  input[len] = 0;
  std::string name = input;
  *(input += len) = delim;

  // if this is a register class, add all the members to the return set.
  if (register_class_list_.count(name))
    return GetMemberList(register_class_list_[name]);
  // If it's a register, add just the one register.
  if (register_dict_.count(name))
    regs.emplace_back(name);
  // If it's neither a register or a register class, just return an empty
  // list. We may want to enhance this for register class tuples at some
  // point.
  return regs;
}

// Process an add operator: '(' 'add' <expr> (',' <expr>)* ')'.
RegSet MachineDescription::RegClassAdd(char *&input) {
  RegSet regs;
  for (;;) {
    for (auto &item : ParseExpr(input))
      if (std::find(regs.begin(), regs.end(), item) == regs.end())
        regs.push_back(item);
    if (input[0] != ',')
      break;
    input++;
  }
  input = strchr(input, ')') + 1;
  return regs;
}

// Process a sub operator: '(' 'sub' <expr> (',' <expr>)* ')'.
RegSet MachineDescription::RegClassSub(char *&input) {
  auto regs = ParseExpr(input);
  while (input[0] == ',') {
    auto sub = ParseExpr(++input);

    for (auto &item : sub) {
      auto found = std::find(regs.begin(), regs.end(), item);
      if (found != regs.end())
        regs.erase(found, found + 1);
    }
  }
  input = strchr(input, ')') + 1;
  return regs;
}

// Process an and operator: '(' 'and' <expr>, <expr>) ')'.
RegSet MachineDescription::RegClassAnd(char *&input) {
  auto rega = ParseExpr(input);
  if ((input[0] != ','))
    return rega;
  auto regb = ParseExpr(++input);
  input = strchr(input, ')') + 1;

  RegSet result;
  for (auto &item : rega)
    if (std::find(regb.begin(), regb.end(), item) != regb.end())
      result.push_back(item);
  return result;
}

// Process a shl operator: '(' 'shl' <class> ',' <count> ')'.
RegSet MachineDescription::RegClassShl(char *&input) {
  auto regs = ParseExpr(input);
  unsigned count;
  if (sscanf(input, ",%u)", &count) != 1)
    return regs;
  input = strchr(input, ')') + 1;

  if (count > regs.size())
    count = regs.size();
  regs.erase(regs.begin(), regs.begin() + count); // erase first N regs
  return regs;
}

// Process a rotl operator: '(' 'rotl' <class> ',' <count> ')'.
RegSet MachineDescription::RegClassRotl(char *&input) {
  auto regs = ParseExpr(input);
  unsigned count;
  if (sscanf(input, ",%u)", &count) != 1)
    return regs;
  input = strchr(input, ')') + 1;

  if (count >= regs.size())
    return regs;
  auto rot = regs;
  regs.erase(regs.begin(), regs.begin() + count);  // erase the first N regs
  rot.resize(count);                               // truncate to first N regs
  regs.insert(regs.end(), rot.begin(), rot.end()); // concatenate them
  return regs;
}

// Process a rotr operator: '(' 'rotr' <class> ',' <count> ')'.
RegSet MachineDescription::RegClassRotr(char *&input) {
  auto regs = ParseExpr(input);
  unsigned count;
  if (sscanf(input, ",%u)", &count) != 1)
    return regs;
  input = strchr(input, ')') + 1;

  if (count >= regs.size())
    return regs;
  count = regs.size() - count; // N = size - count

  auto rot = regs;
  regs.erase(regs.begin(), regs.begin() + count);  // erase the first N regs
  rot.resize(count);                               // truncate to first N regs
  regs.insert(regs.end(), rot.begin(), rot.end()); // Concatenate them
  return regs;
}

// Process a trunc operator: '(' 'trunc' <class> ',' <count> ')'.
RegSet MachineDescription::RegClassTrunc(char *&input) {
  auto regs = ParseExpr(input);
  unsigned count;
  if (sscanf(input, ",%u)", &count) != 1)
    return regs;
  input = strchr(input, ')') + 1;

  if (count < regs.size())
    regs.resize(count); // truncate to first n regs
  return regs;
}

// Process a decimate operator: '(' 'decimate' <class> ',' <count> ')'.
RegSet MachineDescription::RegClassDecimate(char *&input) {
  auto regs = ParseExpr(input);
  RegSet decimated;

  unsigned count;
  if (sscanf(input, ",%u)", &count) != 1)
    return regs;
  input = strchr(input, ')') + 1;

  for (unsigned i = 0; i < regs.size(); i += count)
    decimated.push_back(regs[i]);
  return decimated;
}

//----------------------------------------------------------------------------
// Parse and generate a sequence of registers:
//              '(' 'sequence "format", <first>, <last> ')'
//----------------------------------------------------------------------------
RegSet MachineDescription::RegClassSequence(char *&input) {
  RegSet regs;
  char format[100];
  unsigned first, last;
  if (sscanf(input, "\"%s %u,%u)", format, &first, &last) != 3)
    return regs;
  if (last < first)
    return regs;
  input = strchr(input, ')') + 1;

  if (auto *quote = strchr(format, '"')) {
    *quote = 0; // strip off trailing quote
    for (unsigned id = first; id <= last; id++) {
      char reg_name[100];
      snprintf(reg_name, sizeof(reg_name), format, id);
      regs.emplace_back(reg_name);
    }
  }
  return regs;
}

//----------------------------------------------------------------------------
// Scan a register class member list, collect all the registers.
// If its register set is empty, go populate the class.
//----------------------------------------------------------------------------
RegSet MachineDescription::GetMemberList(RegisterClass *reg_class) {
  if (reg_class->parsed())
    return reg_class->registers();
  reg_class->set_parsed();

  if (reg_class->registers().empty())
    if (char *members = reg_class->member_list())
      reg_class->set_registers(ParseExpr(members));

  return reg_class->registers();
}

//----------------------------------------------------------------------------
// Given a register name, find its prefix and its (optional) index (suffix).
// This will aid in finding logical sets of registers (in FindRegisterSets()).
//----------------------------------------------------------------------------
std::string GetRegisterPrefix(const std::string &reg, int &regid) {
  // Find trailing digits.
  unsigned suffix = reg.length();
  while (std::isdigit(reg.at(suffix - 1)))
    suffix--;

  // if there's no suffix, return -1 for index, and the whole name.
  if (suffix == reg.length()) {
    regid = -1;
    return reg;
  }

  // But don't include leading 0s in the suffix (like R03), since this would
  // confuse our coalescing efforts (turning R0, R1, R2 into R[0-2]).
  for (; reg.at(suffix) == '0' && suffix + 1 < reg.length(); suffix++) {
  }

  regid = std::stoi(reg.substr(suffix, reg.length()).c_str(), nullptr, 10);
  return reg.substr(0, suffix);
}

//----------------------------------------------------------------------------
// Given the set of defined register names, discover register classes.
// Combine adjacent register definitions that have identical prefixes and
// sequential suffixes.  Note: This is purely for readability of the output.
//----------------------------------------------------------------------------
void MachineDescription::FindRegisterSets() {
  // If a register can be combined with the previously defined register
  // (same prefix, sequential suffixes), combine them. Otherwise add
  // the new register to the list.
  for (auto const &reg : registers_) {
    if (!register_sets_.empty() && register_sets_.back().index() != -1 &&
        reg.prefix() == register_sets_.back().prefix() && reg.index() != -1 &&
        reg.index() == register_sets_.back().last_index() + 1)
      register_sets_.back().set_last_index(reg.index());
    else
      register_sets_.push_back(reg);
  }
}

} // namespace scan
} // namespace mdl
} // namespace mpact
