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

void dump(std::ostream& os, SynType* type)
{
	if (!type)
	{
		os << "generic";
	}
	else if (CASE(SynTypeIdentifier, type))
	{
		os << _->type.name;
		if (!_->generics.empty())
		{
			os << "<";
			for (size_t i = 0; i < _->generics.size(); ++i)
			{
				if (i != 0) os << ", ";
				dump(os, _->generics[i]);
			}
			os << ">";
		}
	}
	else if (CASE(SynTypeGeneric, type))
	{
		os << "'" << _->type.name;
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
			if (i != 0) os << ", ";
			dump(os, _->args[i]);
		}

		os << ") -> ";
		dump(os, _->result);
	}
	else if (CASE(SynTypeRecord, type))
	{
		os << "[";

		for (size_t i = 0; i < _->members.size(); ++i)
		{
			if (i != 0) os << ", ";
			os << _->members[i].name.name << ": ";
			dump(os, _->members[i].type);
		}

		os << "]";
	}
	else if (CASE(SynTypeTuple, type))
	{
		os << "(";

		for (size_t i = 0; i < _->members.size(); ++i)
		{
			if (i != 0) os << ", ";
			dump(os, _->members[i]);
		}

		os << ")";
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
	case SynUnaryOpRefGet: os << "unary !"; break;
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
	case SynBinaryOpRefSet: os << "binary :="; break;
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
	else if (CASE(SynBooleanLiteral, root))
		os << (_->value ? "true" : "false") << "\n";
	else if (CASE(SynCharacterLiteral, root))
		os << "\'" << _->value << "\'\n";
	else if (CASE(SynArrayLiteral, root))
	{
		os << "[\n";
		for (size_t i = 0; i < _->elements.size(); ++i)
			dump(os, _->elements[i], indent + 1);
		indentout(os, indent);
		os << "]\n";
	}
	else if (CASE(SynTupleLiteral, root))
	{
		os << "(\n";
		for (size_t i = 0; i < _->elements.size(); ++i)
			dump(os, _->elements[i], indent + 1);
		indentout(os, indent);
		os << ")\n";
	}
	else if (CASE(SynRecordDefinition, root))
	{
		os << "record " << _->name.name << "<";
		for (size_t i = 0; i < _->generics.size(); ++i)
		{
			if (i != 0) os << ", ";
			dump(os, _->generics[i]);
		}
		os << ">\n";
		indentout(os, indent);
		os << "{\n";
		for (size_t i = 0; i < _->type->members.size(); ++i)
		{
			indentout(os, indent + 1);
			os <<  _->type->members[i].name.name << ": ";
			dump(os, _->type->members[i].type);
			os << "\n";
		}
		indentout(os, indent);
		os << "}\n";
	}
	else if (CASE(SynUnionDefinition, root))
	{
		os << "union " << _->name.name << "<";
		for (size_t i = 0; i < _->generics.size(); ++i)
		{
			if (i != 0) os << ", ";
			dump(os, _->generics[i]);
		}
		os << ">\n";
		for (size_t i = 0; i < _->members.size(); ++i)
		{
			indentout(os, indent);
			os << "| ";
			os <<  _->members[i].name.name;
			if (_->members[i].type)
			{
				os << ": ";
				dump(os, _->members[i].type);
			}
			os << "\n";
		}
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

		for (size_t i = 0; i < _->arg_values.size(); ++i)
		{
			indentout(os, indent);
			if (_->arg_names.empty())
				os << "arg " << i << "\n";
			else
				os << "arg " << _->arg_names[i].name << "\n";
			dump(os, _->arg_values[i], indent + 1);
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
	else if (CASE(SynLetVars, root))
	{
		os << "let (";
		for (size_t i = 0; i < _->vars.size(); ++i)
		{
			if (i != 0) os << ", ";
			os << _->vars[i].name.name << ": ";
			dump(os, _->vars[i].type);
		}
		os << ") =\n";
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
	else if (CASE(SynForInRangeDo, root))
	{
		os << "for " << _->var.name.name << ": ";
		dump(os, _->var.type);
		os << " =\n";
		indentout(os, indent);
		os << "from\n";
		dump(os, _->start, indent + 1);
		indentout(os, indent);
		os << "to\n";
		dump(os, _->end, indent + 1);
		indentout(os, indent);
		os << "do\n";
		dump(os, _->body, indent + 1);
	}
	else if (CASE(SynMatchNumber, root))
	{
		os << _->value;
	}
	else if (CASE(SynMatchBoolean, root))
	{
		os << (_->value ? "true" : "false");
	}
	else if (CASE(SynMatchArray, root))
	{
		os << "[";
		for (size_t i = 0; i < _->elements.size(); ++i)
		{
			if (i != 0)
				os << ", ";
			dump(os, _->elements[i], 0);
		}
		os << "]";
	}
	else if (CASE(SynMatchTuple, root))
	{
		os << "(";
		for (size_t i = 0; i < _->elements.size(); ++i)
		{
			if (i != 0)
				os << ", ";
			dump(os, _->elements[i], 0);
		}
		os << ")";
	}
	else if (CASE(SynMatchTypeSimple, root))
	{
		os << _->type.name << " " << _->alias.name;
	}
	else if (CASE(SynMatchTypeComplex, root))
	{
		os << _->type.name << "(";
		for (size_t i = 0; i < _->arg_values.size(); ++i)
		{
			if (i != 0)
				os << ", ";
			if (!_->arg_names.empty())
				os << _->arg_names[i].name << " = ";
			dump(os, _->arg_values[i], 0);
		}
		os << ")";
	}
	else if (CASE(SynMatchPlaceholder, root))
	{
		os << _->alias.name.name;
		if (_->alias.type)
		{
			os << ": ";
			dump(os, _->alias.type);
		}
	}
	else if (CASE(SynMatchPlaceholderUnnamed, root))
	{
		os << "_";
	}
	else if (CASE(SynMatchOr, root))
	{
		for (size_t i = 0; i < _->options.size(); ++i)
		{
			if (i != 0)
				os << " | ";
			dump(os, _->options[i], 0);
		}
	}
	else if (CASE(SynMatchIf, root))
	{
		dump(os, _->match, 0);
		os << " if\n";
		dump(os, _->condition, indent + 1);
	}
	else if (CASE(SynMatchWith, root))
	{
		os << "match\n";
		dump(os, _->variable, indent + 1);
		indentout(os, indent);
		os << "with\n";
		for (size_t i = 0; i < _->variants.size(); ++i)
		{
			indentout(os, indent);
			os << "| ";
			dump(os, _->variants[i], 0);
			os << " ->\n";
			dump(os, _->expressions[i], indent + 1);
		}
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

void dump(std::ostream& os, SynBase* root)
{
	dump(os, root, 0);
}

void dump(std::ostream& os, PrettyPrintContext& context, Type* type)
{
	os << typeName(type, context);
}

void dump(std::ostream& os, BindingBase* binding)
{
	if (CASE(BindingFunction, binding))
	{
		os << "function " << _->target->name << "(";
		for (size_t i = 0; i < _->arg_names.size(); i++)
			os << (i == 0 ? "" : ", ") << _->arg_names[i];
		os << ")";
		if (dynamic_cast<BindingFreeFunction*>(binding))
			os << " with no context";
	}
	else if (CASE(BindingLocal, binding))
		os << _->target->name;
	else
	{
		assert(!"Unknown binding");
	}
}

void dump(std::ostream& os, PrettyPrintContext& context, MatchCase* case_)
{
	if (CASE(MatchCaseAny, case_))
	{
		os << (_->alias ? _->alias->name : "_");
		if (_->type)
		{
			os << ": ";
			dump(os, context, _->type);
		}
	}
	else if (CASE(MatchCaseBoolean, case_))
	{
		os << (_->value ? "true" : "false");
		if (_->type)
		{
			os << ": ";
			dump(os, context, _->type);
		}
	}
	else if (CASE(MatchCaseNumber, case_))
	{
		os << _->value;
		if (_->type)
		{
			os << ": ";
			dump(os, context, _->type);
		}
	}
	else if (CASE(MatchCaseValue, case_))
	{
		dump(os, _->value);
		if (_->type)
		{
			os << ": ";
			dump(os, context, _->type);
		}
	}
	else if (CASE(MatchCaseArray, case_))
	{
		os << "[";
		for (size_t i = 0; i < _->elements.size(); ++i)
		{
			if (i != 0)
				os << ", ";
			dump(os, context, _->elements[i]);
		}
		os << "]";
	}
	else if (CASE(MatchCaseMembers, case_))
	{
		dump(os, context, _->type);
		os << "(";
		for (size_t i = 0; i < _->member_values.size(); ++i)
		{
			if (i != 0)
				os << ", ";
			if (!_->member_names.empty())
				os << _->member_names[i] << " = ";
			dump(os, context, _->member_values[i]);
		}
		os << ")";
	}
	else if (CASE(MatchCaseUnion, case_))
	{
		TypeInstance* ti = dynamic_cast<TypeInstance*>(_->type);
		TypePrototypeUnion* tu = dynamic_cast<TypePrototypeUnion*>(*ti->prototype);

		os << tu->member_names[_->tag] << " of ";

		dump(os, context, _->pattern);
	}
	else if (CASE(MatchCaseOr, case_))
	{
		for (size_t i = 0; i < _->options.size(); ++i)
		{
			if (i != 0)
				os << " | ";
			dump(os, context, _->options[i]);
		}
	}
	else if (CASE(MatchCaseIf, case_))
	{
		dump(os, context, _->match);
		os << " if\n";
		dump(os, _->condition);
	}
	else
	{
		assert(!"Unknown match case");
	}
}

void dump(std::ostream& os, PrettyPrintContext& context, TypeFunction* funty, BindingTarget* target, const std::vector<BindingTarget*>& args)
{
	os << target->name;
	os << "(";

	for (size_t i = 0; i < args.size(); i++)
	{
		if (i != 0)
			os << ", ";

		os << args[i]->name;
		os << ": ";

		dump(os, context, args[i]->type);
	}

	os << "): ";

	dump(os, context, funty->result);
}

void dump(std::ostream& os, PrettyPrintContext& context, Expr* root, int indent)
{
	assert(root);

	indentout(os, indent);
	os << "//";
	dump(os, context, root->type);
	os << "\n";
	indentout(os, indent);

	if (CASE(ExprUnit, root))
		os << "()\n";
	else if (CASE(ExprNumberLiteral, root))
		os << _->value << "\n";
	else if (CASE(ExprBooleanLiteral, root))
		os << (_->value ? "true" : "false") << "\n";
	else if (CASE(ExprCharacterLiteral, root))
		os << "\'" << _->value << "\'\n";
	else if (CASE(ExprArrayLiteral, root))
	{
		os << "[\n";
		for (size_t i = 0; i < _->elements.size(); ++i)
			dump(os, context, _->elements[i], indent + 1);
		indentout(os, indent);
		os << "]\n";
	}
	else if (CASE(ExprTupleLiteral, root))
	{
		os << "(\n";
		for (size_t i = 0; i < _->elements.size(); ++i)
			dump(os, context, _->elements[i], indent + 1);
		indentout(os, indent);
		os << ")\n";
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
		dump(os, context, _->expr, indent + 1);
	}
	else if (CASE(ExprBinaryOp, root))
	{
		dump(os, _->op);
		os << "\n";
		dump(os, context, _->left, indent + 1);
		dump(os, context, _->right, indent + 1);
	}
	else if (CASE(ExprCall, root))
	{
		os << "call\n";
		dump(os, context, _->expr, indent + 1);

		for (size_t i = 0; i < _->args.size(); ++i)
		{
			indentout(os, indent);
			os << "arg " << i << "\n";
			dump(os, context, _->args[i], indent + 1);
		}
	}
	else if (CASE(ExprArrayIndex, root))
	{
		os << "index\n";
		dump(os, context, _->arr, indent + 1);

		indentout(os, indent);
		os << "with\n";
		dump(os, context, _->index, indent + 1);
	}
	else if (CASE(ExprArraySlice, root))
	{
		os << "slice of\n";
		dump(os, context, _->arr, indent + 1);

		indentout(os, indent);
		os << "from\n";
		dump(os, context, _->index_start, indent + 1);
		indentout(os, indent);
		os << "to\n";
		if (_->index_end)
		{
			dump(os, context, _->index_end, indent + 1);
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
		os << _->member_name << "\n";

		indentout(os, indent);
		os << "of\n";
		dump(os, context, _->aggr, indent + 1);
	}
	else if (CASE(ExprLetVar, root))
	{
		os << "let " << _->target->name << ": ";
		dump(os, context, _->type);
		os << " =\n";
		dump(os, context, _->body, indent + 1);
	}
	else if (CASE(ExprLetVars, root))
	{
		os << "let (";
		for (size_t i = 0; i < _->targets.size(); ++i)
		{
			if (i != 0) os << ", ";
			if (_->targets[i])
			{
				os << _->targets[i]->name << ": ";
				dump(os, context, _->targets[i]->type);
			}
			else
			{
				os << "_";
			}
		}
		os << ") =\n";
		dump(os, context, _->body, indent + 1);
	}
	else if (CASE(ExprLLVM, root))
	{
		os << "llvm \"" << _->body << "\"\n";
	}
	else if (CASE(ExprLetFunc, root))
	{
		os << "let ";
		dump(os, context, dynamic_cast<TypeFunction*>(_->type), _->target, _->args);
		os << " =\n";
		dump(os, context, _->body, indent + 1);
		if (_->context_target)
		{
			indentout(os, indent);
			os << "context: ";
			dump(os, context, _->context_target->type);
			os << "\n";
		}
		for (size_t i = 0; i < _->externals.size(); ++i)
			dump(os, context, _->externals[i], indent + 1);
	}
	else if (CASE(ExprExternFunc, root))
	{
		os << "extern ";
		dump(os, context, dynamic_cast<TypeFunction*>(_->type), _->target, _->args);
		os << "\n";
	}
	else if (CASE(ExprStructConstructorFunc, root))
	{
		os << "constructor ";
		dump(os, context, dynamic_cast<TypeFunction*>(_->type), _->target, _->args);
		os << "\n";
	}
	else if (CASE(ExprUnionConstructorFunc, root))
	{
		os << "union member #" << _->member_id << " constructor ";
		dump(os, context, dynamic_cast<TypeFunction*>(_->type), _->target, _->args);
		os << "\n";
	}
	else if (CASE(ExprIfThenElse, root))
	{
		os << "if\n";
		dump(os, context, _->cond, indent + 1);
		indentout(os, indent);
		os << "then\n";
		dump(os, context, _->thenbody, indent + 1);
		indentout(os, indent);
		os << "else\n";
		dump(os, context, _->elsebody, indent + 1);
	}
	else if (CASE(ExprForInDo, root))
	{
		os << "for " << _->target->name << ": ";
		dump(os, context, _->target->type);
		os << "\n";
		indentout(os, indent);
		os << "in\n";
		dump(os, context, _->arr, indent + 1);
		indentout(os, indent);
		os << "do\n";
		dump(os, context, _->body, indent + 1);
	}
	else if (CASE(ExprForInRangeDo, root))
	{
		os << "for " << _->target->name << ": ";
		dump(os, context, _->target->type);
		os << "\n";
		indentout(os, indent);
		os << "from\n";
		dump(os, context, _->start, indent + 1);
		indentout(os, indent);
		os << "to\n";
		dump(os, context, _->end, indent + 1);
		indentout(os, indent);
		os << "do\n";
		dump(os, context, _->body, indent + 1);
	}
	else if (CASE(ExprMatchWith, root))
	{
		os << "match\n";
		dump(os, context, _->variable, indent + 1);
		indentout(os, indent);
		os << "with\n";
		for (size_t i = 0; i < _->cases.size(); ++i)
		{
			indentout(os, indent);
			os << "| ";
			dump(os, context, _->cases[i]);
			os << " ->\n";
			dump(os, context, _->expressions[i], indent + 1);
		}
	}
	else if (CASE(ExprBlock, root))
	{
		os << "block\n";
		for (size_t i = 0; i < _->expressions.size(); ++i)
			dump(os, context, _->expressions[i], indent + 1);
	}
	else
	{
		assert(!"Unknown node");
	}
}

void dump(std::ostream& os, Expr* root)
{
	PrettyPrintContext context;
	dump(os, context, root, 0);
}
