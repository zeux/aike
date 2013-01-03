#pragma once

struct MatchCase;

MatchCase* clone(MatchCase* pattern);
bool match(MatchCase* pattern, MatchCase* rhs);
MatchCase* simplify(MatchCase* pattern);
