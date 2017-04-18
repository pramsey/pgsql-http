%define _topdir %(echo "`pwd`")

#Get PostgreSQL version from environment, otherwise - 96
#Package explicitly requires postgresql packages with such version
%define __pg_version    %(echo ${PG_VERSION:-96})
%define __pg_version2   %(PG_VERSION=${PG_VERSION:-96}; echo "${PG_VERSION:0:1}.${PG_VERSION:1}")
%define __pg_engine     /usr/pgsql-%{__pg_version2}/bin

Name:           postgresql%{__pg_version}-contrib-http
Version:        1.1
Release:        1%{?dist}
Summary:        http extension for PostgreSQL %{__pg_version2} 

License:        MIT

Source0:        https://github.com/pramsey/pgsql-http/archive/master.zip

BuildArch:      x86_64

Requires:       postgresql%{__pg_version}-server
Requires:       libcurl >= 0.7.20
BuildRequires:  postgresql%{__pg_version}-devel
BuildRequires:  libcurl-devel >= 0.7.20
BuildRequires:  unzip
BuildRequires:  gcc

%description
http extention for PostgreSQL %{__pg_version}
Contains just libs
To add extention do 'CREATE EXTENTION http;' within your database

# Uncomment to disable debug package building
#%global _enable_debug_package 0
#%global debug_package %{nil}

%prep
%setup -c

%build
PATH=$PATH:%{__pg_engine} make

#Copy-paste from 'make install' command
#TODO: use PREFIX parameter from PG Makefile
%install
pwd
mkdir -p %{buildroot}/usr/pgsql-%{__pg_version2}/lib
mkdir -p %{buildroot}/usr/pgsql-%{__pg_version2}/share/extension
mkdir -p %{buildroot}/usr/pgsql-%{__pg_version2}/share/extension
install -c -m 755 http.so %{buildroot}/usr/pgsql-%{__pg_version2}/lib/http.so
install -c -m 644 http.control %{buildroot}/usr/pgsql-%{__pg_version2}/share/extension/
install -c -m 644 http--1.1.sql http--1.0--1.1.sql %{buildroot}/usr/pgsql-%{__pg_version2}/share/extension/

%files
%defattr (750, postgres, postgres, 644)
/usr/pgsql-%{__pg_version2}/lib/http.so
/usr/pgsql-%{__pg_version2}/share/extension/http.control
/usr/pgsql-%{__pg_version2}/share/extension/http--1.1.sql
/usr/pgsql-%{__pg_version2}/share/extension/http--1.0--1.1.sql
