#pragma once

#include <z3++.h>

#include "model.hpp"

// Library of example synthesis tasks.

// Turn off the rightmost 1-bit (x & (x-1)) from { dec : x->x-1, and : (x,y)->x&y }.
Problem turn_off_rightmost_bit_problem(z3::context& ctx);
