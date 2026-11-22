/* wealth.c — the wealth distribution of households facing income risk.
 *
 * The standard incomplete-markets household (Bewley, Huggett, Aiyagari):
 * it earns a wage of 1 when employed, a benefit b when not, and for the
 * occasional spell a multiple of the wage; it moves between the three
 * states at random, and cannot insure. It can only save or borrow at
 * the interest rate r, down to a borrowing limit, and it chooses consumption
 * to maximise expected CRRA utility. The decision rule is solved by the
 * endogenous grid method each time a slider moves; 20,000 households then
 * follow it, and the picture is their wealth. Everyone is identical ex ante:
 * the inequality is the residue of luck.
 *
 * A period is a year. Wealth is measured in years of wages.
 *
 * Part of econ-sims. Copyright (C) 2026 Mico Mrkaic.
 * GPL-3.0-or-later; see LICENSE. */
#include "ui.h"
#include "rng.h"

/* ---- the household problem ------------------------------------------- */
#define NA 200                    /* asset grid points */
#define A_MAX 60.0                /* top of the asset grid */
#define GRID_CURVE 2.5            /* grid points crowd towards the borrowing limit */
enum { UNEMPLOYED = 0, EMPLOYED = 1, HIGH = 2, NE = 3 };   /* the income states */

typedef struct {
    /* parameters */
    double r, beta, sigma;        /* interest rate, discount factor, risk aversion */
    double limit;                 /* borrowing limit: assets >= -limit */
    double lose, find;            /* yearly chance of losing a job, of finding one */
    double benefit;               /* income when unemployed (the wage is 1) */
    double high_pay;              /* income in a high-pay spell, in wages */
    double up, down;              /* yearly chance a worker starts such a spell, and that it ends */
    /* solution, by state */
    double a[NA];                 /* the asset grid */
    double c[NE][NA];             /* consumption at each grid point */
    double next[NE][NA];          /* assets chosen for next year */
    int iterations;
} Household;

static double income(const Household *h, int e) { return e == UNEMPLOYED ? h->benefit : e == EMPLOYED ? 1.0 : h->high_pay; }
/* P(e2 | e): the unemployed find ordinary jobs; workers lose them or move up;
 * a high-pay spell ends in an ordinary job. */
static double stay(const Household *h, int e, int e2)
{
    if (e == UNEMPLOYED) return e2 == EMPLOYED ? h->find : e2 == UNEMPLOYED ? 1.0 - h->find : 0.0;
    if (e == EMPLOYED) return e2 == UNEMPLOYED ? h->lose : e2 == HIGH ? h->up : 1.0 - h->lose - h->up;
    return e2 == EMPLOYED ? h->down : e2 == HIGH ? 1.0 - h->down : 0.0;
}

/* The rate must stay below the household's impatience, 1/beta - 1, or it
 * would save without bound; and the limit must stay inside what the lowest
 * income can service. The sliders may ask for more; these say what is used. */
static double rate_used(double r, double beta) { double cap = 1.0 / beta - 1.0 - 0.002; return r < cap ? r : cap; }
static double limit_used(double limit, double r, double benefit)
{
    double natural = r > 1e-9 ? 0.9 * benefit / r : 1e9;
    return limit < natural ? limit : natural;
}

static void grid_set(Household *h)
{
    for (int i = 0; i < NA; i++)
        h->a[i] = -h->limit + (A_MAX + h->limit) * pow((double)i / (NA - 1), GRID_CURVE);
}
/* fractional grid index of an asset level (the grid's formula, inverted) */
static double grid_pos(const Household *h, double a)
{
    double t = (a + h->limit) / (A_MAX + h->limit);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return pow(t, 1.0 / GRID_CURVE) * (NA - 1);
}
static double interp(const double *v, double pos)
{
    int i = (int)pos;
    if (i >= NA - 1) return v[NA - 1];
    return v[i] + (v[i + 1] - v[i]) * (pos - i);
}

/* Solve the consumption rule by the endogenous grid method (Carroll 2006).
 * For each level of assets carried forward the Euler equation gives today's
 * consumption directly, and the budget gives the assets that make it
 * optimal; interpolating back onto the grid needs no root-finding. `warm`
 * keeps the previous rule as the starting guess. */
static void household_solve(Household *h, bool warm)
{
    double R = 1.0 + h->r, cn[NE][NA], aend[NA];
    grid_set(h);
    if (!warm)
        for (int e = 0; e < NE; e++)
            for (int i = 0; i < NA; i++) { double c0 = h->r * h->a[i] + income(h, e); h->c[e][i] = c0 > 0.05 ? c0 : 0.05; }
    for (h->iterations = 1; h->iterations <= 5000; h->iterations++) {
        double gap = 0;
        for (int e = 0; e < NE; e++) {
            double y = income(h, e);
            for (int i = 0; i < NA; i++) {                       /* i indexes assets carried forward */
                double emu = 0;
                for (int e2 = 0; e2 < NE; e2++) emu += stay(h, e, e2) * pow(h->c[e2][i], -h->sigma);
                double c = pow(h->beta * R * emu, -1.0 / h->sigma);
                aend[i] = (c + h->a[i] - y) / R;                 /* assets today that make this optimal */
            }
            int k = 0;
            for (int j = 0; j < NA; j++) {                       /* j indexes assets held today */
                double a = h->a[j], an;
                if (a <= aend[0]) an = h->a[0];                  /* the limit binds */
                else {
                    while (k < NA - 2 && aend[k + 1] < a) k++;
                    an = h->a[k] + (h->a[k + 1] - h->a[k]) * (a - aend[k]) / (aend[k + 1] - aend[k]);
                    if (an > A_MAX) an = A_MAX;
                }
                h->next[e][j] = an;
                cn[e][j] = R * a + y - an;
            }
        }
        for (int e = 0; e < NE; e++)
            for (int i = 0; i < NA; i++) {
                double d = fabs(cn[e][i] - h->c[e][i]);
                if (d > gap) gap = d;
                h->c[e][i] = cn[e][i];
            }
        if (gap < 1e-10) break;
    }
}

/* ---- the cross-section ------------------------------------------------ */
#define MAX_PEOPLE 20000
typedef struct { double wealth[MAX_PEOPLE]; unsigned char state[MAX_PEOPLE]; int n; Rng rng; } People;

static void people_reset(People *p, int n, uint64_t seed)
{
    p->n = n; rng_seed(&p->rng, seed);
    for (int i = 0; i < n; i++) { p->wealth[i] = 0; p->state[i] = EMPLOYED; }
}
static void people_year(People *p, const Household *h)
{
    for (int i = 0; i < p->n; i++) {
        int e = p->state[i];
        double a = p->wealth[i] < h->a[0] ? h->a[0] : p->wealth[i];   /* a tighter limit forces repayment */
        p->wealth[i] = interp(h->next[e], grid_pos(h, a));
        double u = rng_uniform(&p->rng), p0 = stay(h, e, UNEMPLOYED);
        p->state[i] = (unsigned char)(u < p0 ? UNEMPLOYED : u < p0 + stay(h, e, EMPLOYED) ? EMPLOYED : HIGH);
    }
}

typedef struct { double mean, gini, at_limit, top10, p95, share[NE]; } Spread;

static int cmp_double(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

/* Summary of the cross-section; `sorted` receives the wealth levels in order
 * (the Lorenz curve is read off it). */
static Spread people_spread(const People *p, const Household *h, double *sorted)
{
    Spread s = {0};
    int n = p->n, lim = 0;
    double total = 0, weighted = 0;
    for (int i = 0; i < n; i++) {
        sorted[i] = p->wealth[i]; total += p->wealth[i];
        if (p->wealth[i] <= h->a[0] + 1e-9) lim++;
        s.share[p->state[i]] += 1.0 / n;
    }
    qsort(sorted, (size_t)n, sizeof(double), cmp_double);
    for (int i = 0; i < n; i++) weighted += (i + 1.0) * sorted[i];
    double top = 0;
    for (int i = n - n / 10; i < n; i++) top += sorted[i];
    s.mean = total / n;
    s.gini = total > 1e-9 ? 2.0 * weighted / (n * total) - (n + 1.0) / n : NAN;
    s.top10 = total > 1e-9 ? top / total : NAN;
    s.at_limit = (double)lim / n;
    s.p95 = sorted[(int)(0.95 * (n - 1))];
    return s;
}

/* ---- model checks (--test) ------------------------------------------- */
static Household hh;
static People people;
static double sorted[MAX_PEOPLE];

static void set_defaults(Household *h)
{
    h->r = 0.02; h->beta = 0.95; h->sigma = 2.0; h->limit = 0.0; h->lose = 0.06; h->find = 0.5; h->benefit = 0.4;
    h->high_pay = 4.0; h->up = 0.01; h->down = 0.10;
}
static double mean_wealth_after(Household *h, int years, uint64_t seed)
{
    household_solve(h, false);
    people_reset(&people, 5000, seed);
    for (int t = 0; t < years; t++) people_year(&people, h);
    return people_spread(&people, h, sorted).mean;
}

static int run_tests(void)
{
    int fails = 0;
#define CHECK(cond, what) do { if (!(cond)) { fails++; printf("wealth FAIL: %s\n", what); } } while (0)
    Household *h = &hh;
    set_defaults(h);
    household_solve(h, false);
    CHECK(h->iterations < 5000, "the consumption rule converges");

    /* the budget holds exactly, and the rule has the textbook shape */
    double worst = 0; bool rising = true, cautious = true;
    for (int e = 0; e < NE; e++)
        for (int i = 0; i < NA; i++) {
            double gap = fabs(h->c[e][i] + h->next[e][i] - (1 + h->r) * h->a[i] - income(h, e));
            if (gap > worst) worst = gap;
            if (i && (h->c[e][i] <= h->c[e][i - 1] || h->next[e][i] < h->next[e][i - 1])) rising = false;
            if (h->c[UNEMPLOYED][i] >= h->c[EMPLOYED][i] || h->c[EMPLOYED][i] >= h->c[HIGH][i]) cautious = false;
        }
    CHECK(worst < 1e-12, "consumption plus saving equals resources");
    CHECK(rising, "richer households consume more and save more");
    CHECK(cautious, "consumption rises with income at every wealth level");
    CHECK(h->next[0][0] == h->a[0] && h->c[0][0] > 0, "the limit binds for the poor unemployed, with positive consumption");

    /* the Euler equation holds wherever the limit does not bind */
    double euler = 0;
    for (int e = 0; e < NE; e++)
        for (int i = 0; i < NA - 20; i++) {
            if (h->next[e][i] <= h->a[0] + 1e-9 || h->next[e][i] > 0.8 * A_MAX) continue;
            double pos = grid_pos(h, h->next[e][i]), emu = 0;
            for (int e2 = 0; e2 < NE; e2++) emu += stay(h, e, e2) * pow(interp(h->c[e2], pos), -h->sigma);
            double err = fabs(pow(h->beta * (1 + h->r) * emu, -1.0 / h->sigma) / h->c[e][i] - 1.0);
            if (err > euler) euler = err;
        }
    CHECK(euler < 2e-3, "the Euler equation holds off the limit (interpolation error only)");

    /* no risk at all: consumption must grow at (beta (1+r))^(1/sigma), the closed form */
    Household d = *h;
    d.lose = 0.0; d.find = 1.0; d.benefit = 1.0; d.up = 0.0; d.high_pay = 1.0;
    household_solve(&d, false);
    double want = pow(d.beta * (1 + d.r), 1.0 / d.sigma), off = 0;
    for (int i = 40; i < NA - 20; i++) {
        if (d.next[1][i] <= d.a[0] + 0.05) continue;
        double g = interp(d.c[1], grid_pos(&d, d.next[1][i])) / d.c[1][i];
        if (fabs(g - want) > off) off = fabs(g - want);
    }
    CHECK(off < 1e-3, "without risk, consumption growth matches the closed form");

    /* the comparative statics every textbook draws */
    double base = mean_wealth_after(h, 600, 3);
    Household hi_r = *h; hi_r.r = 0.04;
    Household risky = *h; risky.lose = 0.15;
    Household loose = *h; loose.limit = 2.0;
    double m_r = mean_wealth_after(&hi_r, 600, 3), m_loose = mean_wealth_after(&loose, 600, 3);
    CHECK(m_r > base * 1.3, "a higher interest rate raises saving");
    CHECK(m_loose < base, "a looser borrowing limit lowers net wealth");
    /* precaution: a worker facing more job risk consumes less at the same wealth */
    household_solve(h, false); household_solve(&risky, false);
    bool precaution = true;
    for (int i = 0; i < NA / 2; i++) if (risky.c[EMPLOYED][i] >= h->c[EMPLOYED][i]) precaution = false;
    CHECK(precaution, "more job risk lowers a worker's consumption at every wealth level");

    /* the cross-section: unemployment at its stationary rate, some at the limit, sensible Gini */
    household_solve(h, false);
    people_reset(&people, MAX_PEOPLE, 4);
    for (int t = 0; t < 600; t++) people_year(&people, h);
    Spread s = people_spread(&people, h, sorted);
    double pi[NE] = {0, 1, 0}, nxt[NE];           /* the chain's stationary shares, by iteration */
    for (int t = 0; t < 2000; t++) {
        for (int e2 = 0; e2 < NE; e2++) { nxt[e2] = 0; for (int e = 0; e < NE; e++) nxt[e2] += pi[e] * stay(h, e, e2); }
        for (int e = 0; e < NE; e++) pi[e] = nxt[e];
    }
    CHECK(fabs(s.share[UNEMPLOYED] - pi[UNEMPLOYED]) < 0.01 && fabs(s.share[HIGH] - pi[HIGH]) < 0.01,
          "the shares unemployed and on high pay settle at the chain's stationary ones");
    CHECK(s.at_limit > 0.005 && s.at_limit < 0.5, "some households, not most, sit at the borrowing limit");
    CHECK(s.gini > 0.4 && s.gini < 0.9, "identical households end up unequal");
    CHECK(s.top10 > 0.3, "the top tenth holds well over a tenth of the wealth");
    CHECK(rate_used(0.10, 0.95) < 1 / 0.95 - 1 && limit_used(50, 0.02, 0.4) < 0.4 / 0.02, "the rate and limit are capped where the problem is well-posed");
#undef CHECK
    if (!fails) printf("wealth: 15 model checks passed (mean wealth %.2f years of wages, Gini %.2f)\n", s.mean, s.gini);
    return fails;
}

/* ---- the window ------------------------------------------------------- */
static double p_r = 2.0, p_beta = 0.95, p_sigma = 2.0, p_limit = 0.0, p_lose = 6.0, p_benefit = 0.40, p_high = 4.0;
static UiHist h_gini;
static double x_top = 12;          /* right edge of the histogram, eased towards the data */

static void resolve(bool warm)
{
    hh.beta = p_beta; hh.sigma = p_sigma; hh.lose = p_lose / 100; hh.find = 0.5; hh.benefit = p_benefit;
    hh.high_pay = p_high; hh.up = 0.01; hh.down = 0.10;
    hh.r = rate_used(p_r / 100, hh.beta);
    hh.limit = limit_used(p_limit, hh.r, hh.benefit);
    household_solve(&hh, warm);
}

static void draw(void)
{
    const int sx = UI_PANEL_X, sw = UI_PANEL_W;
    char a[32], b[64];
    ui_header("Wealth when income is risky",
              "Identical households, uninsurable job risk: they save to protect themselves.");

    int y = 84;
    bool moved = false;
    moved |= ui_slider(sx, y, sw, "Interest rate, % a year", &p_r, -2.0, 6.0, 0.1, "%.1f"); y += 46;
    moved |= ui_slider(sx, y, sw, "Patience (discount factor)", &p_beta, 0.90, 0.99, 0.005, "%.3f"); y += 46;
    moved |= ui_slider(sx, y, sw, "Risk aversion", &p_sigma, 1.0, 5.0, 0.1, "%.1f"); y += 46;
    moved |= ui_slider(sx, y, sw, "Borrowing limit, years of wages", &p_limit, 0.0, 4.0, 0.1, "%.1f"); y += 46;
    moved |= ui_slider(sx, y, sw, "Chance of losing a job, % a year", &p_lose, 1.0, 20.0, 0.5, "%.1f"); y += 46;
    moved |= ui_slider(sx, y, sw, "Unemployment benefit / wage", &p_benefit, 0.10, 0.90, 0.01, "%.2f"); y += 46;
    moved |= ui_slider(sx, y, sw, "High pay, in wages (1 = none)", &p_high, 1.0, 8.0, 0.1, "%.1f"); y += 50;
    if (moved) resolve(true);
    if (ui_button(sx, y, 220, "Start everyone at zero") || ui.reset) { people_reset(&people, MAX_PEOPLE, 11); h_gini.n = 0; }
    y += 42;

    if (!ui.paused) people_year(&people, &hh);
    Spread s = people_spread(&people, &hh, sorted);
    if (!ui.paused) ui_hist_push(&h_gini, isnan(s.gini) ? 0 : s.gini);

    /* tiles */
    int tw = sw / 2 - 6;
    if (isnan(s.gini)) snprintf(a, sizeof a, "n/a"); else snprintf(a, sizeof a, "%.2f", s.gini);
    ui_tile(sx, y, tw, "Gini of wealth", a, "0 equal, 1 one owner");
    snprintf(a, sizeof a, "%.2f", s.mean);
    ui_tile(sx + tw + 12, y, tw, "Average wealth", a, "years of wages");
    y += 88;
    snprintf(a, sizeof a, "%.1f%%", 100 * s.at_limit);
    ui_tile(sx, y, tw, "At the debt limit", a, "no buffer left");
    if (isnan(s.top10)) snprintf(a, sizeof a, "n/a"); else snprintf(a, sizeof a, "%.0f%%", 100 * s.top10);
    ui_tile(sx + tw + 12, y, tw, "Held by the top 10%", a, "share of all wealth");
    y += 92;
    if (hh.r < p_r / 100 - 1e-9) { ui_textf(sx, y, UI_SMALL, UI_S2, "Rate held at %.2f%%: below impatience, 1/beta - 1", 100 * hh.r); y += 14; }
    if (hh.limit < p_limit - 1e-9) ui_textf(sx, y, UI_SMALL, UI_S2, "Limit held at %.1f: what the benefit can service", hh.limit);

    /* histogram of wealth */
    const int hx = 64, hy = 112, hw = 700, hh_ = 250, bins = 70;
    double want_top = s.p95 * 1.15 > 4 ? s.p95 * 1.15 : 4;
    x_top += (want_top - x_top) * 0.08;
    double x0 = -hh.limit - 0.02 * (x_top + hh.limit), bw = (x_top - x0) / bins;
    static double count[70];
    double peak = 0;
    for (int k = 0; k < bins; k++) count[k] = 0;
    for (int i = 0; i < people.n; i++) {
        int k = (int)((people.wealth[i] - x0) / bw);
        if (k >= 0 && k < bins) count[k] += 100.0 / people.n;
    }
    for (int k = 0; k < bins; k++) if (count[k] > peak) peak = count[k];
    double ytop = peak > 12 ? peak * 1.1 : 12;
    UiPlot hp = ui_plot(hx, hy, hw, hh_, "Households by wealth, percent", x0, x_top, 0, ytop, "%.0f", "%.0f");
    for (int k = 0; k < bins; k++) {
        int x1 = ui_px(&hp, x0 + k * bw), x2 = ui_px(&hp, x0 + (k + 1) * bw), yt = ui_py(&hp, count[k] > ytop ? ytop : count[k]);
        if (count[k] > 0) ui_rect(x1 + 1, yt, x2 - x1 - 1 > 1 ? x2 - x1 - 1 : 1, hy + hh_ - yt, UI_S1);
    }
    ui_line(ui_px(&hp, -hh.limit), hy, ui_px(&hp, -hh.limit), hy + hh_, UI_S2);
    ui_text(ui_px(&hp, -hh.limit) + 6, hy + 4, UI_SMALL, UI_INK2, "borrowing limit");
    ui_text_right(hx + hw, hy + hh_ + 20, UI_SMALL, UI_MUTED, "wealth, years of wages (top 5% off the chart)");

    /* Lorenz curve */
    const int lx = 64, ly = 440, lw = 250, lh = 230;
    static double px[102], py[102];
    double total = s.mean * people.n, run = 0, lowest = 0;
    int np = 0, stride = people.n / 100;
    px[np] = 0; py[np++] = 0;
    for (int i = 0; i < people.n; i++) {
        run += sorted[i];
        if ((i + 1) % stride == 0 && np < 102) { px[np] = 100.0 * (i + 1) / people.n; py[np] = total > 1e-9 ? 100 * run / total : 0; if (py[np] < lowest) lowest = py[np]; np++; }
    }
    UiPlot lp = ui_plot(lx, ly, lw, lh, "Lorenz curve", 0, 100, lowest < 0 ? floor(lowest / 10) * 10 : 0, 100, "%.0f", "%.0f");
    double ex[2] = {0, 100}, ey[2] = {0, 100};
    ui_plot_line(&lp, ex, ey, 2, UI_AXIS);
    if (total > 1e-9) ui_plot_line(&lp, px, py, np, UI_S1);
    ui_text(lx, ly + lh + 20, UI_SMALL, UI_MUTED, "poorest x% hold y% of wealth");

    /* the decision rule itself */
    const int cx = 400, cy = 440, cw = 364, ch = 230;
    static double gx[NA];
    int shown = 0;
    while (shown < NA && hh.a[shown] <= x_top) shown++;
    for (int i = 0; i < NA; i++) gx[i] = hh.a[i];
    double ctop = shown > 1 ? hh.c[HIGH][shown - 1] * 1.05 : 2;
    UiPlot cp = ui_plot(cx, cy, cw, ch, "Consumption by wealth", x0, x_top, 0, ctop, "%.0f", "%.1f");
    ui_plot_line(&cp, gx, hh.c[HIGH], shown, UI_S3);
    ui_plot_line(&cp, gx, hh.c[EMPLOYED], shown, UI_S1);
    ui_plot_line(&cp, gx, hh.c[UNEMPLOYED], shown, UI_S2);
    int kx = ui_key(cx, cy + ch + 18, UI_S2, false, "No job");
    kx = ui_key(kx, cy + ch + 18, UI_S1, false, "Job");
    ui_key(kx, cy + ch + 18, UI_S3, false, "High pay");
    snprintf(b, sizeof b, "solved in %d steps", hh.iterations);
    ui_text_right(cx + cw, cy - 18, UI_SMALL, UI_MUTED, b);
}

static int app(int argc, char **argv)
{
    if (ui_args(argc, argv, "wealth", "the wealth distribution of households facing income risk")) return run_tests() ? 1 : 0;
    if (!ui_open("Wealth when income is risky")) return 1;
    resolve(false);
    people_reset(&people, MAX_PEOPLE, 11);
    ui_run(draw);
    return 0;
}
UI_MAIN(app)
