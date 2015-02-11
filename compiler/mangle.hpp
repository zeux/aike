#pragma once

struct Ty;

string mangleType(Ty* type);
string mangleFn(const Str& name, Ty* type, const string& parent = string());
string mangleFn(int unnamed, Ty* type, const string& parent = string());

string mangle(const string& name);