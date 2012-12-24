#include "typecheck.hpp"

#include "output.hpp"

#include <cassert>

struct Binding
{
	std::string name;
	BindingBase* binding;

	Binding(const std::string& name, BindingBase* binding): name(name), binding(binding)
	{
	}
};

struct TypeBinding
{
	std::string name;
	Type* type;

	TypeBinding(const std::string& name, Type* type): name(name), type(type)
	{
	}
};

struct Environment
{
	std::vector<Binding> bindings;
	std::vector<TypeBinding> types;
};

BindingBase* tryResolveBinding(const std::string& name, Environment& env)
{
	for (size_t i = env.bindings.size(); i > 0; --i)
		if (env.bindings[i - 1].name == name)
			return env.bindings[i - 1].binding;

	return 0;
}

BindingBase* resolveBinding(const std::string& name, Environment& env, const Location& location)
{
	if (BindingBase* result = tryResolveBinding(name, env))
		return result;

	errorf(location, "Unresolved binding %s", name.c_str());
}

Type* tryResolveType(const std::string& name, Environment& env)
{
	for (size_t i = 0; i < env.types.size(); ++i)
		if (env.types[i].name == name)
			return env.types[i].type;

	return 0;
}

Type* resolveType(const std::string& name, Environment& env, const Location& location)
{
	if (name == "")
		return new TypeGeneric();

	if (Type* type = tryResolveType(name, env))
		return type;

	errorf(location, "Unknown type %s", name.c_str());
}

Type* resolveType(SynType* type, Environment& env)
{
	if (!type)
	{
		return new TypeGeneric();
	}

	if (CASE(SynTypeBasic, type))
	{
		return resolveType(_->type.name, env, _->type.location);
	}

	if (CASE(SynTypeArray, type))
	{
		return new TypeArray(resolveType(_->contained_type, env));
	}

	if (CASE(SynTypeFunction, type))
	{
		std::vector<Type*> argtys;

		for (size_t i = 0; i < _->args.size(); ++i)
			argtys.push_back(resolveType(_->args[i], env));

		return new TypeFunction(resolveType(_->result, env), argtys);
	}

	assert(!"Unknown syntax tree type");
	return 0;
}

Type* resolveFunctionType(SynType* rettype, const std::vector<SynTypedVar>& args, Environment& env)
{
	std::vector<Type*> argtys;

	for (size_t i = 0; i < args.size(); ++i)
		argtys.push_back(resolveType(args[i].type, env));

	return new TypeFunction(resolveType(rettype, env), argtys);
}

Expr* resolveExpr(SynBase* node, Environment& env)
{
	assert(node);

	if (CASE(SynUnit, node))
		return new ExprUnit(resolveType("unit", env, _->location), _->location);

	if (CASE(SynNumberLiteral, node))
		return new ExprNumberLiteral(resolveType("int", env, _->location), _->location, _->value);

	if (CASE(SynArrayLiteral, node))
	{
		std::vector<Expr*> elements;

		for (size_t i = 0; i < _->elements.size(); ++i)
			elements.push_back(resolveExpr(_->elements[i], env));

		return new ExprArrayLiteral(elements.empty() ? (Type*)new TypeGeneric() : (Type*)new TypeArray(elements[0]->type), _->location, elements);
	}

	if (CASE(SynVariableReference, node))
	{
		for (size_t i = env.bindings.size(); i > 0; --i)
		{
			if (env.bindings[i - 1].name == _->name)
			{
				Location location = _->location;

				if (CASE(BindingLocal, env.bindings[i - 1].binding))
					return new ExprBinding(_->target->type, location, _);
				else
					return new ExprBinding(new TypeGeneric(), location, env.bindings[i - 1].binding);
			}
		}

		errorf(_->location, "Unresolved variable reference %s", _->name.c_str());
	}

	if (CASE(SynUnaryOp, node))
		return new ExprUnaryOp(new TypeGeneric(), _->location, _->op, resolveExpr(_->expr, env));

	if (CASE(SynBinaryOp, node))
		return new ExprBinaryOp(new TypeGeneric(), _->location, _->op, resolveExpr(_->left, env), resolveExpr(_->right, env));

	if (CASE(SynCall, node))
	{
		std::vector<Expr*> args;
		for (size_t i = 0; i < _->args.size(); ++i)
			args.push_back(resolveExpr(_->args[i], env));

		Expr* function = resolveExpr(_->expr, env);

		TypeFunction* function_type = dynamic_cast<TypeFunction*>(function->type);

		return new ExprCall(function_type ? function_type->result : new TypeGeneric(), _->location, function, args);
	}

	if (CASE(SynArrayIndex, node))
	{
		Expr* arr = resolveExpr(_->arr, env);

		TypeArray* arr_type = dynamic_cast<TypeArray*>(arr->type);

		return new ExprArrayIndex(arr_type ? arr_type->contained : new TypeGeneric(), _->location, arr, resolveExpr(_->index, env));
	}

	if (CASE(SynLetVar, node))
	{
		BindingTarget* target = new BindingTarget(_->var.name.name, resolveType(_->var.type, env));

		Expr* body = resolveExpr(_->body, env);

		// If the type is not defined, take the body type
		if (!_->var.type)
			target->type = body->type;

		env.bindings.push_back(Binding(_->var.name.name, new BindingLocal(target)));

		return new ExprLetVar(target->type, _->location, target, body);
	}

	if (CASE(SynLLVM, node))
		return new ExprLLVM(new TypeGeneric(), _->location, _->body);

	if (CASE(SynLetFunc, node))
	{
		std::vector<BindingTarget*> args;

		for (size_t i = 0; i < _->args.size(); ++i)
		{
			BindingTarget* target = new BindingTarget(_->args[i].name.name, resolveType(_->args[i].type, env));

			args.push_back(target);
			env.bindings.push_back(Binding(_->args[i].name.name, new BindingLocal(target)));
		}

		Type* funty = resolveFunctionType(_->ret_type, _->args, env);

		BindingTarget* target = new BindingTarget(_->var.name, funty);

		Expr* body = resolveExpr(_->body, env);

		for (size_t i = 0; i < _->args.size(); ++i)
			env.bindings.pop_back();

		env.bindings.push_back(Binding(_->var.name, new BindingLocal(target)));

		return new ExprLetFunc(funty, _->location, target, args, body);
	}

	if (CASE(SynExternFunc, node))
	{
		Type* funty = resolveFunctionType(_->ret_type, _->args, env);

		BindingTarget* target = new BindingTarget(_->var.name, funty);

		std::vector<BindingTarget*> args;

		for (size_t i = 0; i < _->args.size(); ++i)
		{
			BindingTarget* target = new BindingTarget(_->args[i].name.name, resolveType(_->args[i].type, env));

			args.push_back(target);
		}

		env.bindings.push_back(Binding(_->var.name, new BindingLocal(target)));

		return new ExprExternFunc(funty, _->location, target, args);
	}

	if (CASE(SynIfThenElse, node))
		return new ExprIfThenElse(new TypeGeneric(), _->location, resolveExpr(_->cond, env), resolveExpr(_->thenbody, env), resolveExpr(_->elsebody, env));

	if (CASE(SynForInDo, node))
	{
		Expr* arr = resolveExpr(_->arr, env);

		TypeArray* arr_type = dynamic_cast<TypeArray*>(arr->type);
		if (!arr_type) errorf(arr->location, "iteration is only available on array types");

		BindingTarget* target = new BindingTarget(_->var.name, arr_type->contained);

		env.bindings.push_back(Binding(_->var.name, new BindingLocal(target)));

		return new ExprForInDo(new TypeUnit(), _->location, target, arr, resolveExpr(_->body, env));
	}

	if (CASE(SynBlock, node))
	{
		ExprBlock *expression = new ExprBlock(new TypeUnit(), _->location);
		
		size_t bind_count = env.bindings.size();
		size_t type_count = env.types.size();

		for (size_t i = 0; i < _->expressions.size(); ++i)
			expression->expressions.push_back(resolveExpr(_->expressions[i], env));

		while (env.bindings.size() > bind_count)
			env.bindings.pop_back();

		while (env.types.size() > type_count)
			env.types.pop_back();

		// Block type is the type of the last expression
		if (!expression->expressions.empty())
			expression->type = expression->expressions.back()->type;

		return expression;
	}

	assert(!"Unrecognized AST type");
	return 0;
}

Expr* resolve(SynBase* root)
{
	Environment env;

	env.types.push_back(TypeBinding("unit", new TypeUnit()));
	env.types.push_back(TypeBinding("int", new TypeInt()));
	env.types.push_back(TypeBinding("float", new TypeFloat()));

	return resolveExpr(root, env);
}

Expr* typecheck(SynBase* root)
{
	return resolve(root);
}