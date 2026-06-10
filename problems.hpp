#pragma once

#include <z3++.h>

#include "model.hpp"

// Library of example synthesis tasks.

// P1: Turn off the rightmost 1-bit, x & (x-1).
Problem turn_off_rightmost_bit_problem(z3::context& ctx);

// P2: Test whether an unsigned integer is of the form 2^n - 1.
Problem test_power_of_two_minus_one_problem(z3::context& ctx);

// P3: Isolate the rightmost 1-bit, x & -x.
Problem isolate_rightmost_one_bit_problem(z3::context& ctx);

// P4: Mask that identifies the rightmost 1-bit and the trailing 0s, x ^ (x-1).
Problem mask_rightmost_one_and_trailing_zeros_problem(z3::context& ctx);

// P5: Right-propagate the rightmost 1-bit, x | (x-1).
Problem right_propagate_rightmost_one_problem(z3::context& ctx);

// P6: Turn on the rightmost 0-bit, x | (x+1).
Problem turn_on_rightmost_zero_bit_problem(z3::context& ctx);

// P7: Isolate the rightmost 0-bit, ~x & (x+1).
Problem isolate_rightmost_zero_bit_problem(z3::context& ctx);

// P8: Mask that identifies the trailing 0s, (x-1) & ~x.
Problem mask_trailing_zeros_problem(z3::context& ctx);

// P9: Absolute value of a signed integer.
Problem absolute_value_problem(z3::context& ctx);

// P10: Test if nlz(x) == nlz(y) (nlz = number of leading zeros).
Problem test_equal_leading_zeros_problem(z3::context& ctx);

// P11: Test if nlz(x) < nlz(y).
Problem test_fewer_leading_zeros_problem(z3::context& ctx);

// P12: Test if nlz(x) <= nlz(y).
Problem test_no_more_leading_zeros_problem(z3::context& ctx);

// P13: Sign function: -1, 0 or 1 according to the sign of x.
Problem sign_function_problem(z3::context& ctx);

// P14: Floor of the average of two integers without overflowing.
Problem floor_average_problem(z3::context& ctx);

// P15: Ceiling of the average of two integers without overflowing.
Problem ceil_average_problem(z3::context& ctx);

// P16: Maximum of two (unsigned) integers, branch-free.
Problem unsigned_max_problem(z3::context& ctx);

// P17: Turn off the rightmost contiguous string of 1-bits.
Problem turn_off_rightmost_contiguous_ones_problem(z3::context& ctx);

// P18: Determine whether an integer is a power of 2.
Problem is_power_of_two_problem(z3::context& ctx);

// P19: Exchange two fields A and B of a register x, where m masks field B and
// k is the number of bits from the end of A to the start of B.
Problem exchange_register_fields_problem(z3::context& ctx);

// P20: Next higher unsigned number with the same number of 1-bits.
Problem next_higher_same_popcount_problem(z3::context& ctx);

// P21: Cycle through three values a, b, c (given x equal to one of them).
Problem cycle_three_values_problem(z3::context& ctx);

// P22: Compute the parity of a word.
Problem compute_parity_problem(z3::context& ctx);

// P23: Count the number of 1-bits in a word.
Problem count_bits_problem(z3::context& ctx);

// P24: Round up to the next highest power of 2.
Problem round_up_to_power_of_two_problem(z3::context& ctx);

// P25: Higher-order half of the product of x and y.
Problem high_half_of_product_problem(z3::context& ctx);
