/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

/* Battery SOC estimator from battery voltage.
 *
 *   static bat_soc_t s_soc;
 *   bat_soc_init(&s_soc, read_battery_mv);          // once at boot
 *   bat_soc_tick(&s_soc, elapsed_ms, usb_present);  // every 5 s
 *   bat_soc_set_load(&s_soc, BAT_LD_NORMAL);        // when the load changes
 *
 *   uint8_t pct  = bat_soc_percent(&s_soc);
 *   int     mv   = bat_soc_voltage_mv(&s_soc);
 *   int     mode = bat_soc_mode(&s_soc);
 */
#pragma once

#include <stdint.h>

#include "soc_lookup_table.h"

/* Load state reported by the application. */
typedef enum {
    BAT_LD_SLEEP = 0, /* deep sleep / screen off */
    BAT_LD_IDLE,      /* idle, static display */
    BAT_LD_NORMAL,    /* normal, calibration baseline */
    BAT_LD_HIGH,      /* WiFi on / heavy load */
    BAT_LD_COUNT
} bat_ld_state_t;

typedef enum { BAT_MODE_DISCHARGE = 0, BAT_MODE_CHARGE } bat_mode_t;

typedef int (*bat_read_mv_fn)(void); /* one battery voltage sample in mV */

/* Filtering. */
#ifndef BAT_SAMPLE_MS
#define BAT_SAMPLE_MS 5000u /* tick period */
#endif
#define BAT_BURST_N         16 /* samples per burst */
#define BAT_BURST_SPREAD_MV 25 /* discard a burst if max-min exceeds this */
#define BAT_MED_WIN         25 /* sliding median window, in samples */
#define BAT_IIR_SHIFT       4  /* v += (med - v) >> n */

/* Bursts discarded at start-up. Only needed when the first sample was taken
 * under boot load; seeding the filter with a pre-boot sample makes 1 enough. */
#define BAT_WARMUP_TICKS 1

/* Ticks at the start of a session where the displayed value follows the estimate
 * without hysteresis or the rate limit. A single reading carries a few mV of
 * noise, which is several percent on the flat part of the curve, so the display
 * is allowed to settle before the monotonic latch takes over. 6 x 5s = 30s. */
#define BAT_SETTLE_TICKS 6

/* Display shaping. */
#define BAT_HYST_PT        2       /* dead band in percent points */
#define BAT_DOWN_MS_PER_PT 15000u  /* ms per 1 point when dropping */
#define BAT_UP_MS_PER_PT   600000u /* ms per 1 point when rising */

/* 1 = the percentage never rises while discharging. */
#define BAT_DISCHARGE_MONOTONIC 1
#define BAT_RECOVER_MARGIN      5       /* used when BAT_DISCHARGE_MONOTONIC == 0 */
#define BAT_RECOVER_HOLD_MS     600000u /* used when BAT_DISCHARGE_MONOTONIC == 0 */

/* Mode detection. */
#define BAT_SLOPE_WIN_MS   600000u /* slope window */
#define BAT_CHARGE_HINT_MV 4120    /* charging needs at least this voltage */
#define BAT_CHARGE_RISE_MV 15      /* rise per window that counts as rising */
#define BAT_CHARGE_FALL_MV 5       /* fall per window that counts as falling */
#define BAT_CHARGE_STREAK  2       /* consecutive windows required */

/* Charge ramp. While charging the percentage is ramped by time instead of the
 * lookup table: CC segment is linear, CV segment is exponential. */
#define BAT_I_CHG_MA  500  /* battery charge current */
#define BAT_Q_MAH     1056 /* usable capacity */
#define BAT_R_DC_MOHM 300  /* DC internal resistance */
#define BAT_I_TERM_MA 50   /* termination current */
#define BAT_V_CV_MV   4200 /* charger CV setpoint */
/* The ramp may reach 100% itself: stopping at 99% and waiting for the full
 * anchor made the display sit on 99% for tens of minutes, which reads as a
 * stuck value. The anchor still fires earlier when the platform is reached
 * sooner. */
#define BAT_CHARGE_CAP_PT 100

/* CV detection. */
#define BAT_CV_MIN_MV   4000 /* below this never report CV */
#define BAT_CV_SLOPE_MV 15   /* rise per window at or below this means CV */

/* Longest time a single percentage point may take in the CV segment. The
 * exponential model would spend a full tau on the last point, which reads as a
 * stuck 99%. Capping it keeps the whole CV segment near 30 minutes and reaches
 * 100% a few minutes before the charger terminates. */
#define BAT_CV_MAX_STEP_MS 180000u

/* Full anchor. */
#define BAT_FULL_MIN_MV    4000
#define BAT_FULL_MARGIN_MV 40       /* allowed gap below the session peak */
#define BAT_FULL_SLOPE_MV  8        /* rise per window allowed at full */
#define BAT_FULL_HOLD_MS   2400000u /* how long the anchor must hold */

/* Offset in mV per load state, relative to BAT_LD_NORMAL. */
static const int bat_ld_offset_mv[BAT_LD_COUNT] = {-18, -12, 0, +70};

/* State struct, static allocation. */
typedef struct {
    bat_read_mv_fn read_mv;
    int med[BAT_MED_WIN];
    int med_idx, med_fill;
    int v_filt, v_slope_ref, dv10;
    int v_live; /* last burst median, unfiltered */
    int v_charge_max;
    int i_chg_ma, v_cv_mv;
    int soc_cv; /* percentage at which the CV segment starts, derived from v_cv_mv */
    uint32_t cc_ms_per_pt, cv_tau_ms;
    int slope_valid, rise_streak, fall_streak, chg_by_pin;
    int pct_est, pct_disp;
    int ld_state, mode, charger, inited;
    int warmup; /* remaining bursts to discard after boot */
    int settle; /* remaining ticks where the display may still follow the estimate */
    uint32_t t_mode_ms, t_recover_ms, t_move_ms, t_full_ms;
    uint32_t t_slope_ms;
} bat_soc_t;

#ifdef __cplusplus
extern "C" {
#endif

void bat_soc_init(bat_soc_t* s, bat_read_mv_fn read_mv);
/* Seed the filter with a sample taken before the peripherals were powered up.
 * Skips the boot-load sag, so the first estimate is available immediately. */
void bat_soc_seed(bat_soc_t* s, int mv);
/* Drop the current estimate so the next tick produces a fresh one. Used when the
 * USB state changes and the old value no longer applies. */
void bat_soc_reset_estimate(bat_soc_t* s);
void bat_soc_set_load(bat_soc_t* s, bat_ld_state_t st);
void bat_soc_set_percent(bat_soc_t* s, uint8_t pct);
void bat_soc_tick(bat_soc_t* s, uint32_t dt_ms, int charger_active);
uint8_t bat_soc_percent(const bat_soc_t* s);
int bat_soc_mode(const bat_soc_t* s);

/* Optional battery model overrides. */
void bat_soc_set_cv_mv(bat_soc_t* s, int mv);
int bat_soc_get_cv_mv(const bat_soc_t* s);
void bat_soc_set_i_net(bat_soc_t* s, int ma);
int bat_soc_get_i_net(const bat_soc_t* s);

#ifdef __cplusplus
}
#endif

/* False until the first estimate exists. */
static inline int bat_soc_ready(const bat_soc_t* s)
{
    return s->pct_disp >= 0;
}

/* Filtered battery voltage in mV. */
static inline int bat_soc_voltage_mv(const bat_soc_t* s)
{
    return s->v_filt;
}

/* Latest unfiltered burst median in mV, for a live voltage display. */
static inline int bat_soc_voltage_live_mv(const bat_soc_t* s)
{
    return s->v_live;
}
