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
	if (CASE(TypeGeneric, type))
		os << "'" << type;
	else if (CASE(TypeUnit, type))
		os << "unit";
	else if (CASE(TypeInt, type))
		os << "int";
	else if (CASE(TypeFloat, type))
		os << "float";
	else if (CASE(TypeFunction, type))
	{
		os << "(";
		for (size_t i = 0; i < _->args.size(); ++i)
		{
			if (i != 0) os << ",";
			dump(os, _->args[i]);
		}
		os << ")=>";
		dump(os, _->result);
	}
	else
	{
		assert(!"Unknown type");
	}
}

void dump(std::ostream& os, BindingBase* binding)
{
	if (CASE(BindingLet, binding))
		os << _->target->name;
	else if (CASE(BindingFunarg, binding))
		os << _->target->name << "#" << _->index;
	else
	{
		assert(!"Unknown binding");
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

void dump(std::ostream& os, Expr* root, int indent)
{
	indentout(os, indent);
	os << "//";
	dump(os, root->type);
	os << "\n";
	indentout(os, indent);

	if (CASE(ExprUnit, root))
		os << "()\n";
	else if (CASE(ExprLiteralNumber, root))
		os << _->value << "\n";
	else if (CASE(ExprBinding, root))
	{
		dump(os, _->binding);
		os << "\n";
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
	else if (CASE(ExprLetVar, root))
	{
		os << "let " << _->target->name << ": ";
		dump(os, _->vartype);
		os << " =\n";
		dump(os, _->body, indent + 1);
		indentout(os, indent);
		os << "in\n";
		dump(os, _->expr, indent + 1);
	}
	else if (CASE(ExprLLVM, root))
	{
		os << "llvm \"" << _->body << "\"";
	}
	else if (CASE(ExprLetFunc, root))
	{
		os << "let " << _->target->name << ": ";
		dump(os, _->funtype);
		os << " =\n";
		dump(os, _->body, indent + 1);
		indentout(os, indent);
		os << "in\n";
		dump(os, _->expr, indent + 1);
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
	else if (CASE(ExprSequence, root))
	{
		dump(os, _->head, indent);
		dump(os, _->tail, indent);
	}
	else
	{
		assert(!"Unknown node");
	}
}