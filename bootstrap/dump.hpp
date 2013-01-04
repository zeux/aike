#pragma once

#include <iosfwd>

struct SynBase;
struct Expr;
struct MatchCase;
struct PrettyPrintContext;

void dump(std::ostream& os, Expr* root);
void dump(std::ostream& os, SynBase* root);
void dump(std::ostream& os, PrettyPrintContext& context, MatchCase* case_);
