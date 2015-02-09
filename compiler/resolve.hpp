#pragma once

struct Output;
struct Ast;

void resolveNames(Output& output, Ast* root);
int resolveMembers(Output& output, Ast* root);
