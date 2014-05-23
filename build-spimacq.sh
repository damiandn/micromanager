#!/bin/sh

source ./build-fragments.sh;

test $openspim_build_dir || { quit 1 "openspim_build_dir not set; $0 should not be called directly!"; };

pushd "$(dirname $0)" > /dev/null

mmstudio_src=$(resolve_file "build/java/MMJ_.jar");
mmcore_src=$(resolve_file "build/java/MMCoreJ.jar");

while [ $# -gt 0 ];
do
	case "$1" in
	--staged-mm)
		mmstudio_src=$(resolve_file "${openspim_build_dir}/plugins/Micro-Manager/MMJ_.jar");
		mmcore_src=$(resolve_file "${openspim_build_dir}/plugins/Micro-Manager/MMCoreJ.jar");
		;;
	--clean)
		clean="clean";
		;;
	*)
		echo "Unrecognized argument \"$1\"";
		lquit 1;
		;;
	esac;

	shift;
done;

if [[ -x $maven && "$prefer" != "ant" ]];
then
	banner "UPDATING LOCAL REPO";

	$maven install:install-file -Dfile="${mmstudio_src}" -DgroupId="org.micromanager" -DartifactId="mmstudio" -Dversion="1" -Dpackaging="jar" || lbanquit 1 "FAILED TO ADD MMSTUDIO";
	$maven install:install-file -Dfile="${mmcore_src}" -DgroupId="org.micromanager" -DartifactId="MMCoreJ" -Dversion="1" -Dpackaging="jar" || lbanquit 1 "FAILED TO ADD MMCORE";

	banner "BUILDING SPIMACQUISITION";

	pushd "plugins/SPIMAcquisition" > /dev/null;

	if [ $clean ]; then $maven clean; fi;

	$maven -Djar.finalName="SPIMAcquisition" || { popd > /dev/null; lbanquit 1 "FAILED TO BUILD SPIMACQUISITION"; };
	popd > /dev/null;

	mkdir -p build/Java/plugins;
	cp plugins/SPIMAcquisition/target/SPIMAcquisition.jar build/Java/plugins/SPIMAcquisition.jar;
elif [[ -x $ant ]];
then
	banner "BUILDING SPIMACQUISITION"

	$ant -Dmm.java.lib.mmcorej="$mmcore_src" -Dmm.java.lib.mmstudio="$mmstudio_src" -buildfile plugins/SPIMAcquisition/build.xml $clean jar || lbanquit 1 "FAILED TO BUILD SPIMACQUISITION";
else
	lbanquit 1 "NO BUILD TOOL AVAILABLE";
fi;

banner "COPYING TO TARGET"

cp build/Java/plugins/SPIMAcquisition.jar $openspim_build_dir/mmplugins/SPIMAcquisition.jar || lbanquit 1 "COULDN'T COPY JAR";

lbanquit 0 "SPIMACQUISITION BUILD: DONE";
