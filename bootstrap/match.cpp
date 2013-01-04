#include "match.hpp"

#include <cassert>

#include "typecheck.hpp"

MatchCase* clone(MatchCase* pattern)
{
	if (CASE(MatchCaseAny, pattern))
	{
		return new MatchCaseAny(_->type, _->location, _->alias);
	}
	if (CASE(MatchCaseNumber, pattern))
	{
		return new MatchCaseNumber(_->type, _->location, _->number);
	}
	if (CASE(MatchCaseArray, pattern))
	{
		std::vector<MatchCase*> clone_elements;

		for (size_t i = 0; i < _->elements.size(); ++i)
			clone_elements.push_back(clone(_->elements[i]));

		return new MatchCaseArray(_->type, _->location, clone_elements);
	}
	if (CASE(MatchCaseMembers, pattern))
	{
		assert(_->member_names.empty()); // Must be resolved in typecheck

		std::vector<MatchCase*> clone_members;

		for (size_t i = 0; i < _->member_values.size(); ++i)
			clone_members.push_back(clone(_->member_values[i]));

		return new MatchCaseMembers(_->type, _->location, clone_members, std::vector<std::string>());
	}
	if (CASE(MatchCaseUnion, pattern))
	{
		return new MatchCaseUnion(_->type, _->location, _->tag, clone(_->pattern));
	}
	if (CASE(MatchCaseOr, pattern))
	{
		MatchCaseOr* copy = new MatchCaseOr(_->type, _->location);

		for (size_t i = 0; i < _->options.size(); ++i)
			copy->addOption(clone(_->options[i]));

		return copy;
	}

	assert(!"Unknown case");
	return 0;
}

bool match(MatchCase* pattern, MatchCase* rhs)
{
	if (CASE(MatchCaseAny, pattern))
	{
		return true;
	}
	if (CASE(MatchCaseNumber, pattern))
	{
		auto pattern_ = _;
		if (CASE(MatchCaseNumber, rhs))
			return pattern_->number == _->number;

		return false;
	}
	if (CASE(MatchCaseArray, pattern))
	{
		auto pattern_ = _;
		if (CASE(MatchCaseArray, rhs))
		{
			if (pattern_->elements.size() != _->elements.size())
				return false;

			for (size_t i = 0; i < pattern_->elements.size(); ++i)
			{
				if (!match(pattern_->elements[i], _->elements[i]))
					return false;
			}

			return true;
		}

		return false;
	}
	if (CASE(MatchCaseMembers, pattern))
	{
		assert(_->member_names.empty()); // Must be resolved in typecheck

		auto pattern_ = _;
		if (CASE(MatchCaseMembers, rhs))
		{
			for (size_t i = 0; i < pattern_->member_values.size(); ++i)
			{
				if (!match(pattern_->member_values[i], _->member_values[i]))
					return false;
			}

			return true;
		}

		return false;
	}
	if (CASE(MatchCaseUnion, pattern))
	{
		auto pattern_ = _;
		if (CASE(MatchCaseUnion, rhs))
			return pattern_->tag == _->tag && match(pattern_->pattern, _->pattern);

		return false;
	}
	if (CASE(MatchCaseOr, pattern))
	{
		for (size_t i = 0; i < _->options.size(); ++i)
		{
			if (match(_->options[i], rhs))
				return true;
		}

		return false;
	}

	assert(!"Unknown case");
	return false;
}

MatchCase* simplify(MatchCase* pattern)
{
	if (CASE(MatchCaseAny, pattern))
	{
		return _;
	}
	if (CASE(MatchCaseNumber, pattern))
	{
		return _;
	}
	if (CASE(MatchCaseArray, pattern))
	{
		for (size_t i = 0; i < _->elements.size(); ++i)
			_->elements[i] = simplify(_->elements[i]);

		// Array match cannot be simplified further
		return _;
	}
	if (CASE(MatchCaseMembers, pattern))
	{
		assert(_->member_names.empty()); // Must be resolved in typecheck

		for (size_t i = 0; i < _->member_values.size(); ++i)
			_->member_values[i] = simplify(_->member_values[i]);

		// If all member match anything then the whole expression matches anything
		bool matchesAny = true;
		for (size_t i = 0; i < _->member_values.size() && matchesAny; ++i)
		{
			if (!match(_->member_values[i], new MatchCaseAny(0, Location(), 0)))
				matchesAny = false;
		}

		if (matchesAny)
			return new MatchCaseAny(0, Location(), 0);

		return _;
	}
	if (CASE(MatchCaseUnion, pattern))
	{
		_->pattern = simplify(_->pattern);

		return _;
	}
	if (CASE(MatchCaseOr, pattern))
	{
		for (size_t i = 0; i < _->options.size(); ++i)
			_->options[i] = simplify(_->options[i]);

		// Remove cases which are covered by the other cases
		for (std::vector<MatchCase*>::iterator it = _->options.begin(); it != _->options.end();)
		{
			bool covered = false;

			for (std::vector<MatchCase*>::iterator subit = _->options.begin(); subit != _->options.end() && !covered; ++subit)
			{
				if (it != subit && match(*subit, *it))
					covered = true;
			}

			if (covered)
				it = _->options.erase(it);
			else
				++it;
		}

		// Join duplicate union cases that have a one member difference in the constructor
		if (MatchCaseUnion* first_tag = dynamic_cast<MatchCaseUnion*>(_->options.empty() ? 0 : _->options[0]))
		{
			// For every option
			for (size_t i = 0; i < _->options.size(); ++i)
			{
				MatchCaseUnion* curr_tag = dynamic_cast<MatchCaseUnion*>(_->options[i]);

				// Get the second option
				std::vector<MatchCase*>::iterator subit = _->options.begin() + i + 1;

				for (; subit != _->options.end(); ++subit)
				{
					MatchCaseUnion* new_tag = dynamic_cast<MatchCaseUnion*>(*subit);

					// Only for constructors of the same type
					if (curr_tag->tag != new_tag->tag)
						continue;

					MatchCaseMembers* curr_members = dynamic_cast<MatchCaseMembers*>(curr_tag->pattern);
					MatchCaseMembers* new_members = dynamic_cast<MatchCaseMembers*>(new_tag->pattern);

					// Pattern must be a member list
					if (!curr_members || !new_members)
						continue;

					// Check if their difference is only in one argument
					size_t mismatch_index = ~0u;
					for (size_t k = 0; k < curr_members->member_values.size(); ++k)
					{
						if (!(match(curr_members->member_values[k], new_members->member_values[k]) && match(new_members->member_values[k], curr_members->member_values[k])))
						{
							if (mismatch_index == ~0u)
							{
								mismatch_index = k;
							}else{
								mismatch_index = ~0u;
								break;
							}
						}
					}

					// More than one mismatch
					if (mismatch_index == ~0u)
						continue;

					MatchCaseOr* arg_options = dynamic_cast<MatchCaseOr*>(curr_members->member_values[mismatch_index]);
					
					if (!arg_options)
					{
						arg_options = new MatchCaseOr(0, Location());
						arg_options->addOption(curr_members->member_values[mismatch_index]);
					}
					if (dynamic_cast<MatchCaseOr*>(new_members->member_values[mismatch_index]))
						arg_options = arg_options;

					arg_options->addOption(new_members->member_values[mismatch_index]);
					curr_members->member_values[mismatch_index] = arg_options;

					_->options.erase(subit);

					return simplify(_);
				}
			}
		}

		// If the members are union tags, they can be merged to MatchAny if all of the tags are fully handled
		if (MatchCaseUnion* first_tag = dynamic_cast<MatchCaseUnion*>(_->options.empty() ? 0 : _->options[0]))
		{
			// Every tag must be matched only once (fully handled)
			bool no_duplicates = true;
			for (size_t i = 0; i < _->options.size() && no_duplicates; ++i)
			{
				for (size_t k = i + 1; k < _->options.size() && no_duplicates; ++k)
				{
					if (dynamic_cast<MatchCaseUnion*>(_->options[i])->tag == dynamic_cast<MatchCaseUnion*>(_->options[k])->tag)
						no_duplicates = false;
				}
			}

			if (no_duplicates)
			{
				bool handle_any = true;
				for (size_t i = 0; i < _->options.size() && handle_any; ++i)
				{
					if (!match(dynamic_cast<MatchCaseUnion*>(_->options[i])->pattern, new MatchCaseAny(0, Location(), 0)))
						handle_any = false;
				}

				if (handle_any)
				{
					TypeUnion* union_type = dynamic_cast<TypeUnion*>(first_tag->type);

					// If it can match any, simplify it to any
					if (union_type->member_types.size() == _->options.size())
						return new MatchCaseAny(0, Location(), 0);
				}
			}
		}

		// If it can match any, simplify it to any
		if (match(_, new MatchCaseAny(0, Location(), 0)))
			return new MatchCaseAny(0, Location(), 0);

		return _;
	}

	assert(!"Unknown case");
	return 0;
}
