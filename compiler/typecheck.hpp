#pragma once

struct Output;
struct Ast;

int typeckPropagate(Output& output, Ast* root);

void typeckVerify(Output& output, Ast* root);
