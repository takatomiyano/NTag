include config.gmk

##### Rules #####

.PHONY: all float dirs inc clean cleanobj

all: float dirs inc lib/libutillib.a
	@echo "[UtilLib] Done!"

float:
ifneq "$(origin USE_DOUBLE)" "undefined"
	$(info [UtilLib] Using double instead of float!)
endif

dirs:
	@mkdir -p lib include

inc:
	@cp `find src/ -name '*.hh'` include

include/%.hh:
	@:

SRCS = $(wildcard src/*.cc) $(wildcard src/*/*.cc) $(wildcard src/*/*/*.cc)
OBJS = $(patsubst src/%.cc, src/%.o, $(SRCS))
FORTRANOBJS = $(patsubst src/SKIO/%.F, src/SKIO/%.o, $(wildcard src/SKIO/*.F))
INC := $(addprefix -I , $(sort $(dir $(shell find src -name '*.hh'))))

$(OBJS): src/%.o: src/%.cc src/%.hh
	@echo "[UtilLib] Building utility library: $(word $(words $(subst /, , $*)), $(subst /, , $*))..."
	@$(CXX) $(CXXFLAGS) -o $@ -c $< $(INC) $(ROOTINCLUDE) $(SKOFLINCLUDE) $(ATMPDINCLUDE)

$(FORTRANOBJS): src/SKIO/%.o: src/SKIO/%.F
	@echo "[UtilLib] Building $*..."
	@$(FC) $(FCFLAGS) -c $< -o $@ -I $(SKOFL_ROOT)/inc -I $(ATMPD_ROOT)/inc -I $(SKOFL_ROOT)/lowe -I $(ATMPD_ROOT)/inc

lib/libutillib.a: $(OBJS) $(FORTRANOBJS)
	@echo "[UtilLib] Building static shared library..."
	@ar crf $@ $^
	
lib/libutillib_double.a: $(OBJS) $(FORTRANOBJS)
	@echo "[UtilLib] Building static shared library (for double)..."
	@ar crf $@ $^

clean:
	@rm -rf src/*.o src/*/*.o src/*/*/*.o lib include
	
cleanobj:
	@rm -rf src/*.o src/*/*.o src/*/*/*.o

double: CXXFLAGS+=-DUSE_DOUBLE=1
double: cleanobj dirs inc lib/libutillib_double.a