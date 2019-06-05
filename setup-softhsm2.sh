#/bin/bash

SRCDIR=`dirname "$0"`
NSSDB=db
CONF=softhsm2.conf
SOPIN="77777777"
PIN="12345678"
export GNUTLS_PIN=$PIN

P11LIB=/usr/lib64/pkcs11/libsofthsm2.so

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

	# Generate 1024b RSA Key pair
	generate_cert "RSA:1024" "01" "RSA_auth"
	generate_cert "RSA:1024" "02" "RSA_sign"
fi
# NSS DB
if [ ! -d "$NSSDB" ]; then
	mkdir "$NSSDB"
        modutil -create -dbdir "sql:$NSSDB" -force
# Skipping that line thanks to last NSS changes
#	modutil -add "SoftHSM PKCS#11" -dbdir "sql:$NSSDB" -libfile "$P11LIB" -force
fi
