%include %{_sourcedir}/defines.inc

%global __provides_exclude_from ^%{_datadir}/%{name}/lib/.*$
%global __requires_exclude ^libsqlcipher\.so.*$

%define _cmake_skip_rpath %{nil}

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
%cmake -GNinja \
       -DCMAKE_BUILD_TYPE=%{_flutter_build_type} \
       -DPSDK_VERSION=%{_flutter_psdk_version} \
       -DPSDK_MAJOR=%{_flutter_psdk_major} \
       -DFLUTTER_PROJECT_NAME=%{name} \
       -DFLUTTER_ORG_NAME=%{orgName}
%ninja_build

%install
%ninja_install

%files
%{_bindir}/%{name}
%{_datadir}/%{name}/*
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
