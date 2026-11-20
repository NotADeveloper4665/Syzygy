#!/usr/bin/env bash
set -Eeuo pipefail

duration_seconds="${1:-300}"
output_dir="${2:-$HOME/syzygy-t1600-apd-$(date +%Y%m%d-%H%M%S)}"

if [[ ! "$duration_seconds" =~ ^[1-9][0-9]*$ ]]; then
  echo "Usage: bash $0 [duration_seconds] [output_directory]" >&2
  exit 2
fi

mkdir -p "$output_dir"
output_dir="$(cd "$output_dir" && pwd)"
archive_path="${output_dir}.tar.gz"
pids=()
finished=0

cleanup() {
  for pid in "${pids[@]}"; do
    kill "$pid" 2>/dev/null || true
  done
  for pid in "${pids[@]}"; do
    wait "$pid" 2>/dev/null || true
  done
}

finish() {
  [[ "$finished" -eq 1 ]] && return
  finished=1
  cleanup
  tar -czf "$archive_path" -C "$(dirname "$output_dir")" "$(basename "$output_dir")"
  echo "Telemetry bundle: $archive_path"
}

trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

{
  echo "collected_at=$(date --iso-8601=seconds)"
  echo "hostname=$(hostname)"
  echo "duration_seconds=$duration_seconds"
  echo
  uname -a
  echo
  if command -v syzygy >/dev/null 2>&1; then
    syzygy --version 2>&1 || true
  else
    echo "syzygy executable not found in PATH"
  fi
  echo
  rpm -q syzygy 2>&1 || true
  echo
  if command -v nvidia-smi >/dev/null 2>&1; then
    nvidia-smi -q 2>&1 || true
  else
    echo "nvidia-smi unavailable"
  fi
  echo
  if command -v lspci >/dev/null 2>&1; then
    lspci -nnk 2>&1 || true
  fi
  echo
  if command -v loginctl >/dev/null 2>&1; then
    loginctl list-sessions 2>&1 || true
  fi
} > "$output_dir/host-info.txt"

if command -v nvidia-smi >/dev/null 2>&1; then
  nvidia-smi --query-gpu=timestamp,name,driver_version,utilization.gpu,utilization.memory,memory.used,temperature.gpu,power.draw,clocks.gr,clocks.mem --format=csv -l 1 \
    > "$output_dir/gpu.csv" 2>&1 &
  pids+=("$!")

  nvidia-smi pmon -s um -d 1 > "$output_dir/gpu-process.csv" 2>&1 &
  pids+=("$!")
fi

if command -v pidstat >/dev/null 2>&1; then
  pidstat -u -r -d 1 > "$output_dir/process.csv" 2>&1 &
  pids+=("$!")
else
  (
    while true; do
      date --iso-8601=seconds
      ps -eo pid,comm,%cpu,%mem,rss --sort=-%cpu | sed -n '1,16p'
      sleep 1
    done
  ) > "$output_dir/process.csv" 2>&1 &
  pids+=("$!")
fi

if command -v journalctl >/dev/null 2>&1; then
  journalctl --user -f -o short-iso > "$output_dir/user-journal.log" 2>&1 &
  pids+=("$!")
fi

syzygy_log="$HOME/.local/state/syzygy/service/service.log"
if [[ -f "$syzygy_log" ]]; then
  tail -n 0 -F "$syzygy_log" > "$output_dir/syzygy-service.log" 2>&1 &
  pids+=("$!")
else
  echo "No service log found at $syzygy_log" > "$output_dir/syzygy-service.log"
fi

echo "Collecting for $duration_seconds seconds."
echo "Start the target stream before or immediately after this collector."
echo "Output directory: $output_dir"
sleep "$duration_seconds"
