#!/bin/bash

mirror="http://mirror.koddos.net/gcc/releases"

function download() {
	local source_dir=$1;
	local compiler=$2;
	local version=$3;
	
	if [ -z "$compiler" ]; then
		echo "Please specify the compiler you want to download";
		return;
	fi;

	if [ -z "$version" ]; then
		echo "Please specify the version";
		return;
	fi;

	if [ -z "$source_dir" ]; then
		echo "Please specify the source directory";
		return;
	fi;

	wget -O "$source_dir/$compiler-$version.tar.gz" "${mirror}/gcc-${version}/gcc-$version.tar.gz";
}


function install() {
	local source_dir=$1;
	local compiler=$2;
	local version=$3;
	local install_dir=$4;

	if [ -z "$compiler" ]; then
		echo "Please specify the compiler you want to install";
		return;
	fi;
	
	if [ -z "$version" ]; then
		echo "Please specify the version";
		return;
	fi;

	if [ -z "$source_dir" ]; then
		echo "Please specify the source directory";
		return;
	fi;

	if [ -f "$compiler-$version.tar.xz" ]; then
		file="$compiler-$version.tar.xz";
	elif [ -f "$compiler-$version.tar.gz" ]; then
		file="$compiler-$version.tar.gz";
	else
		download $compiler $version
		install $compiler $version $install_dir
		return;
	fi;

	if [ -z "$install_dir" ]; then
		install_dir=/opt/compilers
	fi;

	extracted_dir="$(mktemp -d)/"
	real_source_dir="${extracted_dir}${compiler}-${version}"
	echo
	echo "---------------------------------------------------------------"
	echo "Compiler: $compiler-$version"
	echo "Source File: $file"
	echo "Extracted files in: $extracted_dir"
	echo "Real Source Directory: $real_source_dir"
	echo "Installation Directory: $install_dir"
	echo "---------------------------------------------------------------"
	echo

	mkdir -p "$extracted_dir";
	mkdir -p "$install_dir/$compiler-$version";
	mkdir -p "$real_source_dir";

	tar xvf "$file" -C "$extracted_dir"
	rpwd=$(pwd);
	cd "${real_source_dir}"
	./configure --prefix="$install_dir/$compiler-$version/"
	make -j $(nproc)
	cd "$rpwd";
	rm -rf "${extracted_dir}";
}
