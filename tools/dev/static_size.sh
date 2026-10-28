#!/usr/bin/env bash
# Fails if the ELF's .text+.data+.bss exceeds the AGENTS.md section 3 static budget (4 MiB).
# Usage: tools/dev/static_size.sh [toontown3ds.elf]   (needs arm-none-eabi-size on PATH)
set -eu
elf="${1:-toontown3ds.elf}"
limit=$((4 * 1024 * 1024))
read -r text data bss _ < <(arm-none-eabi-size "$elf" | awk 'NR==2{print $1,$2,$3}')
total=$((text + data + bss))
printf 'static: text %d data %d bss %d = %d B (%.2f MiB of 4.00, headroom %d B)\n' \
    "$text" "$data" "$bss" "$total" "$(awk -v t=$total 'BEGIN{print t/1048576}')" $((limit - total))
[ "$total" -le "$limit" ] || { echo "FAIL: static footprint over 4 MiB" >&2; exit 1; }
