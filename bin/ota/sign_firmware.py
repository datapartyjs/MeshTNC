#!/usr/bin/env python3
"""Make and use the Ed25519 key that signs MeshTNC firmware for 'ota' serial updates.

  sign_firmware.py keygen <private.pem>                 new key pair; prints the public key
  sign_firmware.py pubkey <private.pem>                 public key as the OTA_PUBLIC_KEY hex
  sign_firmware.py sign <private.pem> <firmware.bin>    writes <firmware.bin>.sig (64 bytes)
  sign_firmware.py verify <public key hex | private.pem> <firmware.bin> [<firmware.bin.sig>]

The signature is Ed25519 (RFC 8032) over the SHA-256 digest of the .bin, which is what the
firmware computes while the image streams in (src/helpers/FirmwareUpdater.cpp). The firmware
checks it against the key compiled in with -D OTA_PUBLIC_KEY='"<64 hex chars>"'
(variants/byomesh/platformio.ini). Needs the 'cryptography' package: pip install cryptography
"""

import hashlib
import sys

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey
from cryptography.exceptions import InvalidSignature


def load_private(path):
    with open(path, "rb") as f:
        key = serialization.load_pem_private_key(f.read(), password=None)
    if not isinstance(key, Ed25519PrivateKey):
        sys.exit("%s is not an Ed25519 private key" % path)
    return key


def public_hex(private):
    return private.public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw).hex()


def load_public(arg):
    if len(arg) == 64 and all(c in "0123456789abcdefABCDEF" for c in arg):
        return Ed25519PublicKey.from_public_bytes(bytes.fromhex(arg))
    return load_private(arg).public_key()


def digest(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).digest()


def main(argv):
    if len(argv) >= 3 and argv[1] == "keygen":
        private = Ed25519PrivateKey.generate()
        with open(argv[2], "wb") as f:
            f.write(private.private_bytes(
                serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                serialization.NoEncryption()))
        print("private key written to %s (keep it secret)" % argv[2])
        print("public key: %s" % public_hex(private))
        print("build flag: -D OTA_PUBLIC_KEY='\"%s\"'" % public_hex(private))
    elif len(argv) >= 3 and argv[1] == "pubkey":
        print(public_hex(load_private(argv[2])))
    elif len(argv) >= 4 and argv[1] == "sign":
        private = load_private(argv[2])
        sig = private.sign(digest(argv[3]))
        out = argv[4] if len(argv) > 4 else argv[3] + ".sig"
        with open(out, "wb") as f:
            f.write(sig)
        print("signature written to %s" % out)
        print("signature: %s" % sig.hex())
    elif len(argv) >= 4 and argv[1] == "verify":
        public = load_public(argv[2])
        sig_path = argv[4] if len(argv) > 4 else argv[3] + ".sig"
        with open(sig_path, "rb") as f:
            sig = f.read()
        try:
            public.verify(sig, digest(argv[3]))
            print("OK - %s is signed by this key" % argv[3])
        except InvalidSignature:
            sys.exit("FAILED - %s is not signed by this key" % argv[3])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
