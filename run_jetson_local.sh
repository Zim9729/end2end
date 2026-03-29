#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="${repo_root}/build"
config_root="${repo_root}/config"
model_config_root="${config_root}/model_config"
test_data_root="${config_root}/test_data"
listen_port="${LISTEN_PORT:-9000}"
reply_port="${REPLY_PORT:-9001}"

serve_pid=""
worker_pid=""

cleanup() {
    local exit_code=$?
    if [[ -n "${worker_pid}" ]] && kill -0 "${worker_pid}" 2>/dev/null; then
        kill "${worker_pid}" 2>/dev/null || true
        wait "${worker_pid}" 2>/dev/null || true
    fi
    if [[ -n "${serve_pid}" ]] && kill -0 "${serve_pid}" 2>/dev/null; then
        kill "${serve_pid}" 2>/dev/null || true
        wait "${serve_pid}" 2>/dev/null || true
    fi
    exit "${exit_code}"
}

trap cleanup EXIT

require_file() {
    local path="$1"
    local label="$2"
    if [[ ! -f "${path}" ]]; then
        echo "Missing ${label}: ${path}" >&2
        exit 1
    fi
    realpath "${path}"
}

get_first_file() {
    local directory="$1"
    local pattern="$2"
    if [[ ! -d "${directory}" ]]; then
        return 0
    fi
    find "${directory}" -maxdepth 1 -type f -name "${pattern}" | LC_ALL=C sort | head -n 1
}

resolve_engine_path() {
    local engine_path="${ENGINE_FILE:-}"
    if [[ -n "${engine_path}" ]]; then
        require_file "${engine_path}" "engine"
        return 0
    fi
    engine_path="$(get_first_file "${model_config_root}" '*.engine')"
    if [[ -z "${engine_path}" ]]; then
        engine_path="$(get_first_file "${root}" '*.engine')"
    fi
    if [[ -z "${engine_path}" ]]; then
        echo "Engine file not found. Please place a .engine file under config/model_config or build." >&2
        exit 1
    fi
    realpath "${engine_path}"
}

resolve_source_request() {
    local request_path="${REQUEST_JSON:-}"
    if [[ -n "${request_path}" ]]; then
        require_file "${request_path}" "request json"
        return 0
    fi
    if [[ ! -d "${config_root}" ]]; then
        echo "Request JSON not found. Please place an input request JSON file under config." >&2
        exit 1
    fi
    request_path="$(find "${config_root}" -maxdepth 1 -type f -name '*.json' ! -name '*_result.json' | LC_ALL=C sort | head -n 1)"
    if [[ -z "${request_path}" ]]; then
        echo "Request JSON not found. Please place an input request JSON file under config." >&2
        exit 1
    fi
    realpath "${request_path}"
}

resolve_relative_asset_source() {
    local source_request_path="$1"
    local relative_path="$2"
    local source_request_dir
    source_request_dir="$(dirname "${source_request_path}")"
    local candidates=(
        "${source_request_dir}/${relative_path}"
        "${test_data_root}/${relative_path}"
        "${test_data_root}/$(basename "${relative_path}")"
    )
    local candidate
    for candidate in "${candidates[@]}"; do
        if [[ -f "${candidate}" ]]; then
            realpath "${candidate}"
            return 0
        fi
    done
    return 1
}

get_running_end2end_pids() {
    local executable_path="$1"
    pgrep -f "${executable_path}" 2>/dev/null || true
}

get_udp_port_conflicts() {
    local output=""
    local port
    if ! command -v ss >/dev/null 2>&1; then
        return 0
    fi
    for port in "$@"; do
        local lines
        lines="$(ss -lunHp "sport = :${port}" 2>/dev/null || true)"
        if [[ -n "${lines}" ]]; then
            output+="Port ${port} is already in use:"$'\n'"${lines}"$'\n'
        fi
    done
    printf '%s' "${output}"
}

assert_clean_start() {
    local executable_path="$1"
    shift
    local ports=("$@")
    local messages=()
    local running_pids
    running_pids="$(get_running_end2end_pids "${executable_path}")"
    if [[ -n "${running_pids}" ]]; then
        messages+=("Existing end2end processes detected:\n${running_pids}")
    fi
    local port_conflicts
    port_conflicts="$(get_udp_port_conflicts "${ports[@]}")"
    if [[ -n "${port_conflicts}" ]]; then
        messages+=("Test UDP ports are already in use:\n${port_conflicts}")
    fi
    if (( ${#messages[@]} > 0 )); then
        printf '%b\n\n' "${messages[@]}" >&2
        echo "Please stop the old serve/worker processes or free these ports, then try again." >&2
        exit 1
    fi
}

exe="$(require_file "${root}/end2end" "app")"
client="$(require_file "${root}/udp_client_demo" "client")"
engine="$(resolve_engine_path)"
source_request="$(resolve_source_request)"
queue="${root}/queue"
shared="${root}/shared"
requests_inbox="${shared}/requests/inbox"
results_outbox="${shared}/results/outbox"
log_dir="${root}/run_logs"

run_id="$(date +%Y%m%d_%H%M%S)"
task_id="local-${run_id}"
request_relpath="requests/inbox/${task_id}.json"
result_relpath="results/outbox/${task_id}_result.json"
dst_req="${shared}/${request_relpath}"
dst_res="${shared}/${result_relpath}"
serve_log="${log_dir}/serve-${run_id}.log"
worker_log="${log_dir}/worker-${run_id}.log"

assert_clean_start "${exe}" "${listen_port}" "${reply_port}"

mkdir -p "${queue}" "${requests_inbox}" "${results_outbox}" "${log_dir}"
cp "${source_request}" "${dst_req}"

image_path="$(python3 - "${dst_req}" <<'PY'
import json
import os
import sys
with open(sys.argv[1], 'r', encoding='utf-8') as f:
    data = json.load(f)
value = str(data.get('imagePath', '') or '')
if value and not os.path.isabs(value):
    print(value)
PY
)"

if [[ -n "${image_path}" ]]; then
    if resolved_asset="$(resolve_relative_asset_source "${source_request}" "${image_path}")"; then
        dst_image="$(dirname "${dst_req}")/${image_path}"
        mkdir -p "$(dirname "${dst_image}")"
        cp "${resolved_asset}" "${dst_image}"
    fi
fi

echo "Engine: ${engine}"
echo "Request: ${source_request}"
echo "Shared request: ${dst_req}"
echo "Result file: ${dst_res}"
echo "Serve log: ${serve_log}"
echo "Worker log: ${worker_log}"

"${exe}" --serve "${listen_port}" "${queue}" "${shared}" >"${serve_log}" 2>&1 &
serve_pid="$!"
sleep 2

"${exe}" --worker "${engine}" "${queue}" "${shared}" >"${worker_log}" 2>&1 &
worker_pid="$!"
sleep 2

if ! "${client}" 127.0.0.1 "${listen_port}" 127.0.0.1 "${reply_port}" "${task_id}" "${request_relpath}" "${result_relpath}"; then
    echo "udp_client_demo failed. Check logs:" >&2
    echo "  ${serve_log}" >&2
    echo "  ${worker_log}" >&2
    exit 1
fi

deadline=$((SECONDS + 30))
while (( SECONDS < deadline )); do
    if [[ -f "${dst_res}" ]]; then
        echo "Result file detected: ${dst_res}"
        exit 0
    fi
    sleep 0.5
done

echo "No result file detected within 30 seconds. Check logs:" >&2
echo "  ${serve_log}" >&2
echo "  ${worker_log}" >&2
exit 1
