#pragma once

struct Location;
struct Output;
struct Ast;

struct ModuleResolver
{
	function<Ast* (Str)> lookup;
};

void moduleGatherImports(Ast* root, function<void (Str, Location)> f);

vector<unsigned int> moduleSort(Output& output, const vector<Ast*>& modules);