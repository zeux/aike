#pragma once

struct Output;
struct Ast;

void typeckResolve(Output& output, Ast* root);
void typeckInstantiate(Output& output, Ast* root);

int typeckPropagate(Output& output, Ast* root);

void typeckVerify(Output& output, Ast* root);
