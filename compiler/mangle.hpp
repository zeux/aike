#pragma once

struct Ty;

string mangleFn(const Str& name, int unnamed, Ty* type, const Arr<Ty*>& tyargs, const string& parent = string());
string mangleType(Ty* type);
string mangleTypeInfo(Ty* type);
string mangleModule(const Str& name);