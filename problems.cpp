#include <cstdint>
#include <functional>
#include <vector>

#include "problems.hpp"

// ---------------------------------------------------------------------------
// Helpers for building component libraries.
//
// A component's semantics is a relation `result == f(params...)`.  Constants
// (shift amounts, masks, +-1, ...) are folded into the component, so the
// synthesiser only ever wires lines together and never invents literals.
// Comparisons and reductions, which are predicates in QF_BV, are modelled here
// as components that return a full-width 0 or 1.
// ---------------------------------------------------------------------------
namespace {

using z3::expr;
using z3::expr_vector;
using z3::ite;

unsigned width_of(const expr& e) { return e.get_sort().bv_size(); }

// A literal of the same bit-width as `like`.
expr lit(const expr& like, uint64_t v) { return like.ctx().bv_val(v, width_of(like)); }

// A Bool predicate rendered as a full-width 0/1 value.
expr as_bit(const expr& width_like, const expr& pred) {
    return ite(pred, lit(width_like, 1), lit(width_like, 0));
}

// Adapt a single-output spec to the multi-output signature of Problem::spec.
std::function<expr(const expr_vector&, const expr_vector&)>
out1(std::function<expr(const expr_vector&, const expr&)> f) {
    return [f = std::move(f)](const expr_vector& in, const expr_vector& out) {
        return f(in, out[0]);
    };
}

BitwiseComponentSpec un(const char* name, std::function<expr(const expr&)> f) {
    return BitwiseComponentSpec{name, 1, [f = std::move(f)](const expr_vector& in, const expr& r) {
                             return r == f(in[0]);
                         }};
}

BitwiseComponentSpec bin(const char* name, std::function<expr(const expr&, const expr&)> f) {
    return BitwiseComponentSpec{name, 2, [f = std::move(f)](const expr_vector& in, const expr& r) {
                             return r == f(in[0], in[1]);
                         }};
}

// --- Common components -----------------------------------------------------
BitwiseComponentSpec inc() { return un("inc", [](const expr& a) { return a + 1; }); }
BitwiseComponentSpec dec() { return un("dec", [](const expr& a) { return a - 1; }); }
BitwiseComponentSpec neg() { return un("neg", [](const expr& a) { return -a; }); }
BitwiseComponentSpec bnot() { return un("not", [](const expr& a) { return ~a; }); }

BitwiseComponentSpec and_() { return bin("and", [](const expr& a, const expr& b) { return a & b; }); }
BitwiseComponentSpec or_() { return bin("or", [](const expr& a, const expr& b) { return a | b; }); }
BitwiseComponentSpec xor_() { return bin("xor", [](const expr& a, const expr& b) { return a ^ b; }); }
BitwiseComponentSpec add_() { return bin("add", [](const expr& a, const expr& b) { return a + b; }); }
BitwiseComponentSpec sub_() { return bin("sub", [](const expr& a, const expr& b) { return a - b; }); }
BitwiseComponentSpec mul_() { return bin("mul", [](const expr& a, const expr& b) { return a * b; }); }
BitwiseComponentSpec udiv_() {
    return bin("udiv", [](const expr& a, const expr& b) { return z3::udiv(a, b); });
}

// Shifts by a compile-time amount, and by a runtime operand (P19).
BitwiseComponentSpec lshr_by(unsigned k) {
    return BitwiseComponentSpec{"shr" + std::to_string(k), 1,
                         [k](const expr_vector& in, const expr& r) {
                             return r == z3::lshr(in[0], static_cast<int>(k));
                         }};
}
BitwiseComponentSpec shr_top() { // logical shift right by width-1: isolates the top bit
    return un("shr", [](const expr& a) { return z3::lshr(a, static_cast<int>(width_of(a)) - 1); });
}
BitwiseComponentSpec ashr_top() { // arithmetic shift right by width-1: broadcasts the sign bit
    return un("ashr", [](const expr& a) { return z3::ashr(a, static_cast<int>(width_of(a)) - 1); });
}
BitwiseComponentSpec lshr_var() {
    return bin("lshr", [](const expr& a, const expr& b) { return z3::lshr(a, b); });
}
BitwiseComponentSpec shl_var() {
    return bin("shl", [](const expr& a, const expr& b) { return z3::shl(a, b); });
}

// And with a folded-in mask.
BitwiseComponentSpec and_mask(const char* name, uint64_t m) {
    return un(name, [m](const expr& a) { return a & lit(a, m); });
}

// Multiply by a folded-in constant.
BitwiseComponentSpec mul_const(const char* name, uint64_t c) {
    return un(name, [c](const expr& a) { return a * lit(a, c); });
}

// Comparisons and reductions, returning a full-width 0/1.
BitwiseComponentSpec ule_() {
    return bin("ule", [](const expr& a, const expr& b) { return as_bit(a, z3::ule(a, b)); });
}
BitwiseComponentSpec ugt_() {
    return bin("ugt", [](const expr& a, const expr& b) { return as_bit(a, z3::ugt(a, b)); });
}
BitwiseComponentSpec uge_() {
    return bin("uge", [](const expr& a, const expr& b) { return as_bit(a, z3::uge(a, b)); });
}
BitwiseComponentSpec eq_() {
    return bin("eq", [](const expr& a, const expr& b) { return as_bit(a, a == b); });
}
BitwiseComponentSpec redor_() {
    return un("redor", [](const expr& a) { return as_bit(a, a != lit(a, 0)); });
}
BitwiseComponentSpec lnot_() {
    return un("lnot", [](const expr& a) { return as_bit(a, a == lit(a, 0)); });
}

} // namespace

// ===========================================================================
// P1: Turn off the rightmost 1-bit (x & (x-1)).
// ===========================================================================
Problem turn_off_rightmost_bit_problem(z3::context&) {
    std::vector library = {dec(), and_()};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        return out == (x & (x - 1));
    };
    return Problem{1, library, out1(spec)};
}

// ===========================================================================
// P2: Test whether an unsigned integer is of the form 2^n - 1, via x & (x+1).
// ===========================================================================
Problem test_power_of_two_minus_one_problem(z3::context&) {
    std::vector library = {inc(), and_()};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        return out == (x & (x + 1));
    };
    return Problem{1, library, out1(spec)};
}

// ===========================================================================
// P3: Isolate the rightmost 1-bit (x & -x).
// ===========================================================================
Problem isolate_rightmost_one_bit_problem(z3::context&) {
    std::vector library = {neg(), and_()};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        return out == (x & (-x));
    };
    return Problem{1, library, out1(spec)};
}

// ===========================================================================
// P4: Mask the rightmost 1-bit and trailing 0s (x ^ (x-1)).
// ===========================================================================
Problem mask_rightmost_one_and_trailing_zeros_problem(z3::context&) {
    std::vector library = {dec(), xor_()};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        return out == (x ^ (x - 1));
    };
    return Problem{1, library, out1(spec)};
}

// ===========================================================================
// P5: Right-propagate the rightmost 1-bit (x | (x-1)).
// ===========================================================================
Problem right_propagate_rightmost_one_problem(z3::context&) {
    std::vector library = {dec(), or_()};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        return out == (x | (x - 1));
    };
    return Problem{1, library, out1(spec)};
}

// ===========================================================================
// P6: Turn on the rightmost 0-bit (x | (x+1)).
// ===========================================================================
Problem turn_on_rightmost_zero_bit_problem(z3::context&) {
    std::vector library = {inc(), or_()};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        return out == (x | (x + 1));
    };
    return Problem{1, library, out1(spec)};
}

// ===========================================================================
// P7: Isolate the rightmost 0-bit (~x & (x+1)).
// ===========================================================================
Problem isolate_rightmost_zero_bit_problem(z3::context&) {
    std::vector library = {bnot(), inc(), and_()};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        return out == (~x & (x + 1));
    };
    return Problem{1, library, out1(spec)};
}

// ===========================================================================
// P8: Mask the trailing 0s ((x-1) & ~x).
// ===========================================================================
Problem mask_trailing_zeros_problem(z3::context&) {
    std::vector library = {dec(), bnot(), and_()};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        return out == ((x - 1) & ~x);
    };
    return Problem{1, library, out1(spec)};
}

// ===========================================================================
// P9: Absolute value, |x| = (x ^ s) - s with s = x >>_a (width-1).
// ===========================================================================
Problem absolute_value_problem(z3::context&) {
    std::vector library = {ashr_top(), xor_(), sub_()};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        const expr s = z3::ashr(x, static_cast<int>(width_of(x)) - 1);
        return out == ((x ^ s) - s);
    };
    return Problem{1, library, out1(spec)};
}

// ===========================================================================
// P10: Test if nlz(x) == nlz(y), via (x^y) <=u (x&y).
// ===========================================================================
Problem test_equal_leading_zeros_problem(z3::context&) {
    std::vector library = {and_(), xor_(), ule_()};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        const expr& y = in[1];
        return out == as_bit(x, z3::ule(x ^ y, x & y));
    };
    return Problem{2, library, out1(spec)};
}

// ===========================================================================
// P11: Test if nlz(x) < nlz(y), via (x & ~y) >u y.
// ===========================================================================
Problem test_fewer_leading_zeros_problem(z3::context&) {
    std::vector library = {bnot(), and_(), ugt_()};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        const expr& y = in[1];
        return out == as_bit(x, z3::ugt(x & ~y, y));
    };
    return Problem{2, library, out1(spec)};
}

// ===========================================================================
// P12: Test if nlz(x) <= nlz(y), via (x & ~y) <=u y.
// ===========================================================================
Problem test_no_more_leading_zeros_problem(z3::context&) {
    std::vector library = {bnot(), and_(), ule_()};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        const expr& y = in[1];
        return out == as_bit(x, z3::ule(x & ~y, y));
    };
    return Problem{2, library, out1(spec)};
}

// ===========================================================================
// P13: Sign function, (x >>_a (w-1)) | (-x >>_l (w-1)).
// ===========================================================================
Problem sign_function_problem(z3::context&) {
    std::vector library = {ashr_top(), neg(), shr_top(), or_()};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        const int top = static_cast<int>(width_of(x)) - 1;
        return out == (z3::ashr(x, top) | z3::lshr(-x, top));
    };
    return Problem{1, library, out1(spec)};
}

// ===========================================================================
// P14: Floor of the average, (x & y) + ((x ^ y) >>_l 1).
// ===========================================================================
Problem floor_average_problem(z3::context&) {
    std::vector library = {and_(), xor_(), lshr_by(1), add_()};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        const expr& y = in[1];
        return out == ((x & y) + z3::lshr(x ^ y, 1));
    };
    return Problem{2, library, out1(spec)};
}

// ===========================================================================
// P15: Ceiling of the average, (x | y) - ((x ^ y) >>_l 1).
// ===========================================================================
Problem ceil_average_problem(z3::context&) {
    std::vector library = {or_(), xor_(), lshr_by(1), sub_()};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        const expr& y = in[1];
        return out == ((x | y) - z3::lshr(x ^ y, 1));
    };
    return Problem{2, library, out1(spec)};
}

// ===========================================================================
// P16: Unsigned max, ((x ^ y) & -(x >=u y)) ^ y.
// ===========================================================================
Problem unsigned_max_problem(z3::context&) {
    std::vector library = {xor_(), uge_(), neg(), and_(), xor_()};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        const expr& y = in[1];
        return out == ite(z3::uge(x, y), x, y);
    };
    return Problem{2, library, out1(spec)};
}

// ===========================================================================
// P17: Turn off the rightmost contiguous string of 1-bits,
//      (((x | (x-1)) + 1) & x).
// ===========================================================================
Problem turn_off_rightmost_contiguous_ones_problem(z3::context&) {
    std::vector library = {dec(), or_(), inc(), and_()};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        return out == (((x | (x - 1)) + 1) & x);
    };
    return Problem{1, library, out1(spec)};
}

// ===========================================================================
// P18: Determine whether x is a power of 2: (x & (x-1)) == 0 and x != 0.
// ===========================================================================
Problem is_power_of_two_problem(z3::context&) {
    std::vector library = {dec(), and_(), redor_(), redor_(), lnot_(), and_()};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        const expr cleared = (x - 1) & x;
        return out == as_bit(x, (cleared == lit(x, 0)) && (x != lit(x, 0)));
    };
    return Problem{1, library, out1(spec)};
}

// ===========================================================================
// P19: Exchange fields A and B of register x; m masks B, k is their distance.
// ===========================================================================
Problem exchange_register_fields_problem(z3::context&) {
    std::vector library = {lshr_var(), xor_(), and_(), shl_var(), xor_(), xor_()};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        const expr& m = in[1];
        const expr& k = in[2];
        const expr t = (x ^ z3::lshr(x, k)) & m;
        return out == ((z3::shl(t, k) ^ t) ^ x);
    };
    return Problem{3, library, out1(spec)};
}

// ===========================================================================
// P20: Next higher unsigned number with the same number of 1-bits.
// ===========================================================================
Problem next_higher_same_popcount_problem(z3::context&) {
    std::vector library = {neg(), and_(), add_(), xor_(), lshr_by(2), udiv_(), or_()};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        const expr smallest = x & (-x);
        const expr ripple = x + smallest;
        const expr ones = z3::udiv(z3::lshr(x ^ ripple, 2), smallest);
        return out == (ripple | ones);
    };
    return Problem{1, library, out1(spec)};
}

// ===========================================================================
// P21: Cycle through three values a, b, c (x is one of them; return the next).
// ===========================================================================
Problem cycle_three_values_problem(z3::context&) {
    std::vector library = {eq_(),  neg(), xor_(), eq_(), neg(),
                           xor_(), and_(), and_(), xor_(), xor_()};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        const expr& a = in[1];
        const expr& b = in[2];
        const expr& c = in[3];
        const expr is_c = -as_bit(x, x == c); // all-ones if x == c, else 0
        const expr is_a = -as_bit(x, x == a);
        const expr t = (is_c & (a ^ c)) ^ (is_a & (b ^ c));
        return out == (t ^ c);
    };
    return Problem{4, library, out1(spec)};
}

// ===========================================================================
// P22: Compute the parity of a word, via the magic-multiply nibble fold.
// ===========================================================================
Problem compute_parity_problem(z3::context&) {
    std::vector library = {lshr_by(1),
                           xor_(),
                           lshr_by(2),
                           xor_(),
                           and_mask("and11111111", 0x11111111),
                           mul_const("mul11111111", 0x11111111),
                           lshr_by(28),
                           and_mask("and1", 0x1)};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        const expr o2 = z3::lshr(x, 1) ^ x;
        const expr o4 = o2 ^ z3::lshr(o2, 2);
        const expr o6 = (o4 & lit(x, 0x11111111)) * lit(x, 0x11111111);
        return out == (z3::lshr(o6, 28) & lit(x, 0x1));
    };
    return Problem{1, library, out1(spec)};
}

// ===========================================================================
// P23: Count the number of 1-bits, via the SWAR bit-population sequence.
// ===========================================================================
Problem count_bits_problem(z3::context&) {
    std::vector library = {lshr_by(1),
                           and_mask("and55555555", 0x55555555),
                           sub_(),
                           and_mask("and33333333", 0x33333333),
                           lshr_by(2),
                           and_mask("and33333333", 0x33333333),
                           add_(),
                           lshr_by(4),
                           add_(),
                           and_mask("and0F0F0F0F", 0x0F0F0F0F)};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        const expr m1 = lit(x, 0x55555555), m2 = lit(x, 0x33333333), m4 = lit(x, 0x0F0F0F0F);
        const expr a = x - (z3::lshr(x, 1) & m1);
        const expr b = (a & m2) + (z3::lshr(a, 2) & m2);
        const expr c = (b + z3::lshr(b, 4)) & m4;
        return out == c;
    };
    return Problem{1, library, out1(spec)};
}

// ===========================================================================
// P24: Round up to the next highest power of 2.  Decrement, smear the high bit
// down with shifts 1, 2, 4, ... < width, then increment.
// ===========================================================================
Problem round_up_to_power_of_two_problem(z3::context&) {
    std::vector<BitwiseComponentSpec> library = {dec()};
    for (unsigned s = 1; s < BV_LENGTH; s *= 2) {
        library.push_back(lshr_by(s));
        library.push_back(or_());
    }
    library.push_back(inc());

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        expr v = x - 1;
        for (unsigned s = 1; s < width_of(x); s *= 2)
            v = v | z3::lshr(v, static_cast<int>(s));
        return out == (v + 1);
    };
    return Problem{1, library, out1(spec)};
}

// ===========================================================================
// P25: Higher-order half of the product of x and y, computed from half-word
// multiplies (the word is split into two halves of width/2 bits).
// ===========================================================================
Problem high_half_of_product_problem(z3::context&) {
    std::vector library = {and_mask("andFFFF", 0xFFFF),
                           lshr_by(BV_LENGTH / 2),
                           and_mask("andFFFF", 0xFFFF),
                           lshr_by(BV_LENGTH / 2),
                           mul_(),
                           mul_(),
                           mul_(),
                           mul_(),
                           lshr_by(BV_LENGTH / 2),
                           add_(),
                           and_mask("andFFFF", 0xFFFF),
                           lshr_by(BV_LENGTH / 2),
                           add_(),
                           lshr_by(BV_LENGTH / 2),
                           add_(),
                           add_()};

    const auto spec = [](const expr_vector& in, const expr& out) {
        const expr& x = in[0];
        const expr& y = in[1];
        const int h = static_cast<int>(width_of(x)) / 2;
        const expr lo = lit(x, (uint64_t{1} << h) - 1);
        const expr x0 = x & lo, x1 = z3::lshr(x, h);
        const expr y0 = y & lo, y1 = z3::lshr(y, h);
        const expr t = (x1 * y0) + z3::lshr(x0 * y0, h);
        const expr w = (x0 * y1) + (t & lo);
        return out == ((z3::lshr(w, h) + z3::lshr(t, h)) + (x1 * y1));
    };
    return Problem{2, library, out1(spec)};
}

// ===========================================================================
// P26/P27: First-order masked AND / OR on Boolean shares (SecAnd / SecOr),
// after Biryukov/Dinu/Le Corre/Udovenko, "Optimal First-Order Boolean Masking
// for Embedded IoT Devices" (CARDIS 2017).  The inputs are the four shares
// x1, x2, y1, y2 of the secrets x = x1^x2 and y = y1^y2; the two outputs are
// shares z1, z2 with z1^z2 = x&y (resp. x|y).  No intermediate value may leak
// information about the sensitive functions below, so the two output shares
// are never combined and no fresh randomness is needed.
// ===========================================================================
namespace {

// The paper's sensitive set K = {x, y, x&y, ~x&y, x&~y, ~x&~y}, as Boolean
// functions of the input share bits (x1, x2, y1, y2).
std::vector<std::function<bool(const std::vector<bool>&)>> share_pair_sensitive() {
    const auto x = [](const std::vector<bool>& v) { return v[0] != v[1]; };
    const auto y = [](const std::vector<bool>& v) { return v[2] != v[3]; };
    return {
        x,
        y,
        [=](const std::vector<bool>& v) { return x(v) && y(v); },
        [=](const std::vector<bool>& v) { return !x(v) && y(v); },
        [=](const std::vector<bool>& v) { return x(v) && !y(v); },
        [=](const std::vector<bool>& v) { return !x(v) && !y(v); },
    };
}

} // namespace

Problem sec_and_problem(z3::context&) {
    // The paper's optimal basic-ISA multiset: 7 operations.
    std::vector library = {bnot(), and_(), and_(), or_(), or_(), xor_(), xor_()};

    const auto spec = [](const expr_vector& in, const expr_vector& out) {
        const expr x = in[0] ^ in[1];
        const expr y = in[2] ^ in[3];
        return (out[0] ^ out[1]) == (x & y);
    };
    Problem p{4, library, spec};
    p.num_outputs = 2;
    p.sensitive_fns = share_pair_sensitive();
    return p;
}

Problem sec_or_problem(z3::context&) {
    // The paper's optimal basic-ISA multiset: 6 operations.
    std::vector library = {and_(), and_(), or_(), or_(), xor_(), xor_()};

    const auto spec = [](const expr_vector& in, const expr_vector& out) {
        const expr x = in[0] ^ in[1];
        const expr y = in[2] ^ in[3];
        return (out[0] ^ out[1]) == (x | y);
    };
    Problem p{4, library, spec};
    p.num_outputs = 2;
    p.sensitive_fns = share_pair_sensitive();
    return p;
}
