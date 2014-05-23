#!/bin/sh

source ./build-fragments.sh;

pushd "$(dirname $0)" > /dev/null

clean=
target=

while [ $# -gt 0 ];
do
	case "$1" in
	--clean)
		clean="clean";
		;;
	*)
		lquit 1 "Unrecognized argument \"$1\"";
		;;
	esac;

	shift;
done;

if [ $clean ]; then target=":REBUILD"; fi;

if [[ -x $ant && $prefer != "maven" ]];
then
	# Ant build.

	echo "Ignore reports of failing builds. (Some device adapters do not compile; the necessary DLL, however, does.)";

	if [ $clean ];
	then
		banner "SPRING CLEANING";
		$ant -Dmm.architecture=${platform} -silent -buildfile build.xml clean || lbanquit 1 "CLEAN (ACTUALLY) FAILED";
	fi;

	$ant -Dmm.architecture=${platform} -silent -buildfile build.xml build-buildtools;

	banner "BUILDING C++ MMCORE";

	$ant -Dmm.architecture=${platform} -silent -buildfile build.xml build-cpp;
	test -a "build/$config/$platform/MMCoreJ_wrap.dll" || lbanquit 1 "C++ BUILD (ACTUALLY) FAILED";

	banner "BUILDING MMSTUDIO";

	$ant -Dmm.architecture=${platform} -silent -buildfile build.xml build-java;
	test -a "build/java/MMCoreJ.jar" && test -a "build/java/MMJ_.jar" || lbanquit 1 "JAVA BUILD (ACTUALLY) FAILED";

	banner "DEPLOYING MM FILES";

	$ant -Dmm.architecture=${platform} -Dmm.installdir="$dir" -silent -buildfile build.xml stage-only;
	test -a "$dir/MMCoreJ_wrap.dll" && test -a "$dir/plugins/Micro-Manager/MMCoreJ.jar" && test -a "$dir/plugins/Micro-Manager/MMJ_.jar" || lbanquit 1 "STAGING FAILED";
else
	# Non-Ant build (find msbuild; build required projects -- actually shorter than ant build; copy into place).
	test -x "$JAVA_HOME/bin/javac.exe" || lbanquit 1 "NO JAVAC -- NO JDK?";

	banner "FINDING MSBUILD";

	dotnetfwdir="$(registry_query "HKEY_LOCAL_MACHINE\\\\SOFTWARE\\\\Microsoft\\\\VisualStudio\\\\SxS\\\\VC7" "FrameworkDir32")"
	dotnetfwver="$(registry_query "HKEY_LOCAL_MACHINE\\\\SOFTWARE\\\\Microsoft\\\\VisualStudio\\\\SxS\\\\VC7" "FrameworkVer32")"
	msbuild="$dotnetfwdir$dotnetfwver\\msbuild.exe";

	test -x $msbuild || die "Couldn't find msbuild.exe ($msbuild). No .NET framework?";
	echo "Found msbuild.exe at $msbuild (fw \"$dotnetfwver\").";

	if [ $clean ];
	then
		banner "SPRING CLEANING";
		{ pushd mmstudio > /dev/null && $maven clean && popd; } || lbanquit 1 "CLEANING FAILED";
	fi;

	banner "BUILDING C++ MMCORE";

	$msbuild micromanager.sln /property:Configuration=$config /property:Platform=$platform /target:MMCore${target}\;MMCoreJ_wrap${target}\;PicardStage${target} //fileLogger1 //verbosity:minimal && test "$(grep -c '^Build FAILED\.$' msbuild1.log)" == "0" || lbanquit 1 "FAILED TO BUILD MMCOREJ_WRAP";
	test -a "build/$config/$platform/MMCoreJ_wrap.dll" || lbanquit 1 "C++ BUILD (ACTUALLY) FAILED";

	banner "BUILDING JAVA MMCORE";

	# We assume we don't have ant. We probably have maven, but MMCore isn't a maven project.
	mkdir -p build/java;

	# These lines are adapted from MMCoreJ_wrap/Makefile.am... I'm not proud.
	mkdir -p MMCoreJ_wrap/mmcorej;
	cp MMCoreJ_wrap/TaggedImage.java MMCoreJ_wrap/mmcorej;
	../3rdpartypublic/swig/swig.exe -c++ -java -package mmcorej -outdir MMCoreJ_wrap/mmcorej -module MMCoreJ "-DMMCOREJ_LIBRARY_PATH=\"\"" -o MMCoreJ_wrap/MMCoreJ_wrap.cxx MMCoreJ_wrap/MMCoreJ.i;
	$JAVA_HOME/bin/javac.exe -d "MMCoreJ_wrap" "mmstudio/src/org/json"/*.java || lbanquit 1 "JSON BUILD (ACTUALLY) FAILED";
	$JAVA_HOME/bin/javac.exe -cp "MMCoreJ_wrap" "MMCoreJ_wrap/mmcorej"/*.java -d "MMCoreJ_wrap" || lbanquit 1 "MMCOREJ_WRAP BUILD (ACTUALLY) FAILED";
	{ pushd MMCoreJ_wrap > /dev/null && "$JAVA_HOME/bin/jar.exe" cf ../build/Java/MMCoreJ.jar org/json/*.class mmcorej/*.class 2>&1 && popd > /dev/null; } || lbanquit 1 "MMCOREJ_WRAP JAR (ACTUALLY) FAILED";

	banner "BUILDING MMSTUDIO";

	{ pushd mmstudio > /dev/null && $maven verify install; popd > /dev/null; } || lbanquit 1 "MMSTUDIO MAVEN BUILD FAILED";

	banner "COPYING MM FILES";

	mkdir -p $dir/plugins/Micro-Manager;
	cp "build/java/MMCoreJ.jar" "$dir/plugins/Micro-Manager/MMCoreJ.jar" || lbanquit 1 "FAILED TO COPY MMCOREJ.JAR";
	cp "build/java/MMJ_.jar" "$dir/plugins/Micro-Manager/MMJ_.jar" || lbanquit 1 "FAILED TO COPY MMJ_.JAR";
	cp "build/$config/$platform/MMCoreJ_wrap.dll" "$dir/MMCoreJ_wrap.dll" || lbanquit 1 "FAILED TO COPY MMCOREJ_WRAP.DLL";
	cp "build/$config/$platform/mmgr_dal_PicardStage.dll" "$dir/mmgr_dal_PicardStage.dll" || lbanquit 1 "FAILED TO COPY PICARDSTAGE DAL";
	test -a "$dir/MMCoreJ_wrap.dll" && test -a "$dir/plugins/Micro-Manager/MMCoreJ.jar" && test -a "$dir/plugins/Micro-Manager/MMJ_.jar" || lbanquit 1 "STAGING FAILED";
fi;

lbanquit 0 "MM BUILD: DONE";
