#!/bin/sh

source ./build-fragments.sh;

dir="OpenSPIM.app";
smm=
clean=
clinst=
buildsh="build-maven.sh"
config="Release"

maven="/dev/null"
ant="/dev/null"
prefer=; # Use whichever is best for the task at hand.

pushd "$(dirname $0)" > /dev/null;

while [ $# -gt 0 ];
do
	case "$1" in
	-h | --help)
		cat <<EOS
Usage: $0 [options]
Options:
    --64, --x64            specify 64-bit architecture (otherwise autodetect)
    --32, --win32, --x86   specify 32-bit architecture (otherwise autodetect)
    --dir <dir>            specify app directory (default OpenSPIM.app)
    --clean-installer      delete installer after use
    --staged-mm            don't build MM; compile vs installed JARs
    --clean                clean things up before building
    --maven-at <mvn>       specify path to maven (otherwise autodetect)
    --ant-at <ant>         specify path to ant (otherwise autodetect)
    --prefer <ant|maven>   use specified tool whenever possible
    --no-ant, --no-maven   assume the specified tool isn't available

This tool makes an effort to build an up-to-date version of OpenSPIM by:
1. Determine system's processor type (32 or 64 bit); find Ant, Maven, or both.
2. Download latest Micro-Manager nightly build and install.
3. Upgrade MM to Fiji and install OpenSPIM from update site.
4. Build (optionally) MM core/studio and OpenSPIM and copy to app.

By default, the application is installed to ./OpenSPIM.app, MMCore/studio is
compiled with Ant, and OpenSPIM is built with Maven.
EOS
		lquit 0 "";
		;;
	--64 | --x64)
		bits="64";
		;;
	--32 | --win32 | --x86)
		bits="32";
		;;
	--dir)
		shift;
		dir="$1";
		;;
	--clean-installer)
		clinst="--clean-installer";
		;;
	--staged-mm)
		smm="--staged-mm";
		;;
	--clean)
		clean="--clean";
		;;
	--maven-at)
		shift;
		maven=$(resolve_file "$1");

		test -x $maven || lquit 1 "Maven not found at $maven.";
		;;
	--ant-at)
		shift;
		ant=$(resolve_file "$1");

		test -x $ant || lquit 1 "Ant not found at $ant.";
		;;
	--prefer)
		shift;
		prefer="$1";
		;;
	--no-ant)
		ant=;
		;;
	--no-maven)
		maven=;
		;;
	*)
		lquit 1 "Unrecognized argument \"$1\". Try --help.";
		;;
	esac;

	shift;
done;

openspim_build_dir=$(resolve_file $PWD/$dir);
test -d $openspim_build_dir || mkdir $openspim_build_dir;

if ! [ $bits ];
then
	banner "CHECKING SYSTEM";

	bits=$(wmic OS get OSArchitecture | sed -n -e '/-bit/s/.*\([36][24]\)-bit.*/\1/p');

	echo "Processor architecture: ${bits}-bit.";
fi;

case $bits in
32)
	platform="Win32";
	;;
64)
	platform="x64";
	;;
*)
	lbanquit 1 "ABNORMAL $bits-BIT PROCESSOR";
	;;
esac;

{ test -x "$ant" && test -x "$maven"; } || banner "FINDING BUILD TOOLS";

if [[ "$ant" && ! -x "$ant" ]];
then
	ant="$(which ant || echo '/dev/null')";
	if ! [ -x "$ant" ]; then ant=$(resolve_file ../3rdpartypublic/apache-ant-?.?.?/bin/ant); fi;
	test -x "$ant" && echo "Apache Ant found at $ant" || { echo "Couldn't find Apache Ant..."; prefer="maven"; };
fi;

if [[ "$maven" && ! -x "$maven" ]];
then
	maven="$(which mvn || echo '/dev/null')";
	if ! [ -x "$maven" ]; then maven=$(resolve_file ../3rdpartypublic/apache-maven-?.?.?/bin/mvn); fi;
	test -x "$maven" && echo "Apache Maven found at $maven" || { echo "Couldn't find Apache Maven..."; prefer="ant"; };
fi;

test -x "$ant" || test -x "$maven" || lbanquit 1 "NO BUILD TOOL AVAILABLE";

banner "INITIAL SET-UP";

ant=$ant maven=$maven prefer=$prefer bits=$bits dir=$openspim_build_dir build-do-setup.sh $clinst || lbanquit 1 "SETUP FAILED";

jh=$JAVA_HOME;

if [ -z $smm ] || \
	! [ -x "$ant" ] || \
	[ "$prefer" = "maven" ] || \
	[ -z "$prefer" -a -x "$maven" ];
then
	# We need a JDK to build MM JARs, or to build anything with Maven.
	# Note that if there's no stated preference, build-spimacq prefers to use maven while build-mm-prereqs prefers to use ant.

	banner "FINDING JDK";

	if ! [ -f $jh/include/jni.h ]; then jh=$(resolve_file $openspim_build_dir/java/$platform/jdk1.6.????); fi;
	if ! [ -f $jh/include/jni.h ]; then jh=$(resolve_file jdk/$platform); fi; # This should be last -- the JDK will be created here if it can't be found.

	if ! [ -f $jh/include/jni.h ];
	then
		echo "Unable to find JDK; downloading...";

		curl "http://fiji.sc/cgi-bin/gitweb.cgi?p=java/win${bits}.git;a=snapshot;h=HEAD;sf=tgz" |
		(
			mkdir -p jdk/$platform &&
			cd jdk/$platform &&
			tar --strip-components=2 -xzf -
		) || lbanquit 1 "JDK DOWNLOAD FAILED";

		jh=$(resolve_file jdk/$platform);
	fi;

	test -f $jh/include/jni.h && test -f $jh/bin/java.exe || lbanquit 1 "UNABLE TO OBTAIN JDK";
	echo "Found JDK at $jh";

	if [ -z $smm ];
	then
		banner "BUILDING PREREQUISITES";

		ant=$ant maven=$maven prefer=$prefer JAVA_HOME=$jh dir=$openspim_build_dir config=$config platform=$platform build-mm-prereqs.sh $clean || lbanquit 1 "MM BUILD FAILED";
	fi;
else
	banner "FINDING JRE";

	if ! [ -f $jh/bin/javaw.exe ]; then jh=$(resolve_file $openspim_build_dir/jre); fi;
	test -f $jh/bin/java.exe || lbanquit 1 "UNABLE TO FIND JRE";

	echo "Found JRE at $jh";
fi;

ant=$ant maven=$maven prefer=$prefer JAVA_HOME=$jh openspim_build_dir=$openspim_build_dir build-spimacq.sh $clean $smm || lbanquit 1 "BUILD FAILED";

lbanquit 0 "BUILD: DONE";
