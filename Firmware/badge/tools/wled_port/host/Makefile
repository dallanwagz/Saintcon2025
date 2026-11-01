# Host build of components/wled_fx (the exact sources the badge compiles) with ASan + UBSan.
#   make            -> out/wledsim
#   make sweep      -> manifest check + every effect x 6 palettes x vlen {20,150} x 1000 frames
#   make golden     -> (re)write golden/ frames for fx 65, 67, 101
#   make check      -> compare against golden/
C       := ../../../components/wled_fx
CXX     := clang++
# ponytail: two UBSan checks are off because upstream WLED trips them by design and stock WLED runs the
# same code on ESP32: shift-base (left shift of negative ints, fastled_slim; well defined in C++20/GCC) and
# float-cast-overflow (negative float -> unsigned in Starburst, PacMan and the audio fx on simulateSound;
# FX.cpp 3709/3710/6844/6855/6856/6866/6877/7023/7277/7280/7322/7359/7414/7518 in v16.0.1).
SAN     := -fsanitize=address,undefined -fno-sanitize=shift-base,float-cast-overflow -fno-sanitize-recover=all -fno-omit-frame-pointer
DEFS    := -DWLED_DISABLE_2D -DWLED_DISABLE_PARTICLESYSTEM2D -DWLED_DISABLE_PARTICLESYSTEM1D \
           -DWLED_PS_DONT_REPLACE_FX -DBOARD_HAS_PSRAM
CXXFLAGS := -std=gnu++17 -O1 -g $(SAN) $(DEFS) -I$(C)/include -I$(C)/shim -I$(C)/gen -I$(C)/community -I$(C)/upstream/wled00 -w
SRCS    := $(C)/upstream/wled00/FX.cpp $(C)/upstream/wled00/palettes.cpp $(C)/upstream/wled00/wled_math.cpp \
           $(C)/upstream/wled00/src/dependencies/fastled_slim/fastled_slim.cpp \
           $(C)/gen/carved_FX_fcn.cpp $(C)/gen/carved_util.cpp $(C)/gen/colors.cpp \
           $(C)/shim/wled_fx_rt.cpp $(C)/src/wled_fx_api.cpp wledsim.cpp $(wildcard $(C)/community/*.cpp)
OBJS    := $(patsubst %.cpp,out/obj/%.o,$(notdir $(SRCS)))
HDRS    := $(wildcard $(C)/include/*.h $(C)/shim/*.h $(C)/gen/*.h $(C)/community/*.h $(C)/upstream/wled00/*.h)
GOLDEN  := 65 67 101
export ASAN_OPTIONS := detect_leaks=0:abort_on_error=1
export UBSAN_OPTIONS := print_stacktrace=1:halt_on_error=1

out/wledsim: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

vpath %.cpp $(sort $(dir $(SRCS)))
out/obj/%.o: %.cpp $(HDRS) Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ONLY=<fx id> limits the sweep to one effect (wledport add uses it)
sweep: out/wledsim
	./out/wledsim --check-manifest $(C)/manifest.json
	./out/wledsim --sweep --frames 1000 $(if $(ONLY),--only $(ONLY))

# fixed host RNG seed + fixed timestamps -> byte-identical frames; vlen 20 and 150 each
golden: out/wledsim
	@mkdir -p golden
	@for f in $(GOLDEN); do for v in 20 150; do \
	  ./out/wledsim $$f --vlen $$v --frames 200 --seed 1 > golden/fx$$f-v$$v.txt || exit 1; done; done
	@echo "golden written: $$(ls golden | wc -l | tr -d ' ') files"

check: out/wledsim
	@mkdir -p out/golden
	@for f in $(GOLDEN); do for v in 20 150; do \
	  ./out/wledsim $$f --vlen $$v --frames 200 --seed 1 > out/golden/fx$$f-v$$v.txt || exit 1; done; done
	@diff -r golden out/golden && echo "golden check OK: $$(ls golden | wc -l | tr -d ' ') files identical"

clean: ; rm -rf out
.PHONY: sweep golden check clean
