#pragma once

struct Ast;

struct ModuleResolver
{
	function<Ast* (Str)> lookup;
};

void moduleGatherImports(Ast* root, function<void (Str)> f);