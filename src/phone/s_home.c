/* Home and Discover: the wordmark and the Settings gear, chips to narrow the rows (Series, Films, a list of the
   catalogs), a big card for one title, Continue Watching, then the catalogs of the installed addons as rows.
   Discover has the same rows without the big card. */
#include "core/notify.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "phone/phone.h"
#include "core/keys.h"
#include "core/i18n.h"
#include "core/json.h"
#include "ui/text.h"
#include "ui/img.h"
#include "ui/icon.h"
#include "ui/widgets.h"
#include "ui/palette.h"
#include "core/live.h"
float ph_live_row(float y, Scroll *s, int rowIdx, HitFn focus, int focusedIdx); const Item *ph_live_band_item(int i); int ph_live_band(const Item **items);

#define MAXR 40
typedef struct { CatalogDef def; ItemList list; int got; Scroll s; int retries; double retryAt; } HRow; /* retryAt: no answer, asked again then (8, 20, 45 s) */
typedef struct { HRow rows[MAXR]; int n; ItemList cw, up; Scroll cws, ups, v; unsigned lib, gen; int filter, fr, fi; float lastY[MAXR + 2]; int heroRow; Timer1 in; Timer1 sw; int swDir; } Board;
static Board B[2]; /* Home, Discover */
/* Discover's spotlight: this year's films and the trending series, alternating, eight in all; it slides on by itself
   every 5 s (and by the finger), dots under it */
#define NFEAT 8
#include <time.h>
static struct { Item it[NFEAT]; int n, load, gotM, gotS; ItemList m, sr; Scroll s; int idx, dragged; double touchAt; Tween dotW[NFEAT]; } F;
/* the spotlight's picture kept while a page beside this one is shown (Settings, Library): swiped back to, it is there at
   once, never loaded again as it comes in (the pages' many posters would have pushed it out of the budget) */
static char keepUrl[600]; static int keepW, keepH;
static void home_backdrop_url(const char *src, char *out, size_t cap);
void ph_home_keep(void) { if (keepUrl[0] && keepW > 0) img_get_shaped(keepUrl, keepW, keepH, 0, 1); }
static int feat_keep(const Item *it) { return it->id[0] && (it->background[0] || it->poster[0]); }
static void feat_from_rows(Board *b) { /* the spotlight from Home's own catalogs: the first titles of its rows, in turn */
  if (F.n) return; int at[MAXR] = { 0 }, any = 1;
  while (F.n < NFEAT && any) { any = 0; for (int i = 0; i < b->n && F.n < NFEAT; i++) { HRow *r = &b->rows[i]; if (!r->got) continue; if (b->filter && strcmp(r->def.type, b->filter == 1 ? "series" : "movie")) continue; /* (Series or Movies chosen: only those) */
      while (at[i] < r->list.n && !feat_keep(&r->list.items[at[i]])) at[i]++;
      if (at[i] < r->list.n) { const Item *c = &r->list.items[at[i]++]; int dup = 0; for (int k = 0; k < F.n; k++) if (!strcmp(F.it[k].id, c->id)) dup = 1; if (!dup) F.it[F.n++] = *c; any = 1; } } }
  if (F.n) { F.touchAt = app_now(); for (int i = 0; i < NFEAT; i++) tween_set(&F.dotW[i], i == 0 ? 34 : 12); } }
static void feat_act(int a, int b2) { if (b2 < 0 || b2 >= F.n) return; Item it = F.it[b2];
  if (a == 1) { DetailParams p; memset(&p, 0, sizeof p); p.it = it; p.openStreams = 1; app_go(SCR_DETAIL, &p, 0); }
  else if (a == 2) { lib_toggle(&it); toast(lib_has(it.id) ? T("Added to Library") : T("Removed from Library"), 1600, NULL); app_refresh_boards(); }
  else app_open_item(&it); }
/* the spotlight, drawn at y: each title a page of the strip; returns its height */
/* a film's backdrop for other pages to show (Settings' previews): the spotlight's one, "" before it has any */
const char *ph_home_backdrop(char *out, size_t cap) { out[0] = 0; if (!F.n) return out; const Item *hi = &F.it[F.idx]; const cJSON *hm = sb_meta_cached(hi->type, hi->id); Item full; if (hm) sb_fill_item(&full, hm, hi->type); else full = *hi; const char *b = sb_bg_mid(&full, out, cap); if (b != out) snprintf(out, cap, "%s", b ? b : ""); if (out[0]) { char hi[600]; home_backdrop_url(out, hi, sizeof hi); snprintf(out, cap, "%s", hi); } return out; }
/* the titles of the spotlight, for Appearance's preview of "from the logo / artwork": i-th one's id, backdrop, logo, poster and name; the count */
int ph_home_demo(int i, Item *out, char *bg, size_t cap) { if (!F.n) return 0; i %= F.n; const Item *hi = &F.it[i]; const cJSON *hm = sb_meta_cached(hi->type, hi->id); Item full; if (hm) { sb_fill_item(&full, hm, hi->type); if (!full.poster[0]) snprintf(full.poster, sizeof full.poster, "%s", hi->poster); } else full = *hi;
  *out = full; const char *b = sb_bg_mid(&full, bg, cap); if (b != bg) snprintf(bg, cap, "%s", b ? b : ""); return F.n; }
static uint32_t disc_page(void) { return col_mix(PAGE | 0xff000000u, ACC | 0xff000000u, 0.18f); } /* Discover's page: tinted by the shown title's colour */
/* Series or Movies chosen (or All again): the spotlight's titles change; the picture shown stays until the new one's is
   in, then the new one comes over it, its words with it (no empty box, nothing popping in) */
static char swOld[600]; static double swReadyAt; static int swOn;
static void feat_switch(void) { if (F.n && keepUrl[0]) { snprintf(swOld, sizeof swOld, "%s", keepUrl); swOn = 1; swReadyAt = 0; } F.n = 0; F.idx = 0; F.s.pos = 0; F.s.vel = 0; F.touchAt = app_now(); }
static float sw_k(void) { if (!swOn) return 1; if (!swReadyAt) return 0; float k = (float)((app_now() - swReadyAt) / 0.4); if (k >= 1) { swOn = 0; swOld[0] = 0; return 1; } return k * k * (3 - 2 * k); }
static float feat_draw(float y) {
  float hh = SB_W * 1.02f + INS_T, W = SB_W; int wide = SB_W > SB_H, tab = ph_tabw(); if (wide) hh = SB_H * 0.74f; if (tab) hh = SB_H * 0.62f; /* (a tablet sideways: a big picture at the top; going down it slides away slower and dims, the rows come up over it) */
  float bh = tab ? W * 9 / 16 : hh, boff = tab ? clampf(bh * 0.35f - hh / 2, 0, bh - hh) : 0, py = tab && y < 0 ? y * 0.5f : y, gone = tab ? clampf(-y / (hh * 0.8f), 0, 1) : 0; /* (the band: the picture whole, framed a third of the way down where faces are, never cut through the middle) */ /* (sideways: shorter, its words up out of the bar's way) */ /* (sideways: the screen's height, not its width) */
  if (!F.n) { GLuint ot = swOn && swOld[0] ? img_get_shaped(swOld, (int)W, (int)bh, 0, 1) : 0; gfx_clip_push(0, y, W, hh); if (ot) gfx_image(ot, 0, y - boff, W, bh, 0, WHITE_A(255), 0, 0, 1, 1); else ph_box(0, y, W, hh, 0); gfx_clip_pop(); return hh; } /* (the new titles not known yet: the picture shown before stays) */
  /* the strip: the finger moves it; let go, it settles on the page it was nearest (a flick goes one on); alone 5 s, it goes on */
  { static double drawnAt; if (drawnAt > 0 && app_now() - drawnAt > 0.3) F.touchAt += app_now() - drawnAt; drawnAt = app_now(); } /* (out of sight, its clock stops: back to it, as it was left, the one a page came from or goes back into still there) */
  ph_drag(0, y, W, hh - 120, &F.s, 0);
  if (F.s.drag) { F.dragged = 1; F.touchAt = app_now(); }
  else { if (F.dragged) { F.dragged = 0; float v = -F.s.vel; int to = (int)floorf(F.s.pos / W + 0.5f); if (v < -600 && F.s.pos / W < F.idx + 0.5f) to = F.idx + 1; else if (v > 600 && F.s.pos / W > F.idx - 0.5f) to = F.idx - 1; F.idx = to < 0 ? 0 : to >= F.n ? F.n - 1 : to; F.s.vel = 0; F.touchAt = app_now(); }
    if (app_now() - F.touchAt > 5) { F.idx = (F.idx + 1) % F.n; F.touchAt = app_now(); }
    float tgt = F.idx * W; F.s.pos += (tgt - F.s.pos) * clampf(phDt * 9, 0, 1); if (fabsf(tgt - F.s.pos) < 0.5f) F.s.pos = tgt; }
  F.s.max = (F.n - 1) * W;
  { const Item *c = &F.it[F.idx]; accent_scope(c->id, c->poster, c->logo); } /* (not while drawn as a picture under another page: that page keeps its colour) */ /* the shown title's colour, for the whole page (as the user's colour setting allows; it fades over) */
  /* the titles crossfade as they change (a picture moves a little, never cut by its neighbour's edge): the pictures, the nearest last */
  int ord[NFEAT], no = 0; for (int i = 0; i < F.n; i++) { float d = fabsf((i * W - F.s.pos) / W); if (d < 1) ord[no++] = i; } for (int a1 = 1; a1 < no; a1++) for (int b1 = a1; b1 > 0 && fabsf(ord[b1] * W - F.s.pos) > fabsf(ord[b1 - 1] * W - F.s.pos); b1--) { int tq = ord[b1]; ord[b1] = ord[b1 - 1]; ord[b1 - 1] = tq; }
  gfx_clip_push(0, y < 0 ? 0 : y, W, y < 0 ? hh + y : hh); /* (scrolled down: it ends where the spotlight ends, never below it) */
  float swk = 1; if (swOn) { /* the picture before, under the new one until it has come in */
    GLuint ot = swOld[0] ? img_get_shaped(swOld, (int)W, (int)bh, 0, 1) : 0; if (ot) gfx_image(ot, 0, y - boff, W, bh, 0, WHITE_A(255), 0, 0, 1, 1);
    if (!swReadyAt) { const Item *hi = &F.it[F.idx]; const cJSON *hm = sb_meta_cached(hi->type, hi->id); Item full; if (hm) sb_fill_item(&full, hm, hi->type); else full = *hi; char nb[600]; const char *bg = sb_bg_mid(&full, nb, sizeof nb);
      if (!bg || !*bg || img_get_shaped(bg, (int)W, (int)bh, 0, 1) || img_shaped_failed(bg, (int)W, (int)bh, 0, 1)) swReadyAt = app_now(); }
    swk = sw_k(); }
  gfx_opacity_push(swk);
  { float f = clampf(F.s.pos / W, 0, (float)(F.n - 1)); int i0 = (int)floorf(f); if (i0 > F.n - 1) i0 = F.n - 1; float t = f - i0; t = t * t * (3 - 2 * t); /* the one going under, the next over it fading in: never an edge between them */
    for (int q = 0; q < 2; q++) { int i = i0 + q; if (i >= F.n || (q == 1 && t < 0.003f)) continue; const Item *hi = &F.it[i]; char buf[600];
      const cJSON *hm = sb_meta_cached(hi->type, hi->id); Item full; if (hm) { sb_fill_item(&full, hm, hi->type); if (!full.poster[0]) snprintf(full.poster, sizeof full.poster, "%s", hi->poster); } else full = *hi;
      const char *bg = sb_bg_mid(&full, buf, sizeof buf); char hiBg[600]; if (bg && *bg) { home_backdrop_url(bg, hiBg, sizeof hiBg); bg = hiBg; } GLuint tx = bg && *bg ? img_get_shaped(bg, (int)W, (int)bh, 0, 1) : 0; if (i == F.idx && bg) { snprintf(keepUrl, sizeof keepUrl, "%s", bg); keepW = (int)W; keepH = (int)bh; } float k = q ? 1.05f - 0.05f * t : 1 + 0.03f * t; /* the old one drifts a little closer, the new settles in */
      gfx_opacity_push(q ? t : 1); gfx_scale_push(k, k, W / 2, y + hh / 2); if (!tx || !ph_picture(tx, 0, py - boff, W, bh, 0, WHITE_A((int)(255 * (1 - 0.7f * gone))))) ph_box(0, y, W, hh, 0); gfx_scale_pop(); gfx_opacity_pop(); } }
  { int nx = (F.idx + 1) % F.n; if (nx != F.idx) { const Item *ni = &F.it[nx]; const cJSON *nm = sb_meta_cached(ni->type, ni->id); Item nf; if (nm) sb_fill_item(&nf, nm, ni->type); else nf = *ni; char nb[600]; const char *nbg = sb_bg_mid(&nf, nb, sizeof nb); if (nbg && *nbg) img_get_shaped(nbg, (int)W, (int)bh, 0, 1); } } /* the next title's picture asked ahead: on a big screen it takes a moment, and it is there when its turn comes */
  gfx_opacity_pop();
  { int i = 0; float px = 0; (void)i; (void)px;
    gfx_rrect_v(0, y, W, INS_T + 120, 0, BLACK_A(120), BLACK_A(0)); { uint32_t pg = disc_page(); float g0 = y + hh * 0.30f, gh = hh * 0.62f; /* into the page: eased in eight steps (no edge where a straight fade ends, whatever the picture), solid for the last part */
      for (int q = 0; q < 8; q++) { float u0 = q / 8.0f, u1 = (q + 1) / 8.0f, e0 = u0 * u0 * (3 - 2 * u0), e1 = u1 * u1 * (3 - 2 * u1); gfx_rrect_v(0, g0 + gh * u0, W, gh * (u1 - u0), 0, col_alpha(pg, e0), col_alpha(pg, e1)); /* (edge to edge: an overlap would show as a line) */ }
      gfx_rect(0, g0 + gh, W, hh - (g0 + gh - y) + 2, pg); }
    if (wide) { uint32_t pg2 = disc_page(); for (int q = 0; q < 8; q++) { float u0 = q / 8.0f, u1 = (q + 1) / 8.0f, e0 = 1 - u0 * u0 * (3 - 2 * u0), e1 = 1 - u1 * u1 * (3 - 2 * u1); gfx_rrect_h(W * 0.62f * u0, y, W * 0.62f * (u1 - u0), hh, 0, col_alpha(pg2, 0.62f * e0), col_alpha(pg2, 0.62f * e1)); } } /* in the page's own colour: where it meets the page there is nothing to see */ /* sideways, as on the TV: a shade from the left that its words sit on */
  }
  /* its words: they move a little more and fade quicker */
  for (int q = 0; q < no; q++) { int i = ord[q]; float px = i * W - F.s.pos, al = clampf(1 - fabsf(px) / W * 1.6f, 0, 1); if (al <= 0.01f) continue; const Item *hi = &F.it[i]; char buf[600]; (void)buf;
    const cJSON *hm = sb_meta_cached(hi->type, hi->id); Item full; if (hm) { sb_fill_item(&full, hm, hi->type); if (!full.poster[0]) snprintf(full.poster, sizeof full.poster, "%s", hi->poster); } else full = *hi;
    gfx_opacity_push(al * swk); gfx_offset_push(px * 0.4f, 0);
    float lw = wide ? W * 0.34f : W * 0.64f, lh = wide ? 170 : 130, ly = wide ? y + hh - 96 - (lh + 60 + 3 * 32) : y + hh - 390, tw0 = wide ? W * 0.42f : W - 2 * PAD, cx = wide ? PAD + 20 : W / 2;
    if (tab) { lw = W * 0.30f; lh = 190; tw0 = W * 0.40f; cx = PAD + 36; ly = y + hh - 70 - (lh + 14 + 34 + 12 + 3 * 34); } /* a tablet sideways: a column of its own on the left, its words larger */ int al0 = wide ? ALIGN_START : ALIGN_CENTER; /* sideways: bottom left over the picture, as on the TV; upright: centred */ char lb[600]; const char *logo = sb_logo_of(&full, lb, sizeof lb); float asp = 0;
    GLuint lt = logo && *logo ? img_get(logo, (int)lw, (int)lh, &asp) : 0;
    if (lt && asp > 0) { float w = lw, h = w / asp; if (h > lh) { h = lh; w = h * asp; } gfx_image(lt, wide ? cx : W / 2 - w / 2, ly + lh - h, w, h, 0, WHITE_A(255), 0, 0, 1, 1); }
    else if (!(logo && *logo) || img_failed(logo)) text_draw_wrapped(hi->name, cx, ly + lh - 108, wide ? tw0 : W - 2 * PAD, 2, F_H1, 800, TXT, -0.6f, al0, 54); /* (the name only when there is no logo) */
    { char g[200]; const char *kind = !strcmp(hi->type, "series") ? T("Series") : T("Film"); snprintf(g, sizeof g, "%s  \xc2\xb7  %s", T("Trending now"), kind); const cJSON *gg; int k = 0; cJSON_ArrayForEach(gg, js_arr(hm, "genres")) if (cJSON_IsString(gg) && k < 2) { size_t l = strlen(g); snprintf(g + l, sizeof g - l, "  \xc2\xb7  %s", gg->valuestring); k++; }
      text_draw(g, cx, ly + lh + 14, tab ? F_SMALL + 3 : F_SMALL, 600, WHITE_A(205), 0.2f, tw0, al0); }
    { const char *d = js_str(hm, "description", hi->description); if (d && *d) { if (tab) text_draw_wrapped(d, cx, ly + lh + 14 + 34 + 12, tw0 - 20, 3, F_BODY - 1, 400, WHITE_A(210), 0, al0, 34); else text_draw_wrapped(d, cx, ly + lh + 60, tw0 - 20, 3, F_SMALL + 1, 400, WHITE_A(200), 0, al0, 32); } } /* its story, under it */
    if (i == F.idx) hit_act(0, y + INS_T + 60, W, hh - 120 - INS_T, feat_act, 0, i);
    gfx_offset_pop(); gfx_opacity_pop(); }
  gfx_clip_pop();
  /* the dots: the one shown long, the others small, gliding */
  { float dy = y + hh - 34, tot = 0; for (int i = 0; i < F.n; i++) { tween_to(&F.dotW[i], i == F.idx ? 34 : 12, 0.3f, EASE_WEB); tween_tick(&F.dotW[i], phDt); tot += F.dotW[i].v + 10; } tot -= 10;
    float dx = W / 2 - tot / 2; for (int i = 0; i < F.n; i++) { float w = F.dotW[i].v, on = clampf((w - 12) / 22, 0, 1); gfx_rrect(dx, dy, w, 12, 6, col_mix(WHITE_A(80), WHITE_A(240), on)); if (i == F.idx && app_now() - F.touchAt < 5 && !F.s.drag) { float f = (float)((app_now() - F.touchAt) / 5); gfx_clip_push(dx, dy - 1, w * f, 14); gfx_rrect(dx, dy, w, 12, 6, ACC2 | 0xff000000u); gfx_clip_pop(); } /* (the whole dot revealed from its start: never a rounded shape narrower than its rounding) */ dx += w + 10; } } /* (the one shown fills up until it moves on) */
  return hh;
}
static Board *me(void) { return &B[0]; } /* (one board: Home) */
static void lib_rows(Board *b) { itemlist_clear(&b->cw); lib_cw_items(&b->cw); itemlist_clear(&b->up); notify_upcoming(&b->up); b->lib = lib_version(); } /* Continue Watching (new episodes too) and Upcoming: from the library */
static void cw_card_labels(const ItemList *l, const Scroll *s, float y) {
  if (!l || !s) return; float cw = WIDE_CW * 1.30f, ch = WIDE_CH * 1.30f, gap = 14, top = 52; int rtl = app_rtl();
  for (int i = 0; i < l->n; i++) {
    float x = PAD + i * (cw + gap) - s->pos; if (rtl) x = SB_W - x - cw;
    if (x > SB_W || x + cw < 0) continue;
    CwInfo info; if (lib_cw_info(&l->items[i], &info) && info.leftMin > 0) {
      char rem[48]; snprintf(rem, sizeof rem, "متبقي %.0f د", info.leftMin);
      float bw = text_width(rem, F_TINY, 800, 0) + 24, bx = x + cw - bw - 12, by = y + top + 12;
      gfx_rrect(bx, by, bw, 34, 17, BLACK_A(190)); text_draw(rem, bx + bw / 2, by + 17 - text_line_height(F_TINY) / 2.0f, F_TINY, 800, WHITE_A(245), 0, 0, ALIGN_CENTER);
    }
    {
      char label[384];
      if (!strcmp(l->items[i].type, "series")) {
        int season = 0, episode = 0; const char *sep = strchr(l->items[i].videoId, ':');
        if (sep) sscanf(sep + 1, "%d:%d", &season, &episode);
        if (season > 0 && episode > 0) snprintf(label, sizeof label, "%s  ·  S%d E%d", l->items[i].name, season, episode);
        else snprintf(label, sizeof label, "%s", l->items[i].name);
      } else snprintf(label, sizeof label, "%s", l->items[i].name);
      text_draw(label, x + cw / 2, y + top + ch + 5, F_TINY + 1, 700, TXT, 0, cw - 8, ALIGN_CENTER);
    }
  }
}


static void got_row(ItemList *l, int cached, void *ud) { (void)cached; unsigned tag = (unsigned)(uintptr_t)ud; Board *b = &B[tag >> 24]; int i = (tag >> 8) & 255; if ((tag & 255) != (b->gen & 255) || i >= b->n || !l) { if (l) itemlist_free(l); return; }
  HRow *r = &b->rows[i]; if (!l->n && l->total < 0 && !r->list.n && r->retries < 3) { static const double W[3] = { 8, 20, 45 }; r->retryAt = app_now() + W[r->retries]; r->retries++; itemlist_free(l); fprintf(stderr, "[home] %s: no answer, asked again in %.0f s\n", r->def.title, W[r->retries - 1]); return; } /* (an addon that makes its lists on demand: a slow first answer is not the end of the row) */
  if (l->n > 0 || !r->got) { itemlist_clear(&r->list); r->list = *l; free(l); } else itemlist_free(l); r->got = 1; }
static void home_backdrop_url(const char *src, char *out, size_t cap) {
  if (!cap) return;
  snprintf(out, cap, "%s", src ? src : "");
  char *p = strstr(out, "/small/");
  if (p) memcpy(p, "/large/", 7);
  else if ((p = strstr(out, "/medium/")) != NULL) { memmove(p + 10, p + 8, strlen(p + 8) + 1); memcpy(p, "/original/", 10); }
  else {
    static const char *const sizes[] = { "/w300/", "/w500/", "/w780/" };
    for (int i = 0; i < 3; i++) if ((p = strstr(out, sizes[i])) != NULL) {
      memmove(p + 7, p + 6, strlen(p + 6) + 1); memcpy(p, "/w1280/", 7); break;
    }
  }
}
static int home_has(const char *s, const char *needle) {
  if (!s || !needle || !*needle) return 0;
  for (; *s; s++) { const char *a = s, *b = needle; while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; } if (!*b) return 1; }
  return 0;
}
static int home_pick(const CatalogDef *src, int n, const char *type, const char *const *words, int nw, const int *used) {
  for (int i = 0; i < n; i++) {
    if (used && used[i]) continue;
    if (type && strcmp(src[i].type, type)) continue;
    for (int k = 0; k < nw; k++) if (home_has(src[i].title, words[k])) return i;
  }
  return -1;
}
static int home_curate(CatalogDef *out, int max) {
  CatalogDef all[MAXR]; int n = sb_home_defs(all, MAXR), used[MAXR] = {0}, count = 0;
  static const char *trend[] = { "trending", "popular", "top" };
  static const char *now[] = { "now playing", "airing", "currently" };
  static const char *movie[] = { "movies", "movie", "film" };
  static const char *series[] = { "series", "shows", "tv" };
  static const char *anime[] = { "anime", "animation" };
  const char *labels[4] = { "Trending now", "Now playing", "Movies", "Series" };
  const char *const *terms[4] = { trend, now, movie, series };
  const int lens[4] = { 3, 3, 3, 3 };
  const char *types[4] = { NULL, "movie", "movie", "series" };
  int reserved[MAXR] = {0}, generalUsed[MAXR] = {0};
  for (int i = 0; i < n; i++) reserved[i] = home_has(all[i].title, "anime") || home_has(all[i].title, "animation");
  for (int i = 0; i < n; i++) generalUsed[i] = reserved[i] || used[i];
  for (int slot = 0; slot < 4 && count < max; slot++) {
    int pick = home_pick(all, n, types[slot], terms[slot], lens[slot], generalUsed);
    if (pick < 0) for (int i = 0; i < n; i++) if (!generalUsed[i] && (!types[slot] || !strcmp(all[i].type, types[slot]))) { pick = i; break; }
    if (pick < 0) continue;
    out[count] = all[pick]; snprintf(out[count].title, sizeof out[count].title, "%s", labels[slot]); used[pick] = 1; generalUsed[pick] = 1; count++;
  }
  /* Anime gets its own series and movie catalogs; generic catalogs are safely reused
     with an Animation genre constraint when an addon has no anime-specific catalog. */
  for (int kind = 0; kind < 2 && count < max; kind++) {
    const char *type = kind == 0 ? "series" : "movie";
    int pick = home_pick(all, n, type, anime, 2, used);
    if (pick < 0) for (int i = 0; i < n; i++) if (!strcmp(all[i].type, type)) { pick = i; break; }
    if (pick < 0) continue;
    out[count] = all[pick];
    snprintf(out[count].title, sizeof out[count].title, "%s", kind == 0 ? "مسلسلات الأنمي" : "أفلام الأنمي");
    if (!home_has(all[pick].title, "anime") && !home_has(all[pick].title, "animation")) {
      char extra[200] = ""; snprintf(extra, sizeof extra, "%s", out[count].extra);
      size_t n0 = strlen(extra);
      if (n0 < sizeof extra - 1) snprintf(extra + n0, sizeof extra - n0, "%sgenre=Animation", n0 ? "&" : "");
      snprintf(out[count].extra, sizeof out[count].extra, "%s", extra);
      snprintf(out[count].url, sizeof out[count].url, "%s/%s.json", out[count].path, out[count].extra);
    }
    used[pick] = 1; count++;
  }
  return count;
}
static void load(Board *b, int disc) {
  CatalogDef defs[MAXR]; int n = disc ? sb_disc_defs(defs, MAXR) : home_curate(defs, 7);
  live_refresh(0); if (live_catalogs() > 0) { int k = 0; for (int i = 0; i < n; i++) if (!live_type(defs[i].type)) defs[k++] = defs[i]; n = k; } /* the live catalogs: in the Live TV row's All channels, not as rows of posters */
  for (int i = 0; i < b->n; i++) itemlist_clear(&b->rows[i].list); memset(b->rows, 0, sizeof b->rows); b->n = n; b->gen++;
  for (int i = 0; i < n; i++) { b->rows[i].def = defs[i]; b->rows[i].retries = 0; b->rows[i].retryAt = 0; sb_catalog(&b->rows[i].def, got_row, (void *)(uintptr_t)((disc << 24) | (i << 8) | (b->gen & 255))); }
  lib_rows(b); b->fr = -1; (void)disc; F.n = 0;
}
static const Item *hero_item(Board *b) { for (int i = 0; i < b->n; i++) if (b->rows[i].list.n > 0 && (b->filter == 0 || !strcmp(b->rows[i].def.type, b->filter == 1 ? "series" : "movie"))) { b->heroRow = i; return &b->rows[i].list.items[0]; } return NULL; }
static const Item *focused_item(void) { Board *b = me(); if (b->fr == -5) return ph_live_band_item(b->fi); if (b->fr == -2) return b->cw.n > b->fi ? &b->cw.items[b->fi] : NULL; if (b->fr == -4) return b->up.n > b->fi ? &b->up.items[b->fi] : NULL; if (b->fr == -3) return hero_item(b); if (b->fr >= 0 && b->fr < b->n && b->fi < b->rows[b->fr].list.n) return &b->rows[b->fr].list.items[b->fi]; return NULL; }
static void focus(int r, int i) { Board *b = me(); b->fr = r; b->fi = i; const Item *it = focused_item(); if (it) { accent_tint_fetch(it->id, it->poster, it->logo); ph_logo_ahead(it); } }
static void home_card_tap(int r, int i) {
  Board *b = me();
  if (!b || r < 0 || r >= b->n || i < 0 || i >= b->rows[r].list.n) return;
  focus(r, i);
  app_open_item(&b->rows[r].list.items[i]);
} /* the title's colour is on its way before its page opens */
static void hero_act(int a, int b2) { (void)b2; Board *b = me(); const Item *it = hero_item(b); if (!it) return;
  if (a == 1) { if (!strcmp(it->type, "series")) { DetailParams p; memset(&p, 0, sizeof p); p.it = *it; p.openStreams = 1; app_go(SCR_DETAIL, &p, 0); } else ph_streams_show(it, it->id, NULL); }
  else if (a == 2) { lib_toggle(it); toast(lib_has(it->id) ? T("Added to Library") : T("Removed from Library"), 1600, NULL); app_refresh_boards(); }
  else { b->fr = -3; b->fi = 0; app_open_item(it); } }
/* Home's former context-chip handlers were removed with the redundant Series / Movies / Categories pills. */

static void enter(const void *params) { (void)params; Board *b = me(); bg_clear(); if (!b->n || b->lib != lib_version()) load(b, 0); else lib_rows(b); { extern int app_entered_by_swipe; if (app_entered_by_swipe) b->in.on = 0; else timer_start(&b->in, 0.4f); } } /* swiped back to: already in place */
static void leave(void) {}
static void tick(float dt) { (void)dt; { double now = app_now(); for (int k = 0; k < 2; k++) { Board *bb = &B[k]; for (int i = 0; i < bb->n; i++) { HRow *r = &bb->rows[i]; if (r->retryAt > 0 && now >= r->retryAt) { r->retryAt = 0; sb_catalog(&r->def, got_row, (void *)(uintptr_t)((k << 24) | (i << 8) | (bb->gen & 255))); } } } } Board *b = me(); timer_tick(&b->sw, dt); if (b->lib != lib_version()) lib_rows(b); timer_tick(&b->in, dt); }
static void draw(void) {
  Board *b = me(); int disc = 1; float in = ease_apply(EASE_OUT, timer_x(&b->in));
  ph_drag(0, 0, SB_W, SB_H - INS_B - BAR_H, &b->v, 1); scroll_tick(&b->v);
  /* Home: the cover of one title across the top (under the status bar), its logo, its genres, Play and My List on it;
     Discover: its rows from the top. No wordmark: the page is the content. */
  float y = -b->v.pos; if (disc) gfx_rect(0, 0, SB_W, SB_H, disc_page()); /* the page in the shown title's colour */
  /* Redundant top-right Series / Movies / Categories pills removed from Home. */
  const Item *hi = disc ? NULL : hero_item(b);
  if (!disc) {
    float hh = SB_W * 0.86f + INS_T; char buf[600];
    const cJSON *hm = hi ? sb_meta_cached(hi->type, hi->id) : NULL; Item full; if (hm) { sb_fill_item(&full, hm, hi->type); if (!full.poster[0]) snprintf(full.poster, sizeof full.poster, "%s", hi->poster); } else if (hi) full = *hi;
    const char *bg = hi ? sb_bg_mid(&full, buf, sizeof buf) : NULL; char hiBg[600]; if (bg && *bg) { home_backdrop_url(bg, hiBg, sizeof hiBg); bg = hiBg; } GLuint t = bg && *bg ? img_get_shaped(bg, (int)SB_W, (int)hh, 0, 1) : 0;
    float py = y < 0 ? y * 0.5f : y; /* the cover moves at half the speed */
    /* as the page goes up, the cover dims, darkens at its edges and draws back a little; it fades in when it arrives */
    float gone = clampf(b->v.pos / (hh * 0.8f), 0, 1); static GLuint lastT; static Tween heroIn; if (t && t != lastT) { lastT = t; tween_set(&heroIn, 0); tween_to(&heroIn, 1, 0.6f, EASE_OUT); } tween_tick(&heroIn, phDt);
    float zs = 1.06f - 0.06f * ease_apply(EASE_OUT, heroIn.v) + 0.05f * gone; gfx_scale_push(zs, zs, SB_W / 2, py + hh * 0.45f);
    if (!t || !ph_picture(t, 0, py, SB_W, hh, 0, WHITE_A((int)(255 * heroIn.v * (1 - 0.85f * gone))))) ph_box(0, py, SB_W, hh, 0);
    gfx_scale_pop();
    if (gone > 0) { uint32_t pg = PAGE | 0xff000000u; float e = SB_W * 0.35f; /* the vignette closes in from the sides as it goes */
      gfx_rrect_h(0, py, e, hh, 0, col_alpha(pg, 0.9f * gone), col_alpha(pg, 0)); gfx_rrect_h(SB_W - e, py, e, hh, 0, col_alpha(pg, 0), col_alpha(pg, 0.9f * gone)); gfx_rect(0, py, SB_W, hh, col_alpha(pg, 0.45f * gone)); }
    if (hi) hit_add(0, y, SB_W, hh - 150, focus, NULL, -3, 0);
    gfx_rrect_v(0, py, SB_W, INS_T + 90, 0, BLACK_A(110), BLACK_A(0));
    gfx_rrect_v(0, y + hh * 0.38f, SB_W, hh * 0.62f + 2, 0, col_alpha(PAGE | 0xff000000u, 0), PAGE | 0xff000000u);
    float ly = y + hh - 250; char lb[600]; const char *logo = hi ? sb_logo_of(&full, lb, sizeof lb) : NULL; float lw = SB_W * 0.62f, lh = 120, asp = 0;
    GLuint lt = logo && *logo ? img_get(logo, (int)lw, (int)lh, &asp) : 0;
    if (lt && asp > 0) { float w = lw, h = w / asp; if (h > lh) { h = lh; w = h * asp; } gfx_image(lt, SB_W / 2 - w / 2, ly + lh - h, w, h, 0, WHITE_A(255), 0, 0, 1, 1); }
    else if (hi) text_draw_wrapped(hi->name, SB_W / 2, ly + 20, SB_W - 2 * PAD, 2, F_H1, 800, TXT, -0.6f, ALIGN_CENTER, 54);
    if (hi) { char g[200] = ""; const cJSON *gg; int k = 0; cJSON_ArrayForEach(gg, js_arr(hm, "genres")) if (cJSON_IsString(gg) && k < 3) { size_t l = strlen(g); snprintf(g + l, sizeof g - l, "%s%s", k ? "  \xc2\xb7  " : "", gg->valuestring); k++; }
      if (g[0]) text_draw(g, SB_W / 2, ly + lh + 12, F_SMALL, 600, WHITE_A(200), 0.2f, SB_W - 2 * PAD, ALIGN_CENTER); }
    float by = y + hh - 88, bw = (SB_W - 2 * PAD - 16) / 2;
    button(PAD, by, bw, 76, "play", T("Play"), 2, hero_act, 1, 0); button(PAD + bw + 16, by, bw, 76, hi && lib_has(hi->id) ? "ph_lib" : "ph_plus", T("My List"), 0, hero_act, 2, 0);
    y += hh + 30; }
  else { int ready = 0, total = 0; for (int i = 0; i < b->n; i++) { total++; ready += b->rows[i].got; }
    { static int fWas; if (fWas != b->filter) { fWas = b->filter; feat_switch(); } } /* (another choice: the spotlight's titles taken again, of that kind) */
    if (!F.n && (ready >= 2 || (total && ready == total))) feat_from_rows(b); y += feat_draw(y) + 24; } /* the spotlight across the top, from the first rows that came */
  float sk = b->sw.on ? ease_apply(EASE_OUT, timer_x(&b->sw)) : 1; gfx_opacity_push((0.3f + 0.7f * in) * (0.2f + 0.8f * sk)); gfx_offset_push(110 * b->swDir * (1 - sk), 0);
  { /* Continue Watching, always: with Series or Films picked, only that kind */
    static ItemList cwF; static int fKey = -1, fN = -1; static const Item *fP;
    if (fKey != b->filter || fN != b->cw.n || fP != b->cw.items) { fKey = b->filter; fN = b->cw.n; fP = b->cw.items; itemlist_clear(&cwF); cwF.items = calloc(b->cw.n ? b->cw.n : 1, sizeof(Item)); cwF.n = 0;
      for (int i = 0; i < b->cw.n; i++) if (b->filter == 0 || !strcmp(b->cw.items[i].type, b->filter == 1 ? "series" : "movie")) cwF.items[cwF.n++] = b->cw.items[i]; }
    { static float cwK = -1; const ItemList *watch = b->filter ? &cwF : &b->cw; float rowY = y; if (cwF.n > 0) b->lastY[0] = y + b->v.pos; y += row_draw_k(T("Continue Watching"), watch, 1, y, &b->cws, -2, focus, b->fr == -2 ? b->fi : -1, &cwK); cw_card_labels(watch, &b->cws, rowY); } }
  /* Home's discovery surface is intentionally limited to the five curated catalog rows below. */
  int waiting = 0;
  for (int i = 0; i < b->n; i++) { HRow *r = &b->rows[i]; b->lastY[i + 2] = y + b->v.pos - INS_T - 20;
    if (b->filter && strcmp(r->def.type, b->filter == 1 ? "series" : "movie")) continue;
    if (!r->got) { waiting++; continue; }
    char t[200]; snprintf(t, sizeof t, "%s", T(r->def.title)); y += row_draw_act(t, &r->list, 0, y, &r->s, i, focus, home_card_tap, b->fr == i ? b->fi : -1); }
  gfx_offset_pop();
  if (waiting) for (int k = 0; k < 2 && y < SB_H; k++) { ph_text(PAD, y + 10, 260, 1, F_H2 - 2, 40, ALIGN_START); for (int c = 0; c < 4; c++) ph_box(PAD + c * (POSTER_W + 14), y + 52, POSTER_W, POSTER_H, 14); y += 52 + POSTER_H + 36; }
  gfx_opacity_pop();
  b->v.max = y + b->v.pos - (SB_H - INS_B - BAR_H - ctx_room()) + 24;
  /* the top: a shade under the status bar once the page has moved; Discover's name */
  float hk = b->v.pos > 120 ? 1 : b->v.pos / 120; hk = hk * hk * (3 - 2 * hk); phone_top_glass(INS_T, hk);
}
static int key(int k) { Board *b = me();
  if (k == K_OK) { const Item *it = focused_item(); if (it) app_open_item(it); return 1; }
  if (k == K_CH_UP) { scroll_top(&b->v); return 1; }
  if (k == K_BACK) { if (b->v.pos > 20) { scroll_top(&b->v); return 1; } return 0; }
  return 0;
}
static const Item *focused(void) { return focused_item(); }
static int origin(float *x, float *y) { *x = SB_W / 2; *y = SB_H / 2; return 0; }
/* A phone-native category view: it owns its catalog query, three filter pills and a vertically scrolling poster grid.
   It is an overlay over Home, so the top navigation pill and the Home spotlight remain intact underneath. */
static struct {
  CatalogDef base, query;
  ItemList all, shown;
  char title[96], genre[48];
  int year, sort, menu, busy, focus, pageBusy, more, skip;
  double pageRetryAt;
  unsigned gen;
  Scroll scroll;
} HC;
static int hc_year(const Item *it) {
  const char *p = it->metaLine;
  while (p && *p) { if (p[0] && p[1] && p[2] && p[3] && p[0] >= '1' && p[0] <= '2' && p[1] >= '0' && p[1] <= '9' && p[2] >= '0' && p[2] <= '9' && p[3] >= '0' && p[3] <= '9') { int y = (p[0]-'0')*1000 + (p[1]-'0')*100 + (p[2]-'0')*10 + p[3]-'0'; if (y >= 1890 && y <= 2100) return y; } p++; }
  return 0;
}
static double hc_rating(const Item *it) { const cJSON *m = sb_meta_cached(it->type, it->id); return js_num(m, "imdbRating", js_num(m, "rating", 0)); }
static int hc_sort_mode;
static int hc_cmp(const void *aa, const void *bb) {
  const Item *a = aa, *b = bb;
  double x = hc_sort_mode == 1 ? a->airAt : hc_sort_mode == 2 ? hc_year(a) : hc_rating(a);
  double y = hc_sort_mode == 1 ? b->airAt : hc_sort_mode == 2 ? hc_year(b) : hc_rating(b);
  return x < y ? 1 : x > y ? -1 : 0;
}
static void hc_apply(void) {
  itemlist_clear(&HC.shown);
  if (!HC.all.n) return;
  HC.shown.items = calloc((size_t)HC.all.n, sizeof(Item));
  if (!HC.shown.items) return;
  for (int i = 0; i < HC.all.n; i++) {
    int y = hc_year(&HC.all.items[i]), keep = !HC.year || (HC.year == 1 ? y > 0 && y < 1980 : HC.year == 2020 ? y >= 2020 && y <= 2023 : y == HC.year);
    if (keep) HC.shown.items[HC.shown.n++] = HC.all.items[i];
  }
  HC.shown.total = HC.shown.n;
  if (HC.sort && HC.shown.n > 1) { hc_sort_mode = HC.sort; qsort(HC.shown.items, (size_t)HC.shown.n, sizeof(Item), hc_cmp); }
  if (HC.focus >= HC.shown.n) HC.focus = HC.shown.n ? HC.shown.n - 1 : 0;
}
static void hc_got(ItemList *l, int cached, void *ud) {
  (void)cached;
  if ((unsigned)(uintptr_t)ud != HC.gen) { if (l) itemlist_free(l); return; }
  HC.busy = 0;
  if (l) { itemlist_clear(&HC.all); HC.all = *l; free(l); HC.skip = HC.all.n; HC.more = HC.all.n > 0; HC.pageBusy = 0; hc_apply(); }
}
static void hc_page_got(ItemList *l, int more, void *ud) {
  if ((unsigned)(uintptr_t)ud != HC.gen) { if (l) itemlist_free(l); return; }
  HC.pageBusy = 0;
  if (!l) { HC.pageRetryAt = app_now() + 2.0; return; }
  int got = l->n, added = 0;
  if (got > 0) {
    Item *items = realloc(HC.all.items, (size_t)(HC.all.n + got) * sizeof(Item));
    if (items) {
      HC.all.items = items;
      for (int i = 0; i < got; i++) {
        int dup = 0;
        for (int j = 0; j < HC.all.n && !dup; j++)
          dup = !strcmp(HC.all.items[j].type, l->items[i].type) && !strcmp(HC.all.items[j].id, l->items[i].id);
        if (!dup) { HC.all.items[HC.all.n++] = l->items[i]; added++; }
      }
    }
  }
  HC.skip += got;
  /* When an addon omits hasMore, keep paging while it returns new items; stop on empty or repeated pages. */
  HC.more = got > 0 && added > 0 && (more < 0 || more != 0);
  if (!got) HC.more = 0;
  HC.pageRetryAt = 0;
  itemlist_free(l); hc_apply();
}
static void hc_urlenc(const char *src, char *dst, size_t cap) {
  static const char hex[] = "0123456789ABCDEF"; size_t n = 0;
  for (const unsigned char *p = (const unsigned char *)src; *p && n + 4 < cap; p++) {
    unsigned char c = *p;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') dst[n++] = (char)c;
    else { dst[n++] = '%'; dst[n++] = hex[c >> 4]; dst[n++] = hex[c & 15]; }
  }
  dst[n] = 0;
}
static void hc_query(void) {
  HC.gen++; HC.busy = 1; HC.pageBusy = 0; HC.more = 0; HC.skip = 0; HC.pageRetryAt = 0; HC.scroll.pos = HC.scroll.vel = HC.scroll.max = 0; HC.focus = 0;
  itemlist_clear(&HC.all); itemlist_clear(&HC.shown);
  HC.query = HC.base;
  /* Replace, rather than duplicate, a genre extra already present on the source catalog. */
  char clean[200] = "", extra[200]; snprintf(extra, sizeof extra, "%s", HC.base.extra);
  const char *p = extra;
  while (*p) {
    const char *end = strchr(p, '&'); size_t n = end ? (size_t)(end - p) : strlen(p);
    if (!(n >= 6 && !strncmp(p, "genre=", 6))) { size_t used = strlen(clean); if (used && used + 1 < sizeof clean) clean[used++] = '&', clean[used] = 0; if (n < sizeof clean - used) { memcpy(clean + used, p, n); clean[used + n] = 0; } }
    if (!end) break; p = end + 1;
  }
  if (HC.genre[0]) { char encoded[144]; hc_urlenc(HC.genre, encoded, sizeof encoded); size_t n = strlen(clean); snprintf(clean + n, sizeof clean - n, "%sgenre=%s", n ? "&" : "", encoded); }
  snprintf(HC.query.extra, sizeof HC.query.extra, "%s", clean);
  snprintf(HC.query.url, sizeof HC.query.url, "%s%s%s.json", HC.query.path, clean[0] ? "/" : "", clean);
  sb_catalog(&HC.query, hc_got, (void *)(uintptr_t)HC.gen);
}
static const char *hc_genres[] = { "All genres", "Action", "Comedy", "Drama", "Science Fiction", "Thriller", "Mystery", "Horror", "Adventure" };
static const char *hc_years[] = { "All years", "2026", "2025", "2024", "2020–2023", "Classic" };
static const int hc_year_values[] = { 0, 2026, 2025, 2024, 2020, 1 };
static const char *hc_sorts[] = { "Most popular", "Now playing", "Newest airing", "Highest rated" };
static int hc_choice;
static int hc_count(int menu) { return menu == 0 ? 9 : menu == 1 ? 6 : 4; }
static const char *hc_label(int menu, int i) { return menu == 0 ? T(hc_genres[i]) : menu == 1 ? T(hc_years[i]) : T(hc_sorts[i]); }
static void hc_filter_act(int menu, int b) { (void)b; HC.menu = HC.menu == menu ? -1 : menu; hc_choice = -1; }
static void hc_option_act(int i, int b) {
  (void)b;
  if (HC.menu == 0) { snprintf(HC.genre, sizeof HC.genre, "%s", i == 0 ? "" : hc_genres[i]); }
  else if (HC.menu == 1) HC.year = hc_year_values[i];
  else if (HC.menu == 2) HC.sort = i;
  HC.menu = -1; hc_query();
}
static void hc_focus(int r, int i) { (void)r; HC.focus = i; if (i >= 0 && i < HC.shown.n) accent_tint_fetch(HC.shown.items[i].id, HC.shown.items[i].poster, HC.shown.items[i].logo); }
static void hc_open_item(const Item *it);
static void hc_tap(int r, int i) { hc_focus(r, i); if (i >= 0 && i < HC.shown.n) hc_open_item(&HC.shown.items[i]); }
void ph_home_category_back(void) { app_overlay = NULL; HC.menu = -1; }
static const Item *hc_focused(void) { return HC.focus >= 0 && HC.focus < HC.shown.n ? &HC.shown.items[HC.focus] : NULL; }
static void hc_open_item(const Item *it) { if (it) { Item copy = *it; app_overlay = NULL; app_open_item(&copy); } }
static int hc_key(int k) {
  if (k == K_BACK) { app_overlay = NULL; HC.menu = -1; return 1; }
  if (k == K_OK && hc_focused()) { hc_open_item(hc_focused()); return 1; }
  if (k == K_CH_UP) { scroll_top(&HC.scroll); return 1; }
  return 0;
}
static void hc_tick(float dt) { (void)dt; }
static void hc_draw(void) {
  gfx_rect(0, 0, SB_W, SB_H, 0xff000000u);
  top_bar(HC.title, 2, HC.scroll.pos);
  float top = phone_free_top() + 86, x = PAD, gap = 12;
  const char *g = HC.genre[0] ? T(HC.genre) : T("All genres");
  const char *yr = HC.year == 1 ? T("Classic") : HC.year == 2020 ? "2020–2023" : HC.year ? (HC.year == 2026 ? "2026" : HC.year == 2025 ? "2025" : "2024") : T("All years");
  const char *so = T(hc_sorts[HC.sort]);
  x += chip(x, top, g, HC.menu == 0 || HC.genre[0], hc_filter_act, 0, 0) + gap;
  x += chip(x, top, yr, HC.menu == 1 || HC.year, hc_filter_act, 1, 0) + gap;
  chip(x, top, so, HC.menu == 2 || HC.sort, hc_filter_act, 2, 0);
  float gridY = top + 76;
  if (HC.menu >= 0) {
    int n = hc_count(HC.menu); float rowH = 54, panelH = n * rowH + 16, px = PAD, py = gridY;
    float pw = SB_W - 2 * PAD;
    ph_glass(px, py, pw, panelH, 22, RGBA(12, 12, 14, 246)); gfx_ring(px, py, pw, panelH, 22, 1, WHITE_A(28));
    for (int i = 0; i < n; i++) { float ry = py + 8 + i * rowH; if (ph_pressed(px + 8, ry, pw - 16, rowH)) gfx_rrect(px + 8, ry, pw - 16, rowH, 14, WHITE_A(18)); text_draw(hc_label(HC.menu, i), px + 24, ry + rowH / 2 - text_line_height(F_BODY) / 2, F_BODY, 600, TXT, 0, pw - 48, ALIGN_START); hit_act(px + 8, ry, pw - 16, rowH, hc_option_act, i, 0); }
    gridY += panelH + 16;
  }
  float bottom = SB_H - INS_B - BAR_H - 24;
  ph_drag(0, gridY, SB_W, bottom - gridY, &HC.scroll, 1); scroll_tick(&HC.scroll);
  float gy = gridY + 12 - HC.scroll.pos;
  float gh = poster_grid_act(&HC.shown, gy, hc_focus, hc_tap, 0, HC.focus);
  HC.scroll.max = fmaxf(0, gy + gh + HC.scroll.pos - bottom + 24);
  if (!HC.busy && HC.more && !HC.pageBusy && app_now() >= HC.pageRetryAt && HC.scroll.pos >= fmaxf(0, HC.scroll.max - 520)) {
    HC.pageBusy = 1; sb_catalog_page(&HC.query, HC.skip, hc_page_got, (void *)(uintptr_t)HC.gen);
  }
  if (HC.busy || HC.pageBusy) spinner(SB_W / 2, HC.pageBusy ? bottom - 38 : gridY + 70, 20);
  else if (!HC.shown.n) text_draw(T("None found"), SB_W / 2, gridY + 70, F_BODY, 500, TXT2, 0, SB_W - 2 * PAD, ALIGN_CENTER);
}
static Screen hcLayer = { NULL, NULL, hc_key, hc_tick, hc_draw, hc_focused, hc_open_item, NULL, NULL, NULL };
void ph_home_category_open(int row) {
  Board *b = me(); if (!b || row < 0 || row >= b->n) return;
  HC.base = b->rows[row].def; snprintf(HC.title, sizeof HC.title, "%s", T(HC.base.title));
  HC.genre[0] = 0; if (home_has(HC.base.title, "anime")) snprintf(HC.genre, sizeof HC.genre, "Animation");
  HC.year = 0; HC.sort = 0; HC.menu = -1; HC.focus = 0; HC.scroll.pos = HC.scroll.vel = HC.scroll.max = 0;
  phone_keyboard(0); hc_query(); app_overlay = &hcLayer;
}
Screen phHome = { enter, leave, key, tick, draw, focused, NULL, NULL, origin };
/* the TV's boards' hooks the core calls when the library changed */
void board_invalidate(void) { B[0].lib = B[1].lib = ~0u; }
/* the row a card is in (live TV: the channels beside the one played, for the next and the previous) */
int board_row_items(const char *id, const Item **items) { Board *b = me(); *items = NULL;
  { const Item *bi; int n = ph_live_band(&bi); for (int i = 0; i < n; i++) if (!strcmp(bi[i].id, id)) { *items = bi; return n; } } /* a channel of the Live TV row: that row is its line-up */
  for (int r = 0; r < b->n; r++) for (int i = 0; i < b->rows[r].list.n; i++) if (!strcmp(b->rows[r].list.items[i].id, id)) { *items = b->rows[r].list.items; return b->rows[r].list.n; }
  return 0; }
void board_rebuild(void) { lib_rows(me()); }
void board_refresh_quiet(void) { board_rebuild(); }
void board_from_bar(void) {}
void lib_from_bar(void) {}
void tabs_warm_tick(void) {}
/* the splash waits for Home's first rows; the screensaver and the background trailer ask the boards */
int board_home_ready(void) { Board *b = &B[0]; for (int i = 0; i < b->n; i++) if (b->rows[i].got) return 1; return b->n == 0; }
int board_spot_up(void) { return 0; }
int board_saver_items(Item *out, int max) { Board *b = &B[0]; int n = 0; for (int i = 0; i < b->n && n < max; i++) for (int k = 0; k < b->rows[i].list.n && n < max && k < 8; k++) out[n++] = b->rows[i].list.items[k]; return n; }
/* titles from Home's first poster rows (Discover's when Home has none) and the first row's name: the guide's and
   Settings' pictures */
int board_sample(Item *out, int max, char *rowTitle, int cap) { for (int d = 0; d < 2; d++) { Board *b = &B[d]; for (int i = 0; i < b->n; i++) if (b->rows[i].list.n > 0) {
      int n = 0; for (int k = 0; k < b->rows[i].list.n && n < max; k++) out[n++] = b->rows[i].list.items[k]; if (rowTitle && cap > 0) snprintf(rowTitle, cap, "%s", T(b->rows[i].def.title)); return n; } } return 0; }
