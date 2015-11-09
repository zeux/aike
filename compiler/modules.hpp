#pragma once

struct Ast;

struct ModuleResolver
{
	function<Ast* (Str)> lookup;
};
