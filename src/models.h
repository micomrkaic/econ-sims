/* models.h — the two macro models, as text.
 *
 * Each model is a handful of equations in Dynare's notation: x(+1) is next
 * quarter's x as expected today, x(-1) last quarter's. The simulations hand
 * this text to Cozy (see macro.h) and draw it in their windows, so what is
 * on screen is what was solved.
 *
 * Part of econ-sims. Copyright (C) 2026 Mico Mrkaic.
 * GPL-3.0-or-later; see LICENSE. */
#ifndef ECON_MODELS_H
#define ECON_MODELS_H

#include "macro.h"

/* ---- the New Keynesian model (Gali 2015, ch. 3) ---------------------- */
/* Output gap x, inflation pi, the policy rate i and the real rate r, all in
 * percent a quarter from the steady state; two disturbances, to the natural
 * rate of interest (rn: demand) and to the policy rule (v). Cozy's name for
 * the model is `nk`. */
enum { NK_X = 0, NK_PI, NK_I, NK_R, NK_RN, NK_V, NK_NV };
enum { NK_EN = 0, NK_EV, NK_NS };
#define NK_BETA 0.99
static const char *const NK_EQS[] = {
    "pi = beta * pi(+1) + kappa * x",
    "x = x(+1) - (1 / sigma) * (i - pi(+1) - rn)",
    "i = phi_pi * pi + phi_x * x + v",
    "r = i - pi(+1)",
    "rn = rho_n * rn(-1) + en",
    "v = rho_v * v(-1) + ev",
};
#define NK_NEQ 6

static inline bool nk_define(void)
{
    return macro_model("nk", "x pi i r rn v", "en ev",
                       "params = {beta = 0.99, sigma = 1.0, kappa = 0.1275, phi_pi = 1.5, phi_x = 0.125, rho_n = 0.9, rho_v = 0.5},"
                       " init = {x = 0.0, pi = 0.0, i = 0.0, r = 0.0, rn = 0.0, v = 0.0},", NK_EQS, NK_NEQ);
}

/* ---- the real business cycle model ----------------------------------- */
/* Output y, consumption c, investment i, hours n, capital k (in levels) and
 * log technology a. Cozy's name for the model is `rbc`. */
enum { RBC_Y = 0, RBC_C, RBC_I, RBC_N, RBC_K, RBC_A, RBC_NV };
#define RBC_ALPHA 0.33
#define RBC_BETA 0.99
static const char *const RBC_EQS[] = {
    "c^(-sigma) = beta * c(+1)^(-sigma) * (alpha * y(+1) / k + 1 - delta)",
    "psi * n^phi * c^sigma = (1 - alpha) * y / n",
    "y = exp(a) * k(-1)^alpha * n^(1 - alpha)",
    "k = (1 - delta) * k(-1) + i",
    "y = c + i",
    "a = rho * a(-1) + e",
};
#define RBC_NEQ 6

/* The steady state has a closed form, given here as a Cozy function; the
 * dsge package checks that it solves the equations before it uses it. */
static inline bool rbc_define(void)
{
    return macro_model("rbc", "y c i n k a", "e",
                       "params = {alpha = 0.33, beta = 0.99, delta = 0.025, rho = 0.95, sigma = 1.0, phi = 1.0, psi = 8.0},"
                       " steady = fn p -> ("
                       " let ky = p.alpha / (1 / p.beta - 1 + p.delta);"                 /* capital per unit of output */
                       " let yn = ky^(p.alpha / (1 - p.alpha));"                         /* output per hour */
                       " let cn = (1 - p.delta * ky) * yn;"                              /* consumption per hour */
                       " let n = ((1 - p.alpha) * yn / (p.psi * cn^p.sigma))^(1 / (p.phi + p.sigma));"
                       " {y = yn * n, c = cn * n, i = p.delta * ky * yn * n, n = n, k = ky * yn * n, a = 0.0}),",
                       RBC_EQS, RBC_NEQ);
}

/* Cozy source that solves `rbc` and restates the rule in percent deviations:
 * the model is in levels, so each row of P and Q is divided by its variable's
 * steady state and each column of P multiplied by it (technology is a log
 * already). Leaves s_ (the solution), ss_ (steady state) and t_ (the
 * solution in percent, with the shock's standard deviation set). printf
 * arguments: rho, sigma, phi, delta, the shock's standard deviation. */
#define RBC_SOLVE \
    "let s_ = dsge.solve(rbc, {rho = %.6f, sigma = %.6f, phi = %.6f, delta = %.6f});" \
    "let ss_ = s_.ybar; let d_ = 1 ./ ss_; d_[6] = 1.0;" \
    "let P_ = (d_ * ones(1, 6)) .* s_.P ./ (ones(6, 1) * d_'); let Q_ = d_ .* s_.Q;" \
    "let t_ = setfield(setfield(setfield(s_, \"P\", P_), \"Q\", Q_), \"sd\", [%.6f]);"

#endif
