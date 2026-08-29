#!/bin/sh
# The server root public key a build pins, as C initializer
# bytes.
#
#   rootkey.sh <game.public.pem>     # or game.private.pem, or a DER SPKI
#   rootkey.sh 04f2f15c...           # the uncompressed P-256 point, hex
set -eu

usage() {
    echo "usage: rootkey.sh <key.pem | 04-hex-point>" >&2
    exit 2
}

[ "$#" -eq 1 ] || usage
arg="$1"
[ -n "$arg" ] || usage

hex=

if [ -f "$arg" ]; then
    command -v openssl >/dev/null 2>&1 || {
        echo "rootkey.sh: openssl is needed to read $arg" >&2
        exit 1
    }
    # A public pem reads as one; a private pem is turned into its public half
    # first. Either way the SPKI DER for P-256 ends in the 65-byte uncompressed
    # point, which is exactly what the wire carries and what we verify against.
    #
    # Through a file rather than a variable: DER carries NUL bytes, which a
    # command substitution drops, and openssl's own exit status is what says the
    # pem was readable, down a pipe it would be od's, which succeeds on
    # nothing at all and would turn "that is not a key" into an empty point.
    der="$(mktemp "${TMPDIR:-/tmp}/rootkey-XXXXXX")"
    trap 'rm -f "$der"' EXIT INT TERM
    if openssl ec -pubin -in "$arg" -outform DER >"$der" 2>/dev/null &&
       [ -s "$der" ]; then
        :
    elif openssl ec -in "$arg" -pubout -outform DER >"$der" 2>/dev/null &&
         [ -s "$der" ]; then
        :
    else
        echo "rootkey.sh: $arg is not an EC key openssl can read" >&2
        exit 1
    fi
    spki="$(od -An -tx1 -v <"$der" | tr -d ' \n')"
    # OpenSSL 3's `ec` app re-encodes whatever key it is handed rather than
    # refusing a curve it was not asked for, so the curve is checked here: the
    # prime256v1 OID (1.2.840.10045.3.1.7) inside the SPKI. Without this line an
    # RSA modulus whose last 65 bytes happen to begin 04 would pin as a point.
    case "$spki" in
        *06082a8648ce3d030107*) ;;
        *) echo "rootkey.sh: $arg is not a P-256 (prime256v1) key" >&2; exit 1;;
    esac
    hex="$(printf '%s' "$spki" | tail -c 130)"
else
    case "$arg" in
        *[!0-9A-Fa-f]*) 
            echo "rootkey.sh: '$arg' is neither a file nor a hex point" >&2
            exit 1;;
    esac
    hex="$(printf '%s' "$arg" | tr 'A-F' 'a-f')"
fi

# 0x04 is the uncompressed-point tag. A compressed point (0x02/0x03) is half the
# length and would fit nothing here, and a public pem for another curve would
# arrive at some other length: both are caught by these two lines rather than by
# a client that cannot verify.
case "$hex" in
    04*) ;;
    *) echo "rootkey.sh: $arg: the point does not begin 04 (uncompressed)" >&2
       exit 1;;
esac
[ "${#hex}" -eq 130 ] || {
    echo "rootkey.sh: expected 65 bytes (130 hex chars), got $((${#hex} / 2))" >&2
    exit 1
}

# One line, comma-separated, no trailing comma, an initializer for a
# `const u8 [65]`, and short enough to read in the generated header.
out=
rest="$hex"
while [ -n "$rest" ]; do
    byte="${rest%"${rest#??}"}"
    rest="${rest#??}"
    if [ -z "$out" ]; then out="0x$byte"; else out="$out,0x$byte"; fi
done
printf '%s\n' "$out"
