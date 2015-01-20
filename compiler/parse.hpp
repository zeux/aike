#pragma once

struct Ast;
struct Tokens;
struct Output;

Ast* parse(Output& output, const Tokens& tokens);