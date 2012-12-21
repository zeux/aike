#pragma once

#include <iosfwd>

struct Type;
struct BindingBase;
struct Expr;

void dump(std::ostream& os, Type* type);
void dump(std::ostream& os, BindingBase* binding);
void dump(std::ostream& os, Expr* root, int indent = 0);
