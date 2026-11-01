// Host harness for components/wled_fx (same sources as the badge build, macOS clang + ASan/UBSan).
//   wledsim <fx> [pal [sx [ix [c1 [c2 [c3]]]]]] [--vlen N] [--frames N] [--ms 40] [--n 20]
//           [--col RRGGBB,RRGGBB,RRGGBB] [--seed S]     -> one line of hex pixels per frame
//   wledsim --list                                      -> available effects / palettes
//   wledsim --sweep [--frames N] [--only FX]            -> every fx (or one) x pal {0,6,13,49,51,55} x vlen {20,150}
//   wledsim --check-manifest <manifest.json>            -> registry vs. manifest "included" ids
// Unset numeric args take the effect's metadata defaults (wled_fx_defaults).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <set>
#include "wled_fx.h"
#include "wled_fx_rt.h"

static void render(const wled_params_t &p, int frames, int ms, int n, bool print, uint32_t t0 = 1000) {
  uint8_t px[64][3];
  wled_fx_apply(&p);
  for (int f = 0; f < frames; f++) {
    uint32_t t = t0 + f * ms;
    wled_fx_frame(t, px, n);
    if (!print) continue;
    printf("t=%6u ", (unsigned)t);
    for (int i = 0; i < n; i++) printf("%02x%02x%02x", px[i][0], px[i][1], px[i][2]);
    printf("\n");
  }
}

static int sweep(int frames, int only) {
  static const uint8_t pals[] = {0, 6, 13, 49, 51, 55};
  static const uint16_t vlens[] = {20, 150};
  int combos = 0;
  uint32_t t0 = 1000;
  for (uint16_t i = 0; i < wled_fx_count(); i++) {
    uint8_t fx = wled_fx_id_at(i);
    if (only >= 0 && fx != only) continue;
    for (uint8_t pal : pals)
      for (uint16_t vl : vlens) {
        wled_params_t p = {};
        wled_fx_defaults(fx, &p);
        p.pal = pal; p.vlen = vl;
        p.col[0][0] = 255; p.col[0][1] = 160; p.col[1][2] = 255; p.col[2][1] = 255;
        render(p, frames, 40, 20, false, t0);
        t0 += frames * 40 + 5000;  // keep time monotonic across combos, like the badge
        combos++;
      }
    fprintf(stderr, "\rsweep %3u/%u fx %3u %-24s", i + 1, wled_fx_count(), fx, wled_fx_name(fx));
  }
  fprintf(stderr, "\n");
  if (only >= 0 && !combos) { printf("sweep: fx %d not in the registry\n", only); return 1; }
  printf("sweep OK: %u effects x %zu palettes x %zu vlens = %d combos x %d frames\n",
         only >= 0 ? 1u : wled_fx_count(), sizeof pals, sizeof vlens / sizeof vlens[0], combos, frames);
  return 0;
}

static int check_manifest(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) { perror(path); return 2; }
  std::string s; char buf[4096]; size_t k;
  while ((k = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, k);
  fclose(f);
  // ponytail: tiny scan instead of a JSON parser: each effect object is {"id": N, ... "included": bool, ...}
  std::set<int> want;
  for (size_t pos = s.find("\"effects\""); (pos = s.find("\"id\": ", pos)) != std::string::npos; pos++) {
    int id = atoi(s.c_str() + pos + 6);
    size_t inc = s.find("\"included\": ", pos);
    if (inc != std::string::npos && !strncmp(s.c_str() + inc + 12, "true", 4)) want.insert(id);
  }
  int bad = 0;
  std::set<int> have;
  for (uint16_t i = 0; i < wled_fx_count(); i++) {
    uint8_t fx = wled_fx_id_at(i);
    have.insert(fx);
    const char *meta = wled_fx_meta(fx);
    size_t nl = strlen(wled_fx_name(fx));
    if (!meta || !strncmp(meta, "RSVD", 4) || strncmp(meta, wled_fx_name(fx), nl) || (meta[nl] && meta[nl] != '@')) {
      printf("fx %u: table name '%s' vs runtime meta '%s'\n", fx, wled_fx_name(fx), meta ? meta : "(null)");
      bad++;
    }
  }
  if (have != want) { printf("registry (%zu) != manifest included (%zu)\n", have.size(), want.size()); bad++; }
  printf("manifest check: %zu effects, %u palettes, %s\n", have.size(), wled_pal_count(), bad ? "FAIL" : "OK");
  return bad ? 1 : 0;
}

int main(int argc, char **argv) {
  int frames = 10, ms = 40, n = 20, pos = 0, vals[7], nvals = 0, vlen = 0, only = -1;
  uint32_t seed = 0;
  bool list = false, sw = false;
  const char *manifest = nullptr;
  uint8_t col[3][3] = {{255, 160, 0}, {0, 0, 0}, {0, 0, 0}};
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--frames")) frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--ms")) ms = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--n")) n = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--vlen")) vlen = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--seed")) seed = strtoul(argv[++i], nullptr, 0);
    else if (!strcmp(argv[i], "--list")) list = true;
    else if (!strcmp(argv[i], "--sweep")) sw = true;
    else if (!strcmp(argv[i], "--only")) only = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--check-manifest")) manifest = argv[++i];
    else if (!strcmp(argv[i], "--col")) {
      char *s = argv[++i];
      for (int k = 0; k < 3 && *s; k++) {
        uint32_t c = strtoul(s, &s, 16);
        col[k][0] = c >> 16; col[k][1] = c >> 8; col[k][2] = c;
        if (*s == ',') s++;
      }
    } else if (nvals < 7) vals[nvals++] = atoi(argv[i]), pos++;
  }
  if (n < 1 || n > 64) n = 20;
  wled_fx_rt_seed(seed);
  wled_fx_init();
  if (manifest) return check_manifest(manifest);
  if (list) {
    for (uint16_t i = 0; i < wled_fx_count(); i++) printf("fx  %3u %s\n", wled_fx_id_at(i), wled_fx_name(wled_fx_id_at(i)));
    for (int i = 0; i < wled_pal_count(); i++) printf("pal %3d %s\n", i, wled_pal_name(i));
    printf("# %u effects, %u palettes\n", wled_fx_count(), wled_pal_count());
    return 0;
  }
  if (sw) return sweep(frames == 10 ? 1000 : frames, only);
  if (!nvals) { fprintf(stderr, "usage: wledsim <fx> [pal sx ix c1 c2 c3] [--vlen N] [--frames N] | --list | --sweep\n"); return 2; }
  wled_params_t p = {};
  wled_fx_defaults(vals[0], &p);
  uint8_t *slot[] = {&p.fx, &p.pal, &p.sx, &p.ix, &p.c1, &p.c2, &p.c3};
  for (int k = 1; k < nvals; k++) *slot[k] = vals[k];
  memcpy(p.col, col, sizeof col);
  p.vlen = vlen;
  const char *name = wled_fx_name(p.fx);
  printf("# fx %u (%s) pal %u sx %u ix %u c1 %u c2 %u c3 %u vlen %u\n", p.fx, name ? name : "n/a", p.pal, p.sx, p.ix,
         p.c1, p.c2, p.c3, p.vlen);
  render(p, frames, ms, n, true);
  return 0;
}
