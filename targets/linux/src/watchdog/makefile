CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -g

BINDIR  = ../../../../build/linux_sim

WATCHDOG_SRC    = main.c

WATCHDOG_BIN    = $(BINDIR)/watchdog
MAIN_BIN		= $(BINDIR)/linux_sim

.PHONY: all clean run

all: $(BINDIR) $(WATCHDOG_BIN)

build: $(WATCHDOG_BIN)

$(BINDIR):
	mkdir -p $(BINDIR)

$(WATCHDOG_BIN): $(WATCHDOG_SRC)
	$(CC) $(CFLAGS) -o $@ $<

run: all
	@echo ""
	@echo "=============================================="
	@echo " Starting watchdog demo"
	@echo " -> Watchdog timeout : $(WDT_TIMEOUT_SEC)s (set in watchdog.h)"
	@echo " -> Run 1: App hangs after 8s, watchdog resets it"
	@echo " -> Run 2: App kicks normally, exits cleanly"
	@echo "=============================================="
	@echo ""
	@rm -f /tmp/wdt_demo_run_count
	$(WATCHDOG_BIN) $(MAIN_BIN)

clean:
	rm -rf $(BINDIR) /tmp/wdt_demo_run_count