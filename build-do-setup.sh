#!/bin/sh

source ./build-fragments.sh;

pushd "$(dirname "$0")" > /dev/null;

while [ $# -gt 0 ];
do
	case "$1" in
	--clean-installer)
		clinst=t;
		;;
	*)
		lquit 1 "Unrecognized argument \"$1\"";
		;;
	esac;

	shift;
done;

if ! [ $bits ];
then
	banner "CHECKING SYSTEM";

	bits=$(wmic OS get OSArchitecture | sed -n -e '/-bit/s/.*\([36][24]\)-bit.*/\1/p');

	echo "Processor architecture: ${bits}-bit.";
fi;

test ! -x "${dir}/ImageJ-win${bits}.exe" || lquit 0 "Nothing to do."; # ...Probably.

if ! [ -x "${dir}/ImageJ.exe" ];
then
	if ! [ -x setup.exe ];
	then
		banner "DOWNLOADING MICRO-MANAGER";

		yest=$(date --date='yesterday' +'%Y%m%d'); # Today's isn't always available.
		mmver=$(grep '1.[0-9].[0-9]*' version.txt || curl --silent "http://valelab.ucsf.edu/~MM/nightlyBuilds/1.4/Windows/" | sed -n -e "/$yest/s/.*\(1\.[0-9]\.[0-9][0-9]\).*/\1/p" | head -n1);

		echo "Using nightly build ${mmver} from ${yest}";
		echo;

		curl "http://valelab.ucsf.edu/~MM/nightlyBuilds/1.4/Windows/MMSetup_${bits}bit_${mmver}_${yest}.exe" > setup.exe;

		# This 2MB lower bound is heuristic, but if the URL isn't valid, the server dumps some HTML back.
		test -x setup.exe && test $(wc -c setup.exe | cut -f 1 -d ' ') -gt 2097152 || { rm -f setup.exe; lbanquit 1 "MM DOWNLOAD FAILED"; };
	else
		test $(wc -c setup.exe | cut -f 1 -d ' ') -gt 2097152 || lbanquit 1 "UNLIKELY MM SETUP FILE";

		echo "Note: MM installer already exists; chosen/detected processor architecture will be ignored.";
	fi;

	banner "INSTALLING MICRO-MANAGER";

	winpath=$(pushd $dir > /dev/null && pwd -W | sed 's/\//\\/g' && popd > /dev/null);

	echo "Installing...";
	setup.exe //VERYSILENT //SP- //SUPPRESSMSGBOXES //NOICONS //MERGETASKS="!Icons,!desktopicon" //LOG="setup-log.txt" //DIR="$winpath\\";
	test -x "${dir}/ImageJ.exe" || lbanquit 1 "MM SETUP FAILED";

	echo "Finished.";
fi;

pushd "${dir}" > /dev/null;

banner "CONVERTING MM TO FIJI";

echo "Temporarily disabling Micro-Manager autostart..."
mv "macros/StartupMacros.txt" "macros/StartupMacros.txt.mm.old"

if ! [ -a updater-fix.tmp.js ];
then
	cat > updater-fix.tmp.js <<"BLOCK"

var arguments = [ getArgument() ];

var print = function(text) {
	importClass(Packages.java.lang.System);
	System.err.print(text);
}

BLOCK

	curl --silent http://update.imagej.net/bootstrap.js >> updater-fix.tmp.js;
fi;

echo "Installing new updater..."; echo;
jre/bin/java.exe -jar ij.jar -batch updater-fix.tmp.js update 2>&1;
test -x "ImageJ-win${bits}.exe" || lbanquit 1 "UPDATER SETUP FAILED";

banner "INSTALLING SPIMACQUISITION";

echo "Adding update site..."; echo;
ImageJ-win${bits}.exe --console --update add-update-site OpenSPIM http://openspim.org/update/ spim@openspim.org update/ 2>&1;

echo "Updating..."; echo;
ImageJ-win${bits}.exe --console --update update 2>&1;

# Presently, SPIMAcquisition depends on some spare jars -- these are already available, so delete the fresh ones:
rm plugins/MMCoreJ.jar plugins/MMJ_.jar plugins/MMAcqEngine.jar jars/swing-layout-1.0.4.jar

# ImageJ has an old version of rsyntaxarea jar that doesn't support the correct constructor.
mv jars/rsyntaxtextarea-2.5.0.jar jars/rsyntaxtextarea-2.5.0.jar.incompatible

banner "CLEANING UP";

echo "Backing up and replacing old ImageJ launcher...";
mv "ImageJ.exe" "ImageJ.exe.old";
cp "ImageJ-win${bits}.exe" "ImageJ.exe";

echo "Restoring Micro-Manager autostart...";

mkdir macros/AutoRun
cat > macros/AutoRun/MicroManagerStudio.ijm <<"BLOCK"
run("Micro-Manager Studio")
wait(8000)
call("spim.SPIMAcquisition.checkAutoStartScript")
BLOCK

echo "Cleaning up installer..."

if [ $clinst ];
then
	rm updater-fix.tmp.js;
fi;

popd > /dev/null;

if [ $clinst ];
then
	rm setup.exe setup-log.txt;
fi;

lbanquit 0 "SETUP: DONE";
