#pragma once

struct Ty;

string mangleType(Ty* type);
string mangleFn(const Str& name, int unnamed, Ty* type, const Arr<Ty*>& tyargs, const string& parent = string());
string mangleModule(const Str& name);