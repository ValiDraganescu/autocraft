# `make balance` prints the leveling tracking database (docs/leveling.md,
# "Tracking") for the latest balance signature, with the sqlite3 that ships
# with macOS (3.51: `sqrt` and `group_concat(... ORDER BY ...)`, which the views
# use). `VIEW=` picks another view (level_funnel, pick_paths, drive_share) and
# `DB=` another file. The core's tests are `make -C unreal/core-tests`.

DB ?= $(HOME)/Library/Application Support/Autocraft/Unreal/tracking.sqlite
VIEW ?= perk_balance
SQLITE3 := /usr/bin/sqlite3

.PHONY: balance
balance:
	@if [ ! -f "$(DB)" ]; then echo "no tracking database at $(DB)"; exit 1; fi
	@sig=$$($(SQLITE3) -cmd ".timeout 3000" "$(DB)" "SELECT signature FROM games ORDER BY ended DESC LIMIT 1"); \
	if [ -z "$$sig" ]; then echo "no games in $(DB)"; exit 0; fi; \
	echo "$(VIEW), balance signature $$sig ($(DB))"; \
	if [ "$(VIEW)" = perk_balance ]; then \
	  $(SQLITE3) -cmd ".timeout 3000" -box "$(DB)" "SELECT kind, level, perk, offered, took, picked, \
	    CAST(ROUND(pick_rate * 100) AS INTEGER) || '%' AS pick_rate, games, wins, \
	    CAST(ROUND(win_rate * 100) AS INTEGER) || '%' AS win_rate, \
	    CAST(ROUND(margin * 100) AS INTEGER) || '%' AS margin, other_games, other_wins, \
	    CAST(ROUND(other_win_rate * 100) AS INTEGER) || '%' AS other_win_rate, \
	    CAST(ROUND(gap * 100) AS INTEGER) || '%' AS gap \
	    FROM perk_balance WHERE signature = '$$sig' ORDER BY kind, level, perk"; \
	else \
	  $(SQLITE3) -cmd ".timeout 3000" -box "$(DB)" "SELECT * FROM $(VIEW) WHERE signature = '$$sig'"; \
	fi
