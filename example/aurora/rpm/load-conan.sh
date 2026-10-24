#!/bin/bash

if [[ -n "${SAILFISH_SDK_SRC1_MOUNT_POINT}" ]]; then
    WORKSPACE_DIR=${SAILFISH_SDK_SRC1_MOUNT_POINT}
elif [[ "$(basename "${AURORA_SDK}" )" = "aurora_psdk" ]]; then
    WORKSPACE_DIR="${HOME}"
elif [[ "${PWD}" == "/workspace/"* ]]; then
    WORKSPACE_DIR="/workspace"
fi

script_dir=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
if [[ -z "${WORKSPACE_DIR:-}" && "${script_dir}" == "/workspace/"* ]]; then
    WORKSPACE_DIR="/workspace"
elif [[ -z "${WORKSPACE_DIR:-}" ]]; then
    echo %{error: unknown environment}
fi
script_dir=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
config_file="$script_dir"/version.config

if [[ -f "$config_file" ]]; then
    source "$config_file"
    if [[ -z "$conan_version" ]]; then
        echo %{error: Missing conan_version in rpm/"$config_file"}
        exit 1
    fi
else
    echo %{error: No rpm/"$config_file" file}
    exit 1
fi

target="$(uname -m)"
if [[ "$target" == "armv7l" ]]; then
    target="armv7hl"
fi

aurora_sdk_version=$(grep VERSION_ID /etc/os-release | cut -d '=' -f2 | tr -d '"')
aurora_sdk_major_version=$(echo "$aurora_sdk_version" | cut -d '.' -f1)

archive="conan.tar.gz"
download_url="https://conan.omp.ru/distrib/conan/${conan_version}/${aurora_sdk_major_version}/conan-${conan_version}-${target}.tar.gz"
conan_distribution_directory=${WORKSPACE_DIR}/.cache/AuroraTools/"${aurora_sdk_major_version}"/conan/"${conan_version}"/$(uname -m)
conan_rpm_macros=${conan_distribution_directory}/rpm-macros
archive_path="${conan_distribution_directory}/${archive}"

if [ ! -f "${conan_rpm_macros}" ]; then
    echo %{echo:Downloading Conan distribution}
    mkdir -p  "${conan_distribution_directory}"
    curl -s -o "$archive_path" "$download_url" || {
        echo %{error:Failed to download conan archive}
    }
    tar -xf "$archive_path" -C "$conan_distribution_directory" || {
        echo %{error:Failed to exctract conan acrhive}
    }
   rm -f "${archive_path}"
fi

echo "%define __workspace_dir ${WORKSPACE_DIR}"
echo "%define __conan_version ${conan_version}"
echo %{load:${conan_rpm_macros}}
