#/bin/bash

SRCDIR=`dirname "$0"`
NSSDB=db
CONF=softhsm2.conf
SOPIN="77777777"
PIN="12345678"
export GNUTLS_PIN=$PIN


# Detect the softhsm2 library path
tid="pkcs11 agent test"

try_token_libs() {
	for _lib in "$@" ; do
		if test -f "$_lib" ; then
			echo "Using token library $_lib"
			P11LIB="$_lib"
			return
		fi
	done
	echo "skipped: Unable to find PKCS#11 token library"
	exit 0
}

try_token_libs \
	/usr/local/lib/softhsm/libsofthsm2.so \
	/usr/lib64/pkcs11/libsofthsm2.so \
	/usr/lib/x86_64-linux-gnu/softhsm/libsofthsm2.so\
        /usr/lib/softhsm/libsofthsm2.so

generate_cert() {
	TYPE="$1"
	ID="$2"
	LABEL="$3"

	# Generate key pair
	pkcs11-tool --keypairgen --key-type="$TYPE" --login --pin=$PIN \
		--module="$P11LIB" --label="$LABEL" --id=$ID

	if [[ "$?" -ne "0" ]]; then
		echo "Couldn't generate $TYPE key pair"
		return 1
	fi

	# check type value for the PKCS#11 URI (RHEL7 is using old "object-type")
	TYPE_KEY="type"
	p11tool --list-all --provider="$P11LIB" --login | grep "object-type" && \
		TYPE_KEY="object-type"

	# Generate certificate
	certtool --generate-self-signed --outfile="$TYPE.cert" --template=$SRCDIR/cert.cfg \
		--provider="$P11LIB" --load-privkey "pkcs11:object=$LABEL;$TYPE_KEY=private" \
		--load-pubkey "pkcs11:object=$LABEL;$TYPE_KEY=public"
	# convert to DER:
	openssl x509 -inform PEM -outform DER -in "$TYPE.cert" -out "$TYPE.cert.der"
	# Write certificate
	pkcs11-tool --write-object "$TYPE.cert.der" --type=cert --id=$ID \
		--label="$LABEL" --module="$P11LIB"
	rm "$TYPE.cert" "$TYPE.cert.der"

	p11tool --login --provider="$P11LIB" --list-all
}

# Check requirements
if [ ! -f "$(which pkcs11-tool)" ]; then
	echo "ERROR: Need 'opensc' package to run tests"
	exit 1
fi
if [ ! -f "$(which p11tool)" -o ! -f "$(which certtool)" ]; then
	echo "ERROR: Need 'gnutls-utils' package to run tests"
	exit 1
fi
if [ ! -f "$(which modutil)" ]; then
	echo "ERROR: Need 'nss-tools' package to run tests"
	exit 1
fi
if [ ! -f "$(which openssl)" ]; then
	echo "ERROR: Need 'openssl' package to run tests"
	exit 1
fi
if [ ! -f "$(which softhsm2-util)" ]; then
	echo "ERROR: Need 'softhsm' package to run tests"
	exit 1
fi



export SOFTHSM2_CONF="$CONF"
# SoftHSM configuration file
if [ ! -f "$CONF" ]; then
	echo "directories.tokendir = `pwd`/tokens/" > $CONF
	echo "slots.removable = true" >> $CONF
fi

# SoftHSM configuration directory
if [ ! -d "tokens" ]; then
	mkdir "tokens"

	# Init token
	softhsm2-util --init-token --slot 0 --label "SC test" --so-pin="$SOPIN" --pin="$PIN"

	# Generate 2048b RSA Key trio
	generate_cert "RSA:2048" "01" "RSA_id"
	generate_cert "RSA:2048" "02" "RSA_sign"
	generate_cert "RSA:2048" "03" "RSA_encryption"
fi
# NSS DB
if [ ! -d "$NSSDB" ]; then
	mkdir "$NSSDB"
        # Do not add a softhsm2 to the nssdb if there is already p11-kit-proxy
        modutil -create -dbdir "sql:$NSSDB" -force
        modutil -list -dbdir "sql:$NSSDB" | grep "library name: p11-kit-proxy.so" ||\
        	modutil -add "SoftHSM PKCS#11" -dbdir "sql:$NSSDB" -libfile "$P11LIB" -force
fi
