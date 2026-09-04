#!/bin/sh
# Regenerate the self-signed manuals-spoof cert the firmware embeds.
# The PRIVATE KEY (server.key) is intentionally NOT committed. Run this once
# before building so EMBED_TXTFILES can pick up server.key + server.crt.
#
# Shape the PS5 accepts (an EC / CA=false cert was rejected with WV-109153-9):
#   RSA-2048, CA:TRUE, EKU serverAuth, SAN = the manuals hostnames + the AP IP.
set -e
DIR="$(dirname "$0")"
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout "$DIR/server.key" -out "$DIR/server.crt" -days 3650 \
  -subj "/CN=manuals.playstation.net" \
  -addext "basicConstraints=critical,CA:TRUE" \
  -addext "keyUsage=critical,digitalSignature,keyCertSign,keyEncipherment" \
  -addext "extendedKeyUsage=serverAuth" \
  -addext "subjectAltName=DNS:manuals.playstation.net,DNS:*.playstation.net,IP:192.168.4.1"
echo "wrote server.key + server.crt in $DIR"
