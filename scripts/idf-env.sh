# This script should be sourced, not executed.

if [ -n "${BASH_SOURCE-}" ] && [ "${BASH_SOURCE[0]}" = "$0" ]; then
    echo "This script should be sourced:"
    echo ". ${BASH_SOURCE[0]}"
    exit 1
fi

if [ -n "${BASH_SOURCE-}" ]; then
    _idf_env_script_path="${BASH_SOURCE[0]}"
elif [ -n "${ZSH_VERSION-}" ]; then
    _idf_env_script_path="${(%):-%x}"
else
    _idf_env_script_path="$0"
fi

_idf_env_script_dir=$(
    CDPATH= cd -- "$(dirname -- "${_idf_env_script_path}")" >/dev/null 2>&1 && pwd
)
_idf_env_project_root=$(
    CDPATH= cd -- "${_idf_env_script_dir}/.." >/dev/null 2>&1 && pwd
)

if [ -n "${ECP32_S3_SUPPORT_ROOT-}" ]; then
    _idf_env_support_root="${ECP32_S3_SUPPORT_ROOT}"
else
    _idf_env_support_root="${HOME}/Dev/ECP32-S3-support"
fi

_idf_env_support_root=$(
    CDPATH= cd -- "${_idf_env_support_root}" >/dev/null 2>&1 && pwd
)

export IDF_PATH="${_idf_env_support_root}/esp-idf-v5.5.1"
export IDF_TOOLS_PATH="${IDF_PATH}/.espressif"
export PIP_CACHE_DIR="${_idf_env_project_root}/scripts/.cache/pip"

unset _idf_env_script_path
unset _idf_env_script_dir
unset _idf_env_project_root
unset _idf_env_support_root

. "${IDF_PATH}/export.sh"
