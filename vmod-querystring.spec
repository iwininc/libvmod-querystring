%global vmod    querystring
%global vmoddir %{_libdir}/varnish/vmods

Name:           vmod-%{vmod}
Version:        0.4
Release:        1%{?dist}
Group:          System Environment/Libraries
Summary:        QueryString module for Varnish Cache
URL:            https://www.varnish-cache.org/vmod/%{vmod}
License:        BSD

Source:         lib%{name}-%{version}.tar.gz

BuildRequires:  python
BuildRequires:  varnish >= 4
BuildRequires:  varnish-libs-devel >= 4

Requires:       varnish >= 4


%description
Varnish multipurpose module for URL query-string manipulation. Can be used to
normalize for instance request URLs or Location response headers in various
ways. It is recommended to at least clean incoming request URLs (removing empty
query-strings), all other functions do the cleaning.


%prep
%setup -qn lib%{name}-%{version}


%build
%configure --with-rst2man=true
make %{?_smp_mflags}


%install
%make_install


%check
make %{?_smp_mflags} check


%files
%{vmoddir}/libvmod_%{vmod}.so
%{_mandir}/man?/*
%{_docdir}/*


%changelog
* Sat Apr 02 2016 Dridi Boukelmoune <dridi.boukelmoune@gmail.com> - 0.5-1
- Stop removing the libtool archive

* Thu Feb 11 2016 Dridi Boukelmoune <dridi.boukelmoune@gmail.com> - 0.4-1
- Bump version
- Drop Varnish 3 source code requirement

* Tue Aug 04 2015 Dridi Boukelmoune <dridi.boukelmoune@gmail.com> - 0.3-1
- Bump version

* Mon Apr 15 2013 Dridi Boukelmoune <dridi.boukelmoune@gmail.com> - 0.2-1
- Initial version.
