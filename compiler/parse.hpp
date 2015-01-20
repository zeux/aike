#pragma once

struct Ast;
struct Tokens;

Ast* parse(const Tokens& tokens);