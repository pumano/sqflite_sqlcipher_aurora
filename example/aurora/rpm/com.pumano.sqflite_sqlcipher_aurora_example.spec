%include %{_sourcedir}/defines.inc

%global __provides_exclude_from ^%{_datadir}/%{name}/lib/.*$
%global __requires_exclude ^lib(sqlcipher|ssl|crypto)\.so.*$

%define _cmake_skip_rpath %{nil}
%{expand:%(bash %{_sourcedir}/load-conan.sh)}

Name: %{orgName}.%{appName}%{?flavor}
Summary: %{summary}
Version: %{appVersion}
Release: 1
License: %{license}
Source0: %{name}-%{version}.tar.zst

%requires
%dnl Place to add custom BuildRequires.

%description
%{summary}.

%prep
%autosetup

%build
# conan-with-aurora-profile (run by %%conan_install) derives the target
# architecture from $PKG_CONFIG_SYSROOT_DIR. On native builds (aarch64)
# that variable is unset and the script falls back to x86_64, fetching
# binaries of the wrong architecture. Point it at the target sysroot for
# the conan step and restore the original value before the CMake step.
conan_sysroot_save=${PKG_CONFIG_SYSROOT_DIR}
export PKG_CONFIG_SYSROOT_DIR=/opt/cross/%{_target_platform}-meego-linux-gnu
%conan_install
export PKG_CONFIG_SYSROOT_DIR=${conan_sysroot_save}
%conan_cmake -GNinja \
       -DCMAKE_BUILD_TYPE=%{_flutter_build_type} \
       -DPSDK_VERSION=%{_flutter_psdk_version} \
       -DPSDK_MAJOR=%{_flutter_psdk_major} \
       -DFLUTTER_PROJECT_NAME=%{name} \
       -DFLUTTER_ORG_NAME=%{orgName}
%ninja_build

%install
%ninja_install
%conan_deploy_libraries

%files
%{_bindir}/%{name}
%{_datadir}/%{name}/*
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
