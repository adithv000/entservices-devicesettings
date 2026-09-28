#!/bin/sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "${SCRIPT_DIR}"

OUT_DIR="${1:-/tmp/devicesettings-coverage}"
GCDA_DIR="/tmp/gcov"
GCNO_DIRS="/usr/lib/gcov/entservices-devicesettings"
WORK_DIR="${OUT_DIR}/gcov-work"

mkdir -p "${OUT_DIR}"

if [ ! -d "${GCDA_DIR}" ] || ! find "${GCDA_DIR}" -type f -name '*.gcda' | grep -q .; then
    echo "No gcda data found under ${GCDA_DIR}. Run tests first and restart wpeframework to flush coverage." >&2
    exit 1
fi

has_component_gcda() {
    find "${GCDA_DIR}" -type f -name '*.gcda' | grep -Eq '/entservices-devicesettings/'
}

count_gcda() {
    tag="$1"
    find "${GCDA_DIR}" -type f -name '*.gcda' 2>/dev/null | grep -E "/${tag}/" | wc -l
}

if ! has_component_gcda; then
    echo "No DeviceSettings gcda files found under ${GCDA_DIR}." >&2
    exit 1
fi

PLUGIN_GCDA_COUNT="$(count_gcda entservices-devicesettings)"
echo "Detected gcda counts: plugin=${PLUGIN_GCDA_COUNT}"

# Sync .gcno files alongside .gcda files so lcov can locate notes files
echo "Syncing .gcno files into gcda tree ..."
for d in ${GCNO_DIRS}; do
    if [ -d "${d}" ]; then
        ( cd "${d}" && find . -type f -name '*.gcno' | while IFS= read -r f; do
            dest="${GCDA_DIR}/${f#./}"
            mkdir -p "$(dirname "${dest}")"
            cp -f "${f}" "${dest}"
        done )
    fi
done

# Build a component-local gcov workspace to avoid processing stale/foreign gcda
# from other plugins (which causes notes/stamp mismatch warnings).
rm -rf "${WORK_DIR}"
mkdir -p "${WORK_DIR}"

find "${GCDA_DIR}" -type f \( -name '*.gcda' -o -name '*.gcno' \) | while IFS= read -r f; do
    case "${f}" in
        *"/entservices-devicesettings/"*)
            rel="${f#${GCDA_DIR}/}"
            mkdir -p "${WORK_DIR}/$(dirname "${rel}")"
            cp -f "${f}" "${WORK_DIR}/${rel}"
            ;;
    esac
done

if ! find "${WORK_DIR}" -type f -name '*.gcda' | grep -q .; then
    echo "No DeviceSettings gcda files found under ${GCDA_DIR}. Run tests first." >&2
    exit 1
fi

BASE_INFO="${OUT_DIR}/base.info"
RUN_INFO="${OUT_DIR}/run.info"
TOTAL_INFO="${OUT_DIR}/coverage.info"
XML_FILE="${OUT_DIR}/coverage.xml"
PLUGIN_INFO="${OUT_DIR}/plugin.info"

lcov --gcov-tool /usr/bin/gcov --capture --initial --directory "${WORK_DIR}" --output-file "${BASE_INFO}" --ignore-errors source || true
lcov --gcov-tool /usr/bin/gcov --capture --directory "${WORK_DIR}" --output-file "${RUN_INFO}" --ignore-errors source

if ! grep -q '^SF:' "${RUN_INFO}"; then
    echo "No valid runtime coverage records found in ${RUN_INFO}." >&2
    echo "Hint: clear /tmp/gcov, rerun tests, then rerun this script." >&2
    exit 1
fi
lcov -a "${BASE_INFO}" -a "${RUN_INFO}" -o "${OUT_DIR}/combined.info" --ignore-errors source

# Filter to ONLY the DeviceSettings plugin sources (both .cpp and .h).
# lcov --extract keeps a record when its SF path matches ANY pattern below.
# 'set -f' keeps the '*' patterns literal so they are passed to lcov instead of
# being expanded by the shell against the current directory.
set -f
COVERAGE_FILE_PATTERNS="\
*/entservices-devicesettings/plugin/*.cpp \
*/entservices-devicesettings/plugin/*.h"

lcov --extract "${OUT_DIR}/combined.info" ${COVERAGE_FILE_PATTERNS} \
    --output-file "${TOTAL_INFO}" --ignore-errors source || true
set +f

if ! grep -q '^SF:' "${TOTAL_INFO}"; then
    echo "No coverage records found for the requested files in ${OUT_DIR}/combined.info." >&2
    exit 1
fi

# Remap SF paths to the actual source tree so genhtml can embed source.
# Empty/"EOF" source in the HTML report means genhtml could not find the files
# at the SF: paths. Provide the exact built source tree (the recipe's
# ${S}/git dir, or an installed debug-source dir) that CONTAINS 'plugin/' via:
#   - 2nd positional arg:  DeviceSettings-gcov-report.sh <out_dir> <src_root>
#   - or env var:          SRC_ROOT=/path/to/.../git
HTML_INFO="${TOTAL_INFO}"
SRC_ROOT="${SRC_ROOT:-$2}"
if [ -z "${SRC_ROOT}" ]; then
    SRC_ROOT="$(find /usr/src/debug -type d -path '*/entservices-devicesettings/*/git' 2>/dev/null | head -n 1)"
fi
SF_PREFIX="$(grep '^SF:' "${TOTAL_INFO}" | head -n 1 | sed -e 's#^SF:##' -e 's#/plugin/.*##')"
if [ -n "${SRC_ROOT}" ] && [ -n "${SF_PREFIX}" ]; then
    HTML_INFO="${OUT_DIR}/coverage.html.info"
    sed "s|${SF_PREFIX}|${SRC_ROOT}|g" "${TOTAL_INFO}" | sed 's#/git/git/#/git/#g' > "${HTML_INFO}"
fi

# Probe: warn early if the source is not readable (prevents a silent EOF report).
_probe="$(grep -m1 '^SF:' "${HTML_INFO}" | sed 's/^SF://')"
if [ -n "${_probe}" ] && [ ! -f "${_probe}" ]; then
    echo "WARNING: source not found at '${_probe}'." >&2
    echo "         The HTML report will show empty/EOF source. Re-run with the" >&2
    echo "         exact built source tree that contains 'plugin/':" >&2
    echo "         DeviceSettings-gcov-report.sh ${OUT_DIR} /path/to/entservices-devicesettings/.../git" >&2
fi

# Render HTML from the real (remapped) source paths so genhtml can embed source.
# Use --prefix to keep displayed paths short. Do NOT rewrite SF to a relative
# path here: genhtml would then resolve it against the CWD and show empty/EOF.
GENHTML_PREFIX="$(grep -m1 '^SF:' "${HTML_INFO}" | sed -e 's#^SF:##' -e 's#/plugin/.*##')"
if [ -n "${GENHTML_PREFIX}" ]; then
    genhtml "${HTML_INFO}" --output-directory "${OUT_DIR}/html" --prefix "${GENHTML_PREFIX}" --ignore-errors source
else
    genhtml "${HTML_INFO}" --output-directory "${OUT_DIR}/html" --ignore-errors source
fi

# Generate Cobertura XML for CI/reporting tools when gcovr is available.
if command -v gcovr >/dev/null 2>&1; then
    GCOVR_ROOT="${SOURCE_ROOT:-/}"
    if ! gcovr \
        --gcov-executable /usr/bin/gcov \
        --object-directory "${WORK_DIR}" \
        --root "${GCOVR_ROOT}" \
        --filter '.*/entservices-devicesettings/plugin/.*\.(cpp|h)$' \
        --xml-pretty \
        --output "${XML_FILE}"; then
        echo "Warning: gcovr failed to generate XML report." >&2
    fi
else
    echo "Warning: gcovr not found. Install python3-gcovr to generate XML report." >&2
fi

echo ""
echo "Coverage info (plugin): ${TOTAL_INFO}"
echo ""
echo "HTML report: ${OUT_DIR}/html/index.html"
echo ""
if [ -f "${XML_FILE}" ]; then
    echo "XML report: ${XML_FILE}"
    echo ""
fi

# Print per-component summary for quick tracking.
# TOTAL_INFO is already restricted to the requested files, so reuse it directly.
cp -f "${TOTAL_INFO}" "${PLUGIN_INFO}"

echo "Component summary:"
if grep -q '^SF:' "${PLUGIN_INFO}"; then
    echo "- Plugin"
    lcov --summary "${PLUGIN_INFO}"
else
    echo "- Plugin: no data"
fi

echo "Combined summary:"
lcov --summary "${TOTAL_INFO}"
