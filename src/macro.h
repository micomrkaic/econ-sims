/* macro.h — the bridge from a simulation to Cozy.
 *
 * The macro simulations do not solve their models in C. Each one writes its
 * model as text, in Dynare's notation, and hands it to the Cozy interpreter
 * that is linked into the program (third_party/cozy); Cozy's dsge package
 * finds the steady state, linearises and solves, and the decision rule
 * comes back as plain arrays of doubles. C then does what it is good at:
 * stepping the economy forward sixty times a second and drawing it.
 *
 * The dsge package itself rides inside the program as text (dsge_cz.h is
 * generated from third_party/cozy/packages/dsge.cz at build time), so the
 * same code runs from any directory and in the browser.
 *
 * Part of econ-sims. Copyright (C) 2026 Mico Mrkaic.
 * GPL-3.0-or-later; see LICENSE. */
#ifndef ECON_MACRO_H
#define ECON_MACRO_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "cozy.h"
#include "dsge_cz.h"                /* DSGE_CZ: the text of Cozy's dsge package */

static char macro_error[200];       /* why the last call failed, for the window to show */
static double macro_ms;             /* how long the last macro_run took, in milliseconds */

/* Run Cozy source built printf-style. False on a Cozy error, with its first
 * line kept in macro_error. */
static inline bool macro_run(const char *fmt, ...)
{
    static char src[8192];
    va_list ap;
    va_start(ap, fmt); vsnprintf(src, sizeof src, fmt, ap); va_end(ap);
    clock_t t0 = clock();
    const char *out = cozy_eval(src);
    macro_ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;
    if (cozy_ok()) { macro_error[0] = 0; return true; }
    while (*out == ' ') out++;
    snprintf(macro_error, sizeof macro_error, "%s", out);
    char *nl = strchr(macro_error, '\n');
    if (nl) *nl = 0;
    return false;
}

/* Start Cozy and load the dsge package from the text carried in the program. */
static inline bool macro_start(void)
{
    cozy_open();
    cozy_eval(DSGE_CZ);
    if (!cozy_ok()) { snprintf(macro_error, sizeof macro_error, "the dsge package did not load"); return false; }
    return true;
}

/* Copy the Cozy global `name` into dst if it is a rows x cols matrix. */
static inline bool macro_read(const char *name, double *dst, int rows, int cols)
{
    int r, c;
    const double *m = cozy_matrix(name, &r, &c);
    if (!m || r != rows || c != cols) {
        snprintf(macro_error, sizeof macro_error, "Cozy's %s is %d x %d, expected %d x %d", name, r, c, rows, cols);
        return false;
    }
    memcpy(dst, m, (size_t)rows * cols * sizeof(double));
    return true;
}

/* The Cozy source that defines a model: `let NAME = dsge.model({...});`.
 * The equations are given as C strings, so the window can show exactly the
 * text Cozy compiled. `extra` is further fields of the record (params, sd,
 * init, steady), each ending in a comma. */
static inline bool macro_model(const char *name, const char *vars, const char *shocks, const char *extra,
                               const char *const *eqs, int neq)
{
    static char list[4096];
    int n = 0;
    for (int i = 0; i < neq; i++) n += snprintf(list + n, sizeof list - (size_t)n, "%s\"%s\"", i ? "; " : "", eqs[i]);
    return macro_run("let %s = dsge.model({vars = \"%s\", shocks = \"%s\", %s model = [%s]});", name, vars, shocks, extra, list);
}

#endif
