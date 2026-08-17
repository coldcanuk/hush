Name:           hush-relay
Version:        0.1.0
Release:        1%{?dist}
Summary:        Legible C11 Nostr relay core
License:        GPLv3
URL:            https://github.com/coldcanuk/hush
BuildArch:      x86_64

%description
Hush is a lightweight, legible C11 implementation of core Nostr relay
functionality. This package contains the hush-relay binary.

%install
mkdir -p %{buildroot}%{_bindir}
install -m 0755 %{_sourcedir}/hush-relay %{buildroot}%{_bindir}/hush-relay
mkdir -p %{buildroot}%{_docdir}/hush-relay
install -m 0644 %{_sourcedir}/README.md %{buildroot}%{_docdir}/hush-relay/ || true

%files
%{_bindir}/hush-relay
%doc %{_docdir}/hush-relay/*

%changelog
* Mon Aug 17 2026 Hush Maintainers <maintainers@example.invalid> - 0.1.0-1
- Initial packaging for GitHub releases (DEB + RPM support)
