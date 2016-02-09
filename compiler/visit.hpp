#pragma once

struct Ast;
struct Ty;

void visitAst(Ast* node, const function<bool (Ast*)>& f);
void visitAstInner(Ast* node, const function<bool (Ast*)>& f);

void visitAstTypes(Ast* node, const function<void (Ty*)>& f);
void visitType(Ty* type, const function<void (Ty*)>& f);

template <typename F, typename FC> inline void visitAst(Ast* node, F f, FC& fc)
{
	visitAst(node, [&](Ast* n) { return f(fc, n); });
}

template <typename F, typename FC> inline void visitAstInner(Ast* node, F f, FC& fc)
{
	visitAstInner(node, [&](Ast* n) { return f(fc, n); });
}

template <typename F, typename FC> inline void visitAstTypes(Ast* node, F f, FC& fc)
{
	visitAstTypes(node, [&](Ty* ty) { f(fc, ty); });
}

template <typename F, typename FC> inline void visitType(Ty* type, F f, FC& fc)
{
	visitType(type, [&](Ty* ty) { f(fc, ty); });
}
