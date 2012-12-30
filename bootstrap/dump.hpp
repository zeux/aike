#pragma once

#include <iosfwd>

struct SynBase;
struct Expr;

void dump(std::ostream& os, Expr* root);
void dump(std::ostream& os, SynBase* root);