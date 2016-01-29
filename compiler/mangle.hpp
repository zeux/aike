#pragma once

struct Ty;

string mangleFn(const Str& name, int unnamed, Ty* type, const Arr<Ty*>& tyargs, const function<Ty*(Ty*)>& inst, const string& parent = string());
string mangleType(Ty* type, const function<Ty*(Ty*)>& inst);
string mangleTypeInfo(Ty* type, const function<Ty*(Ty*)>& inst);
string mangleModule(const Str& name);