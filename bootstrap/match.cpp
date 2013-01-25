#include "match.hpp"

#include <cassert>

#include "typecheck.hpp"

MatchCase* clone(MatchCase* pattern)
{
	if (CASE(MatchCaseAny, pattern))
	{
		return new MatchCaseAny(_->type, _->location, _->alias);
	}
	if (CASE(MatchCaseBoolean, pattern))
	{
		return new MatchCaseBoolean(_->type, _->location, _->value);
	}
	if (CASE(MatchCaseNumber, pattern))
	{
		return new MatchCaseNumber(_->type, _->location, _->value);
	}
	if (CASE(MatchCaseCharacter, pattern))
	{
		return new MatchCaseCharacter(_->type, _->location, _->value);
	}
	if (CASE(MatchCaseValue, pattern))
	{
		return new MatchCaseValue(_->type, _->location, _->value);
	}
	if (CASE(MatchCaseArray, pattern))
	{
		std::vector<MatchCase*> elements;

		for (size_t i = 0; i < _->elements.size(); ++i)
			elements.push_back(clone(_->elements[i]));

		return new MatchCaseArray(_->type, _->location, elements);
	}
	if (CASE(MatchCaseMembers, pattern))
	{
		assert(_->member_names.empty()); // Must be resolved in typecheck

		std::vector<MatchCase*> member_values;

		for (size_t i = 0; i < _->member_values.size(); ++i)
			member_values.push_back(clone(_->member_values[i]));

		return new MatchCaseMembers(_->type, _->location, member_values, std::vector<std::string>(), std::vector<Location>());
	}
	if (CASE(MatchCaseUnion, pattern))
	{
		return new MatchCaseUnion(_->type, _->location, _->tag, clone(_->pattern));
	}
	if (CASE(MatchCaseOr, pattern))
	{
		std::vector<MatchCase*> options;

		for (size_t i = 0; i < _->options.size(); ++i)
			options.push_back(clone(_->options[i]));

		return new MatchCaseOr(_->type, _->location, options);
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
	if (CASE(MatchCaseBoolean, pattern))
	{
		auto pattern_ = _;
		if (CASE(MatchCaseBoolean, rhs))
			return pattern_->value == _->value;

		return false;
	}
	if (CASE(MatchCaseNumber, pattern))
	{
		auto pattern_ = _;
		if (CASE(MatchCaseNumber, rhs))
			return pattern_->value == _->value;

		return false;
	}
	if (CASE(MatchCaseCharacter, pattern))
	{
		auto pattern_ = _;
		if (CASE(MatchCaseCharacter, rhs))
			return pattern_->value == _->value;

		return false;
	}
	if (CASE(MatchCaseValue, pattern))
	{
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
	if (CASE(MatchCaseBoolean, pattern))
	{
		return _;
	}
	if (CASE(MatchCaseNumber, pattern))
	{
		return _;
	}
	if (CASE(MatchCaseCharacter, pattern))
	{
		return _;
	}
	if (CASE(MatchCaseValue, pattern))
	{
		return _;
	}
	if (CASE(MatchCaseArray, pattern))
	{
		std::vector<MatchCase*> elements;

		for (size_t i = 0; i < _->elements.size(); ++i)
			elements.push_back(simplify(_->elements[i]));

		return new MatchCaseArray(_->type, _->location, elements);
	}
	if (CASE(MatchCaseMembers, pattern))
	{
		assert(_->member_names.empty()); // Must be resolved in typecheck

		std::vector<MatchCase*> member_values;

		for (size_t i = 0; i < _->member_values.size(); ++i)
			member_values.push_back(simplify(_->member_values[i]));

		// If all members match anything then the whole expression matches anything
		bool matchesAny = true;
		for (size_t i = 0; i < member_values.size() && matchesAny; ++i)
		{
			if (!match(member_values[i], new MatchCaseAny(0, Location(), 0)))
				matchesAny = false;
		}

		if (matchesAny)
			return new MatchCaseAny(0, Location(), 0);

		return new MatchCaseMembers(_->type, _->location, member_values, std::vector<std::string>(), std::vector<Location>());
	}
	if (CASE(MatchCaseUnion, pattern))
	{
		return new MatchCaseUnion(_->type, _->location, _->tag, simplify(_->pattern));
	}
	if (CASE(MatchCaseOr, pattern))
	{
		std::vector<MatchCase*> options;

		for (size_t i = 0; i < _->options.size(); ++i)
			options.push_back(simplify(_->options[i]));

		// Remove cases which are covered by the other cases
		for (std::vector<MatchCase*>::iterator it = options.begin(); it != options.end();)
		{
			bool covered = false;

			for (std::vector<MatchCase*>::iterator subit = options.begin(); subit != options.end() && !covered; ++subit)
			{
				if (it != subit && match(*subit, *it))
					covered = true;
			}

			if (covered)
				it = options.erase(it);
			else
				++it;
		}

		// Join constructors that only have a difference in one member (including union cases that have contructors inside)
		for (size_t i = 0; i < options.size(); ++i)
		{
			// Get the second option
			for (size_t k = i + 1; k < options.size(); ++k)
			{
				MatchCaseMembers* curr_members = dynamic_cast<MatchCaseMembers*>(options[i]);
				MatchCaseMembers* new_members = dynamic_cast<MatchCaseMembers*>(options[k]);

				if (!curr_members || !new_members)
				{
					MatchCaseUnion* curr_tag = dynamic_cast<MatchCaseUnion*>(options[i]);
					MatchCaseUnion* new_tag = dynamic_cast<MatchCaseUnion*>(options[k]);

					// Only for constructors of the same type
					if (!curr_tag || !new_tag || curr_tag->tag != new_tag->tag)
						continue;

					curr_members = dynamic_cast<MatchCaseMembers*>(curr_tag->pattern);
					new_members = dynamic_cast<MatchCaseMembers*>(new_tag->pattern);
				}

				// Pattern must be a member list
				if (!curr_members || !new_members)
					continue;

				// Check if their difference is only in one argument
				size_t mismatch_index = ~0u;
				for (size_t l = 0; l < curr_members->member_values.size(); ++l)
				{
					if (!(match(curr_members->member_values[l], new_members->member_values[l]) && match(new_members->member_values[l], curr_members->member_values[l])))
					{
						if (mismatch_index == ~0u)
						{
							mismatch_index = l;
						}
						else
						{
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
					arg_options->options.push_back(curr_members->member_values[mismatch_index]);
				}

				arg_options->options.push_back(new_members->member_values[mismatch_index]);

				// Create new option array without the option at index 'k' and with the option at index 'i' replaced with a duplicate of 'curr_members' that has member at 'mismatch_index' replaced with 'arg_options'
				std::vector<MatchCase*> next_options;

				for (size_t l = 0; l < options.size(); ++l)
				{
					if (l == k)
						continue;

					if (l == i)
					{
						// Create a copy of curr_members with a member at 'mismatch_index' replaced with 'arg_options'
						std::vector<MatchCase*> next_member_values;

						for (size_t m = 0; m < curr_members->member_values.size(); ++m)
						{
							if (m == mismatch_index)
								next_member_values.push_back(arg_options);
							else
								next_member_values.push_back(curr_members->member_values[m]);
						}

						MatchCaseMembers *next_members = new MatchCaseMembers(curr_members->type, curr_members->location, next_member_values, std::vector<std::string>(), std::vector<Location>());

						// This could be a type match or a union tag match
						if (MatchCaseUnion* curr_tag = dynamic_cast<MatchCaseUnion*>(options[i]))
							next_options.push_back(new MatchCaseUnion(curr_tag->type, curr_tag->location, curr_tag->tag, next_members));
						else
							next_options.push_back(next_members);
					}
					else
					{
						next_options.push_back(options[l]);
					}
				}

				return simplify(new MatchCaseOr(_->type, _->location, next_options));
			}
		}

		// If the members are union tags, they can be merged to MatchAny if all of the tags are fully handled
		if (MatchCaseUnion* first_tag = dynamic_cast<MatchCaseUnion*>(options.empty() ? 0 : options[0]))
		{
			// Every tag must be matched only once (fully handled)
			bool no_duplicates = true;
			for (size_t i = 0; i < options.size() && no_duplicates; ++i)
			{
				for (size_t k = i + 1; k < options.size() && no_duplicates; ++k)
				{
					if (dynamic_cast<MatchCaseUnion*>(options[i])->tag == dynamic_cast<MatchCaseUnion*>(options[k])->tag)
						no_duplicates = false;
				}
			}

			if (no_duplicates)
			{
				bool handle_any = true;
				for (size_t i = 0; i < options.size() && handle_any; ++i)
				{
					if (!match(dynamic_cast<MatchCaseUnion*>(options[i])->pattern, new MatchCaseAny(0, Location(), 0)))
						handle_any = false;
				}

				if (handle_any)
				{
					TypeInstance* inst_type = dynamic_cast<TypeInstance*>(first_tag->type);
					TypePrototypeUnion* union_type = dynamic_cast<TypePrototypeUnion*>(*inst_type->prototype);

					// If it can match any, simplify it to any
					if (union_type->member_types.size() == options.size())
						return new MatchCaseAny(0, Location(), 0);
				}
			}
		}

		// If the members are bool numbers, and the member count covers all the cases, merge them to any
		if (MatchCaseBoolean* first_number = dynamic_cast<MatchCaseBoolean*>(options.empty() ? 0 : options[0]))
		{
			if (options.size() == 2)
				return new MatchCaseAny(0, Location(), 0);
		}

		MatchCaseOr *simplified = new MatchCaseOr(_->type, _->location, options);

		// If it can match any, simplify it to any
		if (match(simplified, new MatchCaseAny(0, Location(), 0)))
			return new MatchCaseAny(0, Location(), 0);

		return simplified;
	}

	assert(!"Unknown case");
	return 0;
}
