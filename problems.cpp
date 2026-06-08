#include <cstdint>

#include "problems.hpp"

// Turn off the rightmost 1-bit (x & (x-1)) from { dec : x->x-1, and : (x,y)->x&y }.
Problem turn_off_rightmost_bit_problem(z3::context& ctx) {
    const std::vector library = {
        ComponentSpec{
            "dec", 1,
            [](const z3::expr_vector& in, const z3::expr& r) { return r == in[0] - 1; }},
        ComponentSpec{
            "and", 2,
            [](const z3::expr_vector& in, const z3::expr& r) { return r == (in[0] & in[1]); }}
    };

    const auto spec = [](const z3::expr_vector& in, const z3::expr& out) {
        const z3::expr& x = in[0];
        z3::context& ctx = x.ctx();
        const unsigned width = x.get_sort().bv_size();

        z3::expr result = x;                         // x == 0: nothing to clear
        z3::expr seen_set_bit = ctx.bool_val(false); // did a lower bit already set?
        for (unsigned i = 0; i < width; ++i) {
            z3::expr bit_set = x.extract(i, i) == ctx.bv_val(1, 1);
            z3::expr is_lowest = bit_set && !seen_set_bit;
            z3::expr cleared = x & ~ctx.bv_val(uint64_t{1} << i, width);
            result = z3::ite(is_lowest, cleared, result);
            seen_set_bit = seen_set_bit || bit_set;
        }
        return out == result;
    };

    return Problem {1, library, spec};
}
