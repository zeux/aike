#include "dump.hpp"

#include "type.hpp"
#include "typecheck.hpp"

#include <ostream>
#include <cassert>

void indentout(std::ostream& os, int indent)
{
	for (int i = 0; i < indent; ++i)
		os << "  ";
}

void dump(std::ostream& os, Type* type)
{
	type = finalType(type);

	if (CASE(TypeGeneric, type))
		os << "'" << type;
	else if (CASE(TypeUnit, type))
		os << "unit";
	else if (CASE(TypeInt, type))
		os << "int";
	else if (CASE(TypeFloat, type))
		os << "float";
	else if (CASE(TypeBool, type))
		os << "bool";
	else if (CASE(TypeReference, type))
	{
		os << "(";
		dump(os, _->contained);
		os << ")";
		os << " ref";
	}
	else if (CASE(TypeArray, type))
	{
		os << "(";
		dump(os, _->contained);
		os << ")";
		os << "[]";
	}
	else if (CASE(TypeFunction, type))
	{
		os << "(";
		for (size_t i = 0; i < _->args.size(); ++i)
		{
			if (i != 0) os << ",";
			dump(os, _->args[i]);
		}
		os << ")->";
		dump(os, _->result);
	}
	else if (CASE(TypeStructure, type))
	{
		os << "[";
		assert(_->member_names.size() == _->member_types.size());
		for (size_t i = 0; i < _->member_types.size(); ++i)
		{
			if (i != 0) os << ",";
			dump(os, _->member_types[i]);
			os << " " << _->member_names[i];
		}
		os << "]";
	}
	else
	{
		assert(!"Unknown type");
	}
}

void dump(std::ostream& os, BindingBase* binding)
{
	if (CASE(BindingLocal, binding))
		os << _->target->name;
	else if (CASE(BindingFreeFunction, binding))
		os << "free function " << _->target->name;
	else
	{
		assert(!"Unknown binding");
	}
}

void dump(std::ostream& os, SynType* type)
{
	if (!type)
	{
		os << "generic";
	}
	else if (CASE(SynTypeBasic, type))
	{
		os << _->type.name;
	}
	else if (CASE(SynTypeArray, type))
	{
		os << "(";
		dump(os, _->contained_type);
		os << ")";
		os << "[]";
	}
	else if (CASE(SynTypeFunction, type))
	{
		os << "(";

		for (size_t i = 0; i < _->args.size(); ++i)
		{
			os << (i == 0 ? "" : ", ");
			dump(os, _->args[i]);
		}

		os << + ") -> ";
		dump(os, _->result);
	}
	else
	{
		assert(!"Unknown type");
	}
}

void dump(std::ostream& os, SynUnaryOpType op)
{
	switch (op)
	{
	case SynUnaryOpPlus: os << "unary +"; break;
	case SynUnaryOpMinus: os << "unary -"; break;
	case SynUnaryOpNot: os << "unary !"; break;
	default: assert(!"Unknown unary op");
	}
}

void dump(std::ostream& os, SynBinaryOpType op)
{
	switch (op)
	{
	case SynBinaryOpAdd: os << "binary +"; break;
	case SynBinaryOpSubtract: os << "binary -"; break;
	case SynBinaryOpMultiply: os << "binary *"; break;
	case SynBinaryOpDivide: os << "binary /"; break;
	case SynBinaryOpLess: os << "binary <"; break;
	case SynBinaryOpLessEqual: os << "binary <="; break;
	case SynBinaryOpGreater: os << "binary >"; break;
	case SynBinaryOpGreaterEqual: os << "binary >="; break;
	case SynBinaryOpEqual: os << "binary =="; break;
	case SynBinaryOpNotEqual: os << "binary !="; break;
	default: assert(!"Unknown binary op");
	}
}

void dump(std::ostream& os, const SynIdentifier& name, SynType* ret_type, const std::vector<SynTypedVar>& args)
{
	os << name.name;
	os << "(";

	for (size_t i = 0; i < args.size(); i++)
	{
		if (i != 0)
			os << ", ";

		os << args[i].name.name;

		if (args[i].type)
		{
			os << ": ";
			dump(os, args[i].type);
		}
	}

	os << ")";

	if (ret_type)
	{
		os << ": ";
		dump(os, ret_type);
	}
}

void dump(std::ostream& os, SynBase* root, int indent)
{
	assert(root);

	indentout(os, indent);

	if (CASE(SynUnit, root))
		os << "()\n";
	else if (CASE(SynNumberLiteral, root))
		os << _->value << "\n";
	else if (CASE(SynArrayLiteral, root))
	{
		os << "[\n";
		for (size_t i = 0; i < _->elements.size(); ++i)
			dump(os, _->elements[i], indent + 1);
		indentout(os, indent);
		os << "]\n";
	}
	else if (CASE(SynVariableReference, root))
	{
		os << _->name << "\n";
	}
	else if (CASE(SynUnaryOp, root))
	{
		dump(os, _->op);
		os << "\n";
		dump(os, _->expr, indent + 1);
	}
	else if (CASE(SynBinaryOp, root))
	{
		dump(os, _->op);
		os << "\n";
		dump(os, _->left, indent + 1);
		dump(os, _->right, indent + 1);
	}
	else if (CASE(SynCall, root))
	{
		os << "call\n";
		dump(os, _->expr, indent + 1);

		for (size_t i = 0; i < _->args.size(); ++i)
		{
			indentout(os, indent);
			os << "arg " << i << "\n";
			dump(os, _->args[i], indent + 1);
		}
	}
	else if (CASE(SynArrayIndex, root))
	{
		os << "index\n";
		dump(os, _->arr, indent + 1);

		indentout(os, indent);
		os << "with\n";
		dump(os, _->index, indent + 1);
	}
	else if (CASE(SynArraySlice, root))
	{
		os << "slice of\n";
		dump(os, _->arr, indent + 1);

		indentout(os, indent);
		os << "from\n";
		dump(os, _->index_start, indent + 1);
		indentout(os, indent);
		os << "to\n";
		if (_->index_end)
		{
			dump(os, _->index_end, indent + 1);
		}
		else
		{
			indentout(os, indent + 1);
			os << ".length\n";
		}
	}
	else if (CASE(SynMemberAccess, root))
	{
		os << "member\n";
		indentout(os, indent + 1);
		os << _->member.name << "\n";

		indentout(os, indent);
		os << "of\n";
		dump(os, _->aggr, indent + 1);
	}
	else if (CASE(SynLetVar, root))
	{
		os << "let " << _->var.name.name << ": ";
		dump(os, _->var.type);
		os << " =\n";
		dump(os, _->body, indent + 1);
	}
	else if (CASE(SynLLVM, root))
	{
		os << "llvm \"" << _->body << "\"\n";
	}
	else if (CASE(SynLetFunc, root))
	{
		bool anonymous = _->var.name.empty();

		os << (anonymous ? "fun " : "let ");
		dump(os, _->var, _->ret_type, _->args);
		os << (anonymous ? " ->\n" : " =\n");
		dump(os, _->body, indent + 1);
	}
	else if (CASE(SynExternFunc, root))
	{
		os << "extern ";
		dump(os, _->var, _->ret_type, _->args);
		os << "\n";
	}
	else if (CASE(SynIfThenElse, root))
	{
		os << "if\n";
		dump(os, _->cond, indent + 1);
		indentout(os, indent);
		os << "then\n";
		dump(os, _->thenbody, indent + 1);
		indentout(os, indent);
		os << "else\n";
		dump(os, _->elsebody, indent + 1);
	}
	else if (CASE(SynForInDo, root))
	{
		os << "for " << _->var.name.name << ": ";
		dump(os, _->var.type);
		os << " =\n";
		indentout(os, indent);
		os << "in\n";
		dump(os, _->arr, indent + 1);
		indentout(os, indent);
		os << "do\n";
		dump(os, _->body, indent + 1);
	}
	else if (CASE(SynBlock, root))
	{
		os << "SynBlock\n";
		indentout(os, indent);
		os << "{\n";
		for (size_t i = 0; i < _->expressions.size(); ++i)
			dump(os, _->expressions[i], indent + 1);
		indentout(os, indent);
		os << "}\n";
	}
	else
	{
		assert(!"Unknown node");
	}
}

void dump(std::ostream& os, TypeFunction* funty, BindingTarget* target, const std::vector<BindingTarget*>& args)
{
	os << target->name;
	os << "(";

	for (size_t i = 0; i < args.size(); i++)
	{
		if (i != 0)
			os << ", ";

		os << args[i]->name;
		os << ": ";

		dump(os, args[i]->type);
	}

	os << "): ";

	dump(os, funty->result);
}

void dump(std::ostream& os, Expr* root, int indent)
{
	assert(root);

	indentout(os, indent);
	os << "//";
	dump(os, root->type);
	os << "\n";
	indentout(os, indent);

	if (CASE(ExprUnit, root))
		os << "()\n";
	else if (CASE(ExprNumberLiteral, root))
		os << _->value << "\n";
	else if (CASE(ExprArrayLiteral, root))
	{
		os << "[\n";
		for (size_t i = 0; i < _->elements.size(); ++i)
			dump(os, _->elements[i], indent + 1);
		indentout(os, indent);
		os << "]\n";
	}
	else if (CASE(ExprBinding, root))
	{
		dump(os, _->binding);
		os << "\n";
	}
	else if (CASE(ExprBindingExternal, root))
	{
		dump(os, _->context);
		os << "." << _->member_name << " (structure element #" << _->member_index << ")\n";
	}
	else if (CASE(ExprUnaryOp, root))
	{
		dump(os, _->op);
		os << "\n";
		dump(os, _->expr, indent + 1);
	}
	else if (CASE(ExprBinaryOp, root))
	{
		dump(os, _->op);
		os << "\n";
		dump(os, _->left, indent + 1);
		dump(os, _->right, indent + 1);
	}
	else if (CASE(ExprCall, root))
	{
		os << "call\n";
		dump(os, _->expr, indent + 1);

		for (size_t i = 0; i < _->args.size(); ++i)
		{
			indentout(os, indent);
			os << "arg " << i << "\n";
			dump(os, _->args[i], indent + 1);
		}
	}
	else if (CASE(ExprArrayIndex, root))
	{
		os << "index\n";
		dump(os, _->arr, indent + 1);

		indentout(os, indent);
		os << "with\n";
		dump(os, _->index, indent + 1);
	}
	else if (CASE(ExprArraySlice, root))
	{
		os << "slice of\n";
		dump(os, _->arr, indent + 1);

		indentout(os, indent);
		os << "from\n";
		dump(os, _->index_start, indent + 1);
		indentout(os, indent);
		os << "to\n";
		if (_->index_end)
		{
			dump(os, _->index_end, indent + 1);
		}
		else
		{
			indentout(os, indent + 1);
			os << ".length\n";
		}
	}
	else if (CASE(ExprMemberAccess, root))
	{
		os << "member\n";
		indentout(os, indent + 1);
		os << _->member_name << " #" << _->member_index << "\n";

		indentout(os, indent);
		os << "of\n";
		dump(os, _->aggr, indent + 1);
	}
	else if (CASE(ExprLetVar, root))
	{
		os << "let " << _->target->name << ": ";
		dump(os, _->type);
		os << " =\n";
		dump(os, _->body, indent + 1);
	}
	else if (CASE(ExprLLVM, root))
	{
		os << "llvm \"" << _->body << "\"\n";
	}
	else if (CASE(ExprLetFunc, root))
	{
		os << "let ";
		dump(os, dynamic_cast<TypeFunction*>(_->type), _->target, _->args);
		os << " =\n";
		dump(os, _->body, indent + 1);
		if (_->context_target)
		{
			indentout(os, indent);
			os << "context: ";
			dump(os, _->context_target->type);
			os << "\n";
		}
		for (size_t i = 0; i < _->externals.size(); ++i)
			dump(os, _->externals[i], indent + 1);
	}
	else if (CASE(ExprExternFunc, root))
	{
		os << "extern ";
		dump(os, dynamic_cast<TypeFunction*>(_->type), _->target, _->args);
		os << "\n";
	}
	else if (CASE(ExprIfThenElse, root))
	{
		os << "if\n";
		dump(os, _->cond, indent + 1);
		indentout(os, indent);
		os << "then\n";
		dump(os, _->thenbody, indent + 1);
		indentout(os, indent);
		os << "else\n";
		dump(os, _->elsebody, indent + 1);
	}
	else if (CASE(ExprForInDo, root))
	{
		os << "for " << _->target->name << ": ";
		dump(os, _->target->type);
		os << "\n";
		indentout(os, indent);
		os << "in\n";
		dump(os, _->arr, indent + 1);
		indentout(os, indent);
		os << "do\n";
		dump(os, _->body, indent + 1);
	}
	else if (CASE(ExprBlock, root))
	{
		os << "block\n";
		for (size_t i = 0; i < _->expressions.size(); ++i)
			dump(os, _->expressions[i], indent + 1);
	}
	else
	{
		assert(!"Unknown node");
	}
}