/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

/* Sampling, filtering and display shaping for the SOC estimator.
 *   16-sample burst -> burst median -> sliding median -> IIR
 *     -> load offset -> table lookup -> display shaping */
#include "battery_soc.h"

static void charge_update_rates(bat_soc_t* s);

/* ------------------------------ helpers ------------------------------ */

/* Median of n values. */
static int median_inplace(int* a, int n)
{
    int i, j, key;
    for (i = 1; i < n; i++) {
        key = a[i];
        for (j = i - 1; j >= 0 && a[j] > key; j--) a[j + 1] = a[j];
        a[j + 1] = key;
    }
    return a[n / 2];
}

/* Read one burst and write its median; returns 0 when the burst is discarded. */
static int read_burst(bat_read_mv_fn fn, int* out_mv)
{
    int b[BAT_BURST_N];
    int i, lo, hi;
    for (i = 0; i < BAT_BURST_N; i++) b[i] = fn();
    lo = hi = b[0];
    for (i = 1; i < BAT_BURST_N; i++) {
        if (b[i] < lo) lo = b[i];
        if (b[i] > hi) hi = b[i];
    }
    if (hi - lo > BAT_BURST_SPREAD_MV) return 0;
    *out_mv = median_inplace(b, BAT_BURST_N);
    return 1;
}

/* Push one sample into the sliding median window. */
static void med_push(bat_soc_t* s, int v)
{
    s->med[s->med_idx] = v;
    s->med_idx         = (s->med_idx + 1) % BAT_MED_WIN;
    if (s->med_fill < BAT_MED_WIN) s->med_fill++;
}

/* Median of the sliding median window. */
static int med_value(bat_soc_t* s)
{
    int tmp[BAT_MED_WIN];
    int i;
    for (i = 0; i < s->med_fill; i++) tmp[i] = s->med[i];
    return median_inplace(tmp, s->med_fill);
}

/* ------------------------------ public ------------------------------ */

void bat_soc_init(bat_soc_t* s, bat_read_mv_fn read_mv)
{
    int i;
    s->read_mv = read_mv;
    for (i = 0; i < BAT_MED_WIN; i++) s->med[i] = 0;
    s->med_idx = s->med_fill = 0;
    s->v_filt                = 0;
    s->v_slope_ref           = 0;
    s->dv10                  = 0;
    s->v_live                = 0;
    s->v_charge_max          = 0;
    s->i_chg_ma              = BAT_I_CHG_MA;
    s->v_cv_mv               = BAT_V_CV_MV;
    charge_update_rates(s);
    s->slope_valid = 0;
    s->rise_streak = s->fall_streak = 0;
    s->chg_by_pin                   = 0;
    s->pct_est = s->pct_disp = -1; /* -1: no estimate yet */
    s->ld_state              = BAT_LD_NORMAL;
    s->mode                  = BAT_MODE_DISCHARGE;
    s->charger               = 0;
    s->warmup                = BAT_WARMUP_TICKS;
    s->settle                = BAT_SETTLE_TICKS;
    s->t_mode_ms = s->t_recover_ms = s->t_move_ms = 0;
    s->t_full_ms = s->t_slope_ms = 0;
    s->inited                    = 0;
}

void bat_soc_set_cv_mv(bat_soc_t* s, int mv)
{
    if (mv > 3000 && mv <= 4400) {
        s->v_cv_mv = mv;
        charge_update_rates(s);
    }
}

int bat_soc_get_cv_mv(const bat_soc_t* s)
{
    return s->v_cv_mv;
}

void bat_soc_set_i_net(bat_soc_t* s, int ma)
{
    if (ma >= 100 && ma <= 3000) {
        s->i_chg_ma = ma;
        charge_update_rates(s);
    }
}

int bat_soc_get_i_net(const bat_soc_t* s)
{
    return s->i_chg_ma;
}

/* Seed the filter with a pre-boot sample and skip the warm-up discards.
 * The whole median window is filled with it, so the value the estimate starts
 * from is the median of a full window rather than a single raw reading, and the
 * sliding median keeps working normally from the first tick on. */
void bat_soc_seed(bat_soc_t* s, int mv)
{
    int i;
    if (mv <= 0) return;
    for (i = 0; i < BAT_MED_WIN; i++) s->med[i] = mv;
    s->med_idx     = 0;
    s->med_fill    = BAT_MED_WIN;
    s->v_live      = mv;
    s->v_filt      = mv;
    s->v_slope_ref = mv;
    s->inited      = 1;
    s->warmup      = 0;
    s->settle      = BAT_SETTLE_TICKS;
}

/* Drop the estimate; the next tick rebuilds it from the current voltage. */
void bat_soc_reset_estimate(bat_soc_t* s)
{
    s->pct_est      = -1;
    s->pct_disp     = -1;
    s->t_move_ms    = 0;
    s->t_recover_ms = 0;
    s->t_full_ms    = 0;
}

void bat_soc_set_load(bat_soc_t* s, bat_ld_state_t st)
{
    if (st >= 0 && st < BAT_LD_COUNT) s->ld_state = st;
}

void bat_soc_set_percent(bat_soc_t* s, uint8_t pct)
{
    if (s->pct_disp < 0) s->pct_disp = pct;
}

uint8_t bat_soc_percent(const bat_soc_t* s)
{
    if (s->pct_disp < 0) return 0;
    return (uint8_t)s->pct_disp;
}

int bat_soc_mode(const bat_soc_t* s)
{
    return s->mode;
}

/* ---------------------------- mode detection ---------------------------- */

/* Update the slope window and the rise/fall streaks. */
static void update_slope(bat_soc_t* s, uint32_t dt_ms)
{
    s->t_slope_ms += dt_ms;
    if (s->t_slope_ms >= BAT_SLOPE_WIN_MS) {
        s->dv10        = s->v_filt - s->v_slope_ref; /* mV per window */
        s->v_slope_ref = s->v_filt;
        s->t_slope_ms  = 0;
        s->slope_valid = 1;
        s->rise_streak = (s->dv10 > BAT_CHARGE_RISE_MV) ? s->rise_streak + 1 : 0;
        s->fall_streak = (s->dv10 <= -BAT_CHARGE_FALL_MV) ? s->fall_streak + 1 : 0;
    }
}

/* Restart the slope window from the current voltage. */
static void slope_reset(bat_soc_t* s)
{
    s->v_slope_ref = s->v_filt;
    s->t_slope_ms  = 0;
    s->dv10        = 0;
    s->slope_valid = 0;
    s->rise_streak = 0;
    s->fall_streak = 0;
}

/* True when the voltage is near the session peak and its slope is small. */
static int charge_is_full_candidate(const bat_soc_t* s)
{
    return s->v_filt >= BAT_FULL_MIN_MV && s->slope_valid && s->v_filt >= s->v_charge_max - BAT_FULL_MARGIN_MV &&
           s->dv10 <= BAT_FULL_SLOPE_MV;
}

/* Derive the CC and CV ramp intervals from the charge current. */
static void charge_update_rates(bat_soc_t* s)
{
    long tau;
    int remain, inet, ocv_cv, soc_cv;
    inet = s->i_chg_ma;
    if (inet < 100) inet = 100;
    if (inet <= BAT_I_TERM_MA + 20) inet = BAT_I_TERM_MA + 20;
    s->cc_ms_per_pt = (uint32_t)(36000L * BAT_Q_MAH / inet);

    ocv_cv = s->v_cv_mv - inet * BAT_R_DC_MOHM / 1000;
    soc_cv = bat_soc_from_mv(ocv_cv);
    /* The CV segment starts when the terminal voltage reaches the CV setpoint,
     * which corresponds to this state of charge. Deriving the boundary from the
     * setpoint and the IR drop makes it predictable, unlike waiting for the
     * voltage slope to flatten (which lags and can miss the segment entirely). */
    s->soc_cv = soc_cv;
    remain    = (100 - soc_cv) * BAT_Q_MAH / 100;
    if (remain < 20) remain = 20;
    tau = (long)remain * 3600000L / (inet - BAT_I_TERM_MA);
    if (tau < 300000L) tau = 300000L;
    if (tau > 3600000L) tau = 3600000L;
    s->cv_tau_ms = (uint32_t)tau;
}

/* True once the ramp has reached the CV entry point. */
static int charge_is_cv(const bat_soc_t* s)
{
    return s->soc_cv > 0 && s->pct_disp >= s->soc_cv;
}

/* Switch between discharge and charge mode. */
static void update_mode(bat_soc_t* s, uint32_t dt_ms, int charger_active)
{
    s->t_mode_ms += dt_ms;
    s->charger = charger_active;

    if (s->mode != BAT_MODE_CHARGE) {
        if (charger_active) {
            s->mode       = BAT_MODE_CHARGE;
            s->chg_by_pin = 1;
        } else if (s->slope_valid && s->v_filt > BAT_CHARGE_HINT_MV && s->rise_streak >= BAT_CHARGE_STREAK) {
            s->mode       = BAT_MODE_CHARGE;
            s->chg_by_pin = 0;
        } else {
            return;
        }
        s->t_mode_ms    = 0;
        s->t_full_ms    = 0;
        s->t_recover_ms = 0;
        s->t_move_ms    = 0;
        s->v_charge_max = s->v_filt;
        slope_reset(s);
        return;
    }

    if (charge_is_full_candidate(s)) {
        s->t_full_ms += dt_ms;
        if (s->t_full_ms >= BAT_FULL_HOLD_MS) {
            s->pct_est   = 100;
            s->pct_disp  = 100;
            s->t_move_ms = 0;
        }
    } else {
        s->t_full_ms = 0;
    }

    if (!charger_active && (s->chg_by_pin || (s->slope_valid && s->fall_streak >= BAT_CHARGE_STREAK))) {
        s->mode       = BAT_MODE_DISCHARGE;
        s->t_mode_ms  = 0;
        s->t_move_ms  = 0;
        s->t_full_ms  = 0;
        s->chg_by_pin = 0;
        slope_reset(s);
    }
}

/* ---------------------------- display shaping ---------------------------- */

/* Apply hysteresis, rate limit and monotonicity to the displayed percentage. */
static void shape_display(bat_soc_t* s, uint32_t dt_ms)
{
    int diff;

    if (s->pct_disp < 0) {
        s->pct_disp = s->pct_est;
        return;
    }
    if (s->mode == BAT_MODE_CHARGE) return;

    /* At the start of a session the filters are still converging, so the estimate
     * still moves a little. Follow it directly for a few ticks instead of locking
     * the first value, which would freeze whatever noise it carried. */
    if (s->settle > 0) {
        s->settle--;
        s->pct_disp     = s->pct_est;
        s->t_move_ms    = 0;
        s->t_recover_ms = 0;
        return;
    }

    diff = s->pct_est - s->pct_disp;

    if (diff <= -BAT_HYST_PT) {
        s->t_recover_ms = 0;
        s->t_move_ms += dt_ms;
        if (s->t_move_ms >= BAT_DOWN_MS_PER_PT) {
            s->pct_disp -= 1;
            s->t_move_ms = 0;
        }
    } else {
        s->t_move_ms = 0;
#if !BAT_DISCHARGE_MONOTONIC
        if (diff >= BAT_RECOVER_MARGIN) {
            s->t_recover_ms += dt_ms;
            if (s->t_recover_ms >= BAT_RECOVER_HOLD_MS) {
                s->t_move_ms += dt_ms;
                if (s->t_move_ms >= BAT_UP_MS_PER_PT) {
                    s->pct_disp += 1;
                    s->t_move_ms = 0;
                }
            }
        } else {
            s->t_recover_ms = 0;
        }
#else
        s->t_recover_ms = 0;
#endif
    }
    if (s->pct_disp < 0) s->pct_disp = 0;
    if (s->pct_disp > 100) s->pct_disp = 100;
}

/* ------------------------------ main loop ------------------------------ */

void bat_soc_tick(bat_soc_t* s, uint32_t dt_ms, int charger_active)
{
    int v_raw, v_med, v_ref, pct_new;

    /* 1) burst median */
    if (!read_burst(s->read_mv, &v_raw)) return;
    s->v_live = v_raw;

    /* Drop the boot transient before it can seed the estimate. */
    if (s->warmup > 0) {
        s->warmup--;
        return;
    }

    /* 2) sliding median */
    med_push(s, v_raw);
    v_med = med_value(s);

    /* 3) IIR */
    if (!s->inited) {
        s->v_filt      = v_med;
        s->v_slope_ref = v_med;
        s->inited      = 1;
    } else {
        s->v_filt += (v_med - s->v_filt) >> BAT_IIR_SHIFT;
    }

    update_slope(s, dt_ms);
    update_mode(s, dt_ms, charger_active);

    /* 4) load offset, then table lookup */
    v_ref   = s->v_filt + bat_ld_offset_mv[s->ld_state];
    pct_new = bat_soc_from_mv(v_ref);

    /* 5) charging: ramp by time, ignore the lookup result */
    if (s->mode == BAT_MODE_CHARGE) {
        if (s->v_filt > s->v_charge_max) s->v_charge_max = s->v_filt;
        /* No estimate yet (booted with the charger connected): the terminal
         * voltage is lifted and there is no anchor for the ramp, so report
         * "no value" instead of inventing one. The value is rebuilt from the
         * settled voltage once the charger is removed. */
        if (s->pct_disp < 0) {
            s->pct_est = -1;
            return;
        }
        s->t_move_ms += dt_ms;
        if (charge_is_cv(s)) {
            int gap           = 100 - s->pct_disp;
            uint32_t interval = (gap > 1) ? (s->cv_tau_ms / (uint32_t)gap) : s->cv_tau_ms;
            /* CV is never faster than CC: the charge current has already dropped. */
            if (interval < s->cc_ms_per_pt) interval = s->cc_ms_per_pt;
            /* ... and never so slow that the last point looks stuck. */
            if (interval > BAT_CV_MAX_STEP_MS) interval = BAT_CV_MAX_STEP_MS;
            /* At most one point per tick, drop any backlog. */
            if (s->t_move_ms > interval) s->t_move_ms = interval;
            if (s->t_move_ms >= interval) {
                s->t_move_ms -= interval;
                if (s->pct_disp < BAT_CHARGE_CAP_PT) s->pct_disp += 1;
            }
        } else {
            if (s->t_move_ms > s->cc_ms_per_pt) s->t_move_ms = s->cc_ms_per_pt;
            if (s->t_move_ms >= s->cc_ms_per_pt) {
                s->t_move_ms -= s->cc_ms_per_pt;
                if (s->pct_disp < BAT_CHARGE_CAP_PT) s->pct_disp += 1;
            }
        }
        s->pct_est = s->pct_disp;
        return;
    }

    /* 6) discharging */
    s->pct_est = pct_new;

    /* 7) display shaping */
    shape_display(s, dt_ms);
}
