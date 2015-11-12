#pragma once

struct Ast;
struct Location;

struct ModuleResolver
{
	function<Ast* (Str)> lookup;
};

void moduleGatherImports(Ast* root, function<void (Str, Location)> f);