TARGETS := wp85

.PHONY: all $(TARGETS)
all: $(TARGETS)

$(TARGETS):
	export TARGET=$@ ; \
	export MANGOH_ROOT=$(shell pwd)/../.. ; \
	mkapp -v -t $@ \
		-i "$(LEGATO_ROOT)/interfaces/supervisor" \
		-i "$(LEGATO_ROOT)/interfaces/airVantage" \
		dataRouter.adef

clean:
	rm -rf _build_* *.wp85 *.wp85.update
