#!/bin/sh

quit() {
	ecode=$1; shift;
	test -z "$1" || echo "$1";
	exit $ecode;
}

banner() {
	echo;
	len=$(echo "$1" | wc -c);
	printf "%*s%s%s%s%*s\n" "$(expr \( 61 - $len \) / 2 )" "" "----==== " "$1" " ====----" "$(expr \( 62 - $len \) / 2 )" "";
	echo;
}

# I'm such a sucker for puns.
banquit() {
	ecode=$1; shift;
	banner "$@";
	exit $ecode;
}

registry_query() {
	printf '%s\n%s\n' \
		'package require registry 1.0' \
		"puts [registry get \"$1\" \"$2\"]" |
	tclsh
}

lquit() {
	popd > /dev/null;
	quit "$@";
}

lbanquit() {
	popd > /dev/null;
	banquit "$@";
}

resolve_file() {
	if [ -d "$@" ];
	then
		pushd "$@" > /dev/null;
		echo $PWD;
		popd > /dev/null;
	elif [ -d "$(dirname "$@")" ];
	then
		pushd "$(dirname "$@")" > /dev/null;
		echo $PWD/$(basename "$@");
		popd > /dev/null;
	else
		echo "$(dirname "$@")/$(basename "$@")";
	fi;
}
