#pragma once

struct Output;
struct Ast;
struct ModuleResolver;

void resolveNames(Output& output, Ast* root, ModuleResolver* moduleResolver);
int resolveMembers(Output& output, Ast* root);

void resolveGatherImports(Ast* root, function<void (Str)> f);
