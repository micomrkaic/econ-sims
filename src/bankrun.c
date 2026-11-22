/* bankrun.c — a bank run, after Diamond and Dybvig (1983).
 *
 * A bank takes a deposit of 1 from each of 1,000 people, keeps a share in
 * cash and lends the rest. Loans repay with interest at the end of the
 * episode; called in early they fetch only their fire-sale value. A share of
 * depositors will genuinely need their money on some random day. The rest
 * are patient: left alone they wait and share the loans' return.
 *
 * But the bank pays on demand, first come first served. A patient depositor
 * watches the neighbours (and the news), projects total withdrawals, and if
 * the projection exceeds what the bank could pay before its loans mature,
 * withdraws at once — which is rational, and which raises everyone else's
 * projection. A sound bank can therefore fail of fear alone. The classic
 * remedies are here as switches: deposit insurance, suspending withdrawals,
 * and a lender of last resort.
 *
 * Part of econ-sims. Copyright (C) 2026 Mico Mrkaic.
 * GPL-3.0-or-later; see LICENSE. */
#include "ui.h"
#include "rng.h"

/* ---- the model -------------------------------------------------------- */
#define GW 40
#define GH 25
#define PEOPLE (GW * GH)
#define DAYS 120                  /* the loans mature on day DAYS */
#define LOAN_RETURN 1.25          /* what a loan held to maturity repays */
#define SIGHT 2                   /* a depositor sees neighbours within this distance */

enum { WAITING = 0, NEEDED, RAN, LOST, REPAID };   /* a depositor's situation */

typedef struct {
    /* settings */
    double reserve;               /* share of deposits kept as cash */
    double needs;                 /* share of depositors who will need their money */
    double fire_sale;             /* what a loan fetches if sold early, per unit */
    double nerves;                /* how far depositors inflate what they project */
    double news;                  /* weight on the economy-wide count vs the neighbours */
    double insured;               /* share of depositors covered by insurance */
    bool suspend;                 /* the bank stops paying when its cash runs out */
    bool lender;                  /* a central bank lends against loans at par */
    /* depositors */
    unsigned char state[PEOPLE];
    unsigned char covered[PEOPLE];
    short need_day[PEOPLE];       /* day a genuine need arrives, 0 if never */
    float temper[PEOPLE];         /* individual multiplier on nerves */
    /* the bank */
    double cash, loans;           /* loans at face value */
    double borrowed;              /* owed to the lender of last resort */
    int day, withdrawn, ran, lost;
    bool failed, suspended, over;
    double payout;                /* what each remaining depositor got at maturity */
    double insurance_cost;        /* paid by the insurer to covered depositors of a failed bank */
    Rng rng;
} Bank;

static void bank_open(Bank *b, uint64_t seed)
{
    rng_seed(&b->rng, seed);
    for (int i = 0; i < PEOPLE; i++) {
        b->state[i] = WAITING;
        b->covered[i] = rng_uniform(&b->rng) < b->insured;
        b->need_day[i] = rng_uniform(&b->rng) < b->needs ? (short)(1 + rng_below(&b->rng, DAYS)) : 0;
        b->temper[i] = (float)exp(0.25 * rng_normal(&b->rng));
    }
    b->cash = b->reserve * PEOPLE; b->loans = (1.0 - b->reserve) * PEOPLE; b->borrowed = 0;
    b->day = 0; b->withdrawn = b->ran = b->lost = 0;
    b->failed = b->suspended = b->over = false; b->payout = 0; b->insurance_cost = 0;
}

/* The largest share of deposits the bank could pay out before maturity: its
 * cash plus its loans at fire-sale value (at par, with a lender of last
 * resort). A run is rational exactly when withdrawals are expected to pass it. */
static double bank_capacity(const Bank *b)
{
    return b->reserve + (1.0 - b->reserve) * (b->lender ? 1.0 : b->fire_sale);
}

/* Pay one depositor 1 on demand. Returns false if the bank cannot. */
static bool bank_pay(Bank *b)
{
    if (b->cash < 1.0) {
        if (b->suspend) { b->suspended = true; return false; }
        double need = 1.0 - b->cash;
        if (b->lender) { b->borrowed += need; b->cash += need; }           /* borrow against loans, at par */
        else {
            double sell = need / b->fire_sale;                             /* sell loans at a loss */
            if (sell > b->loans + 1e-9) return false;
            b->loans -= sell; b->cash += need;
        }
    }
    b->cash -= 1.0;
    return true;
}

/* What depositor i projects total withdrawals to be, as a share of deposits:
 * what it has seen so far (neighbours and news) plus the genuine needs still
 * to come. */
static double projected(const Bank *b, int i)
{
    int x = i % GW, y = i / GW, seen = 0, gone = 0;
    for (int dy = -SIGHT; dy <= SIGHT; dy++)
        for (int dx = -SIGHT; dx <= SIGHT; dx++) {
            int xx = x + dx, yy = y + dy;
            if ((!dx && !dy) || xx < 0 || xx >= GW || yy < 0 || yy >= GH) continue;
            seen++;
            if (b->state[yy * GW + xx] != WAITING) gone++;
        }
    double local = seen ? (double)gone / seen : 0, global = (double)b->withdrawn / PEOPLE;
    return (1.0 - b->news) * local + b->news * global + b->needs * (1.0 - (double)b->day / DAYS);
}

static void bank_fail(Bank *b)
{
    b->failed = b->over = true;
    for (int i = 0; i < PEOPLE; i++)
        if (b->state[i] == WAITING) {
            if (b->covered[i]) { b->state[i] = REPAID; b->insurance_cost += 1.0; }
            else { b->state[i] = LOST; b->lost++; }
        }
}

/* One day: genuine needs arrive, the patient decide whether to run, and the
 * bank pays those at its window in random order. On the last day the loans
 * mature and whoever waited shares what is left. */
static void bank_day(Bank *b)
{
    static int queue[PEOPLE];
    int n = 0;
    if (b->over) return;
    b->day++;
    double cap = bank_capacity(b);
    for (int i = 0; i < PEOPLE; i++) {
        if (b->state[i] != WAITING) continue;
        if (b->need_day[i] == b->day) { queue[n++] = i; continue; }
        /* insured deposits are safe, and so are all deposits where the bank
         * would close its window before selling a loan, or can borrow at par */
        if (b->covered[i] || b->suspend || b->lender) continue;
        if (b->nerves * b->temper[i] * projected(b, i) > cap) queue[n++] = -(i + 1);   /* negative: a run */
    }
    for (int k = n - 1; k > 0; k--) { int j = rng_below(&b->rng, k + 1), t = queue[k]; queue[k] = queue[j]; queue[j] = t; }
    for (int k = 0; k < n && !b->suspended; k++) {
        bool run = queue[k] < 0;
        int i = run ? -queue[k] - 1 : queue[k];
        if (!bank_pay(b)) { if (!b->suspended) { bank_fail(b); return; } break; }
        b->state[i] = run ? RAN : NEEDED; b->withdrawn++; if (run) b->ran++;
    }
    if (b->day >= DAYS) {
        int left = 0;
        for (int i = 0; i < PEOPLE; i++) if (b->state[i] == WAITING) left++;
        double assets = b->cash + b->loans * LOAN_RETURN - b->borrowed;
        b->payout = left ? assets / left : 0;
        for (int i = 0; i < PEOPLE; i++) if (b->state[i] == WAITING) b->state[i] = REPAID;
        b->over = true;
    }
}

/* A scare: a block of depositors withdraws at once, for no reason. */
static void bank_rumour(Bank *b, int size)
{
    int x0 = rng_below(&b->rng, GW - size + 1), y0 = rng_below(&b->rng, GH - size + 1);
    for (int y = y0; y < y0 + size && !b->over && !b->suspended; y++)
        for (int x = x0; x < x0 + size; x++) {
            int i = y * GW + x;
            if (b->state[i] != WAITING) continue;
            if (!bank_pay(b)) { if (!b->suspended) bank_fail(b); return; }
            b->state[i] = RAN; b->withdrawn++; b->ran++;
        }
}

/* ---- model checks (--test) ------------------------------------------- */
static Bank bank;

static void set_sound(Bank *b)
{
    b->reserve = 0.25; b->needs = 0.15; b->fire_sale = 0.6; b->nerves = 1.0; b->news = 0.3; b->insured = 0;
    b->suspend = b->lender = false;
}
static void set_thin(Bank *b)      /* little cash, loans that sell badly, jumpy depositors */
{
    b->reserve = 0.08; b->needs = 0.15; b->fire_sale = 0.45; b->nerves = 1.5; b->news = 0.3; b->insured = 0;
    b->suspend = b->lender = false;
}
/* run `episodes` episodes, a rumour of the given size on day 10; return how many failed */
static int failures(Bank *b, int episodes, int rumour, int *runs_out)
{
    int fails = 0, runs = 0;
    for (int e = 0; e < episodes; e++) {
        bank_open(b, 100 + (uint64_t)e);
        while (!b->over) { if (b->day == 10 && rumour) bank_rumour(b, rumour); bank_day(b); }
        fails += b->failed; runs += b->ran;
    }
    if (runs_out) *runs_out = runs;
    return fails;
}

static int run_tests(void)
{
    int fails = 0, runs;
#define CHECK(cond, what) do { if (!(cond)) { fails++; printf("bankrun FAIL: %s\n", what); } } while (0)
    Bank *b = &bank;

    /* a sound bank, left alone: only the genuine needs are withdrawn, and waiting pays */
    set_sound(b); bank_open(b, 1);
    int needy = 0;
    for (int i = 0; i < PEOPLE; i++) needy += b->need_day[i] != 0;
    while (!b->over) bank_day(b);
    CHECK(!b->failed && b->ran == 0 && b->withdrawn == needy, "a sound bank sees only genuine withdrawals");
    CHECK(b->payout > 1.0, "depositors who wait get more than they put in");
    double paid = b->withdrawn + b->payout * (PEOPLE - b->withdrawn);
    CHECK(fabs(paid - (b->reserve * PEOPLE + (1 - b->reserve) * PEOPLE * LOAN_RETURN)) < 1e-6,
          "with no loans sold, everything the bank earned is paid out");
    CHECK(fabs(bank_capacity(b) - (0.25 + 0.75 * 0.6)) < 1e-12, "capacity is cash plus loans at fire-sale value");

    /* the same bank shrugs off a scare; a thin one does not */
    set_sound(b);
    CHECK(failures(b, 20, 5, &runs) == 0, "a sound bank survives a rumour");
    set_thin(b);
    int thin_rumour = failures(b, 20, 5, &runs);
    CHECK(thin_rumour >= 18, "a thin bank fails once a rumour starts");
    b->nerves = 0.0;                              /* nobody ever runs: only fundamentals matter */
    CHECK(failures(b, 20, 5, &runs) == 0 && bank_capacity(b) > b->needs, "the same thin bank is solvent: fear, not its books, breaks it");

    /* a failure pays the early and wipes out the late */
    set_thin(b); bank_open(b, 100);
    while (!b->over) { if (b->day == 10) bank_rumour(b, 5); bank_day(b); }
    int counted = 0;
    for (int i = 0; i < PEOPLE; i++) counted += b->state[i] == NEEDED || b->state[i] == RAN || b->state[i] == LOST;
    CHECK(b->failed && counted == PEOPLE && b->lost > 0, "at a failure everyone has either been paid or lost");
    CHECK(b->withdrawn <= (int)(bank_capacity(b) * PEOPLE + 1), "the bank never pays out more than its capacity");
    CHECK(b->cash >= -1e-9 && b->loans >= -1e-9, "the bank's cash and loans never go negative");

    /* the three remedies */
    set_thin(b); b->insured = 1.0;
    CHECK(failures(b, 20, 5, &runs) == 0, "full deposit insurance stops the run");
    set_thin(b); b->suspend = true;
    CHECK(failures(b, 20, 5, &runs) == 0, "suspending withdrawals saves the bank");
    set_thin(b); b->lender = true;
    CHECK(failures(b, 20, 5, &runs) == 0, "a lender of last resort saves the bank");
    set_thin(b); b->insured = 0.5;
    int half = failures(b, 20, 5, NULL);
    CHECK(half < thin_rumour, "partial insurance helps");

    /* fear is the cause: calmer depositors at the same thin bank do not run */
    set_thin(b); b->nerves = 0.6;
    CHECK(failures(b, 20, 5, NULL) == 0, "calm depositors leave a thin bank standing");
#undef CHECK
    if (!fails) printf("bankrun: 15 model checks passed (thin bank: %d of 20 fail after a rumour, %d with half insured)\n", thin_rumour, half);
    return fails;
}

/* ---- the window ------------------------------------------------------- */
static double p_reserve = 0.15, p_needs = 0.15, p_fire = 0.55, p_nerves = 1.5, p_news = 0.30, p_insured = 0.0;
static bool p_suspend, p_lender, p_auto = true;   /* p_auto: a rumour on day 15 of every episode */
static uint64_t seed = 1;
static int episodes, failed_count, hold, tick;
static UiHist h_withdrawn;

static void settings(void)
{
    bank.reserve = p_reserve; bank.needs = p_needs; bank.fire_sale = p_fire; bank.nerves = p_nerves;
    bank.news = p_news; bank.insured = p_insured / 100; bank.suspend = p_suspend; bank.lender = p_lender;
}
static void new_episode(void)
{
    settings();
    bank_open(&bank, seed++);
    h_withdrawn.n = 0; hold = 0; tick = 0;
}

static void bar(int x, int y, int w, const char *label, double share, UiColor c, const char *value)
{
    ui_text(x, y, UI_SMALL, UI_INK2, label);
    ui_text_right(x + w, y, UI_SMALL, UI_INK, value);
    ui_rect(x, y + 12, w, 12, UI_TRACK);
    int fill = (int)((share > 1 ? 1 : share < 0 ? 0 : share) * w);
    ui_rect(x, y + 12, fill, 12, c);
}

static void draw(void)
{
    const int gx = 16, gy = 84, px = 14;
    const int sx = UI_PANEL_X, sw = UI_PANEL_W;
    char a[40], c2[64];
    Bank *b = &bank;

    ui_header("A bank run", "Depositors withdraw if they expect others to. Fear alone can break a sound bank.");

    int y = 84;
    bool restart = false;
    restart |= ui_slider(sx, y, sw, "Cash reserves / deposits", &p_reserve, 0.02, 0.60, 0.01, "%.2f"); y += 46;
    restart |= ui_slider(sx, y, sw, "Depositors with genuine needs", &p_needs, 0.0, 0.40, 0.01, "%.2f"); y += 46;
    restart |= ui_slider(sx, y, sw, "Fire-sale value of loans", &p_fire, 0.30, 1.00, 0.01, "%.2f"); y += 46;
    ui_slider(sx, y, sw, "Nerves", &p_nerves, 0.5, 3.0, 0.05, "%.2f"); y += 46;
    ui_slider(sx, y, sw, "Weight on the news", &p_news, 0.0, 1.0, 0.05, "%.2f"); y += 46;
    restart |= ui_slider(sx, y, sw, "Deposits insured, %", &p_insured, 0, 100, 5, "%.0f"); y += 50;
    ui_toggle(sx, y, "Suspend when cash runs out", &p_suspend); y += 28;
    ui_toggle(sx, y, "Lender of last resort", &p_lender); y += 28;
    ui_toggle(sx, y, "A rumour on day 15, every time", &p_auto); y += 34;
    bool rumour = ui_button(sx, y, 180, "Start a rumour");
    restart |= ui_button(sx + 192, y, 180, "New episode") || ui.reset;
    y += 42;
    if (restart) { new_episode(); }
    b->nerves = p_nerves; b->news = p_news; b->suspend = p_suspend; b->lender = p_lender;
    if (rumour && !b->over) bank_rumour(b, 5);

    /* time: one day every third frame; when an episode ends, show it, then start another */
    if (!ui.paused) {
        if (b->over) {
            if (hold == 0) { episodes++; failed_count += b->failed; }
            if (++hold > 150) new_episode();
        } else if (++tick % 3 == 0) {
            if (p_auto && b->day == 14) bank_rumour(b, 5);
            bank_day(b);
            ui_hist_push(&h_withdrawn, 100.0 * b->withdrawn / PEOPLE);
        }
    }

    /* the depositors */
    ui_rect(gx - 1, gy - 1, GW * px + 2, GH * px + 2, UI_SURFACE);
    for (int i = 0; i < PEOPLE; i++) {
        int cx = gx + (i % GW) * px, cy = gy + (i / GW) * px;
        UiColor col = b->state[i] == WAITING ? UI_S1 : b->state[i] == NEEDED ? UI_AXIS : b->state[i] == RAN ? UI_S2
                    : b->state[i] == LOST ? UI_BAD : UI_S3;
        ui_rect(cx, cy, px - 2, px - 2, col);
        if (b->covered[i] && b->state[i] == WAITING) ui_rect(cx + 4, cy + 4, 4, 4, UI_INK);
    }
    int kx = gx, ky = gy + GH * px + 8;
    kx = ui_key(kx, ky, UI_S1, true, "Waiting");
    kx = ui_key(kx, ky, UI_AXIS, true, "Needed it");
    kx = ui_key(kx, ky, UI_S2, true, "Ran");
    kx = ui_key(kx, ky, UI_BAD, true, "Lost it");
    ui_key(kx, ky, UI_S3, true, "Paid at the end");
    if (p_insured > 0) { ui_rect(gx + 3, ky + 28, 4, 4, UI_INK); ui_text(gx + 16, ky + 22, UI_BODY, UI_INK2, "Insured"); }

    /* outcome banner over the grid */
    if (b->over || b->suspended) {
        const char *msg = b->failed ? "THE BANK HAS FAILED" : b->suspended && !b->over ? "WITHDRAWALS SUSPENDED" : "THE LOANS MATURED: EVERYONE PAID";
        int w = ui_text_w(msg, UI_BIG) + 32, bx = gx + (GW * px - w) / 2, by = gy + GH * px / 2 - 28;
        ui_rect(bx, by, w, 56, UI_BG);
        ui_frame_rect(bx, by, w, 56, b->failed ? UI_BAD : UI_S3);
        ui_text(bx + 16, by + 12, UI_BIG, UI_INK, msg);
    }

    /* withdrawals against what the bank can pay */
    double cap = 100 * bank_capacity(b);
    const int qx = 56, qy = gy + GH * px + 84, qw = GW * px - 40, qh = UI_H - qy - 66;
    UiPlot p = ui_plot(qx, qy, qw, qh, "Withdrawn so far, percent of deposits", 0, DAYS, 0, 100, "%.0f", "%.0f");
    double lx[2] = {0, DAYS}, capy[2] = {cap, cap}, norm[2] = {0, 100 * b->needs};
    ui_plot_line(&p, lx, capy, 2, UI_INK2);
    ui_plot_line(&p, lx, norm, 2, UI_AXIS);
    static double days[UI_HIST_CAP];
    for (int i = 0; i < h_withdrawn.n; i++) days[i] = i + 1;
    ui_plot_line(&p, days, h_withdrawn.v, h_withdrawn.n, UI_S2);
    int lk = ui_key(qx, qy + qh + 20, UI_S2, false, "Withdrawn");
    lk = ui_key(lk, qy + qh + 20, UI_INK2, false, "Most the bank can pay");
    ui_key(lk, qy + qh + 20, UI_AXIS, false, "Genuine needs");

    /* tiles and the balance sheet */
    int tw = sw / 2 - 6;
    snprintf(a, sizeof a, "%d", b->day);
    snprintf(c2, sizeof c2, "of %d; %d ran", DAYS, b->ran);
    ui_tile(sx, y, tw, "Day", a, c2);
    snprintf(a, sizeof a, "%d/%d", failed_count, episodes);
    ui_tile(sx + tw + 12, y, tw, "Banks failed", a, "episodes so far");
    y += 88;
    double out = PEOPLE - b->withdrawn - b->lost, can = b->cash + b->loans * (b->lender ? 1.0 : b->fire_sale);
    snprintf(a, sizeof a, "%.0f", can);
    bar(sx, y, sw, "Bank can pay today (cash + loans sold now)", can / PEOPLE, can >= out ? UI_S3 : UI_S2, a); y += 34;
    snprintf(a, sizeof a, "%.0f", out);
    bar(sx, y, sw, "Deposits still in the bank", out / PEOPLE, UI_S1, a); y += 34;
    if (b->over && !b->failed) ui_textf(sx, y, UI_SMALL, UI_S3, "Those who waited were paid %.2f each", b->payout);
    else if (b->failed) ui_textf(sx, y, UI_SMALL, UI_BAD, "%d depositors lost everything", b->lost);
    else ui_text(sx, y, UI_SMALL, UI_MUTED, can >= out ? "Run-proof: it could pay everyone now" : "Exposed: it cannot pay everyone now");
}

static int app(int argc, char **argv)
{
    if (ui_args(argc, argv, "bankrun", "a bank run, after Diamond and Dybvig")) return run_tests() ? 1 : 0;
    if (!ui_open("A bank run")) return 1;
    new_episode();
    ui_run(draw);
    return 0;
}
UI_MAIN(app)
