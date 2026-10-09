#!/bin/sh
# Uploads the Android release signing key to this repository's GitHub Actions
# secrets, so the release workflow can sign the APK the same way every time.
#
# Needs the GitHub CLI, logged in (gh auth login), and android/keystore.properties
# pointing at the keystore (created alongside the key; never committed).

set -eu

props="$(dirname "$0")/../android/keystore.properties"
if [ ! -f "$props" ]; then
    echo "error: $props not found" >&2
    exit 1
fi
get() { sed -n "s/^$1=//p" "$props"; }

store=$(get storeFile)
# Values go in on stdin so they never appear in the process list.
base64 -w0 "$store" | gh secret set SYNAXIS_ANDROID_KEYSTORE_BASE64
printf '%s' "$(get storePassword)" | gh secret set SYNAXIS_KEYSTORE_PASSWORD
printf '%s' "$(get keyAlias)" | gh secret set SYNAXIS_KEY_ALIAS
printf '%s' "$(get keyPassword)" | gh secret set SYNAXIS_KEY_PASSWORD
echo "Signing secrets set. Back up $store and $props somewhere safe:"
echo "without them you can't publish updates that install over this version."
