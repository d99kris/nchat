#!/bin/bash

if [[ -z "$GID" ]]; then
	GID="$UID"
fi

BINARY_NAME=/usr/bin/mautrix-signal

# Define functions.
function fixperms {
	chown -R $UID:$GID /data

	# /opt/mautrix-signal is read-only, so disable file logging if it's pointing there.
	if [[ "$(yq e '.logging.writers[1].filename' /data/config.yaml)" == "./logs/mautrix-signal.log" ]]; then
		yq -I4 e -i 'del(.logging.writers[1])' /data/config.yaml
	fi
}

if [[ ! -f /data/config.yaml ]]; then
	$BINARY_NAME -c /data/config.yaml -e
	echo "Didn't find a config file."
	echo "Copied default config file to /data/config.yaml"
	echo "Modify that config file to your liking."
	echo "Start the container again after that to generate the registration file."
	exit
fi

if [[ ! -f /data/registration.yaml ]]; then
	$BINARY_NAME -g -c /data/config.yaml -r /data/registration.yaml || exit $?
	echo "Didn't find a registration file."
	echo "Generated one for you."
	echo "See https://docs.mau.fi/bridges/general/registering-appservices.html on how to use it."
	exit
fi

cd /data
fixperms

DLV=/usr/bin/dlv
if [ -x "$DLV" ]; then
    if [ "$DBGWAIT" != 1 ]; then
        NOWAIT=1
    fi
    BINARY_NAME="${DLV} exec ${BINARY_NAME} ${NOWAIT:+--continue --accept-multiclient} --api-version 2 --headless -l :4040"
fi

exec su-exec $UID:$GID $BINARY_NAME
