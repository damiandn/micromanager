plat="$1"
winplat="win$(echo $plat | tail -c3)"
echo $winplat

STABLE_FIJI_URL="http://jenkins.imagej.net/job/Stable-Fiji"
FIJI_URL="$STABLE_FIJI_URL/lastSuccessfulBuild/artifact/fiji-$winplat.tar.gz"
JDK_URL="http://fiji.sc/cgi-bin/gitweb.cgi?p=java/$winplat.git;a=snapshot;h=HEAD;sf=tgz"

WD="$(pwd)"
SRC="$(cd ../ && pwd)"
FIJI_JAVA_HOME="$WD/bin_$plat/java/$plat/jdk1.6.0_24"

(
	cd $SRC &&

	if ! test -d 3rdpartypublic
	then
		echo "Cloning Micro-Manager's 3rdparty libraries" &&
		git clone git://github.com/openspim/3rdpartypublic &&
		(
			cd 3rdpartypublic/ &&
			git config remote.origin.pushURL \
				contrib@fiji.sc:/srv/git/mmanager-3rdparty
		)
	else
		echo "(should pull; ignoring!)" #(cd 3rdpartypublic && git pull)
	fi &&

	if ! test -f $FIJI_JAVA_HOME/include/jni.h
	then
		echo "Downloading and unpacking the JDK" &&
		curl "$JDK_URL" |
		(
			mkdir -p $FIJI_JAVA_HOME &&
			cd $FIJI_JAVA_HOME &&
			tar --strip-components=2 -xzf -
		)
	fi &&

	(
		cd $WD &&
		if ! test -f "bin_$plat/ImageJ-$plat.exe"
		then
			echo "Copying Fiji into Micro-Manager's bin_$plat/ directory" &&
			curl $FIJI_URL |
			(
				cd "bin_$plat/" &&
				tar --strip-components=1 -xzf - &&
				"./ImageJ-$winplat.exe" --update add-update-site \
					OpenSPIM http://openspim.org/update/ \
					spim@openspim.org update/
			)
		fi
	)
)

