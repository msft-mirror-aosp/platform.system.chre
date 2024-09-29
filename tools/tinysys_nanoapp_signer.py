#!/usr/bin/python

#
# Copyright 2024, The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

"""A script to sign nanoapps for testing purpose on tinysys platforms."""

import argparse
import ctypes
import hashlib
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric import utils


class HeaderInfo(ctypes.LittleEndianStructure):
  _fields_ = [
      ("magic_number", ctypes.c_uint32),
      ("header_version", ctypes.c_uint32),
      ("rollback_info", ctypes.c_uint32),
      ("binary_length", ctypes.c_uint32),
      ("flags", ctypes.c_uint64 * 2),
      ("binary_sha256", ctypes.c_uint8 * 32),
      ("reserved_chip_id", ctypes.c_uint8 * 32),
      ("reserved_auth_config", ctypes.c_uint8 * 256),
      ("reserved_image_config", ctypes.c_uint8 * 256),
  ]


def read_private_key(key_file, password):
  with open(key_file, "rb") as file:
    key_bytes = file.read()
    try:
      return serialization.load_der_private_key(key_bytes, password=password)
    except ValueError as e:
      pass  # Not a DER private key

    try:
      return serialization.load_pem_private_key(key_bytes, password=password)
    except ValueError:
      pass  # Not a PEM private key
    raise ValueError("Unable to parse the key file as DER or PEM")


def main():

  parser = argparse.ArgumentParser(
      description="Sign a binary to be authenticated on tinysys platforms"
  )
  parser.add_argument(
      "private_key_file",
      help="The private key (DER or PEM format) used to sign the binary",
  )
  parser.add_argument(
      "-p",
      "--password",
      type=str,
      help="Optional password encrypting the private key",
  )
  parser.add_argument(
      "nanoapp", help="The name of the nanoapp binary file to be signed"
  )
  parser.add_argument(
      "output_path", help="The path where the signed binary should be stored"
  )
  args = parser.parse_args()

  # Load the binary file.
  binary_data = None
  with open(args.nanoapp, "rb") as binary_file:
    binary_data = binary_file.read()

  # Load ECDSA private key.
  password = args.password.encode() if args.password else None
  private_key = read_private_key(args.private_key_file, password)

  # Generate a zero-filled header.
  header = bytearray(0x1000)

  # Fill the public key.
  public_key_numbers = private_key.public_key().public_numbers()
  header[0x200:0x220] = public_key_numbers.x.to_bytes(32, "big")
  header[0x220:0x240] = public_key_numbers.y.to_bytes(32, "big")

  # Fill header_info.
  sha256_hasher = hashlib.sha256()
  sha256_hasher.update(binary_data)
  header_info = HeaderInfo(
      magic_number=0x45524843,
      header_version=1,
      binary_length=len(binary_data),
      binary_sha256=(ctypes.c_uint8 * 32)(*sha256_hasher.digest()),
  )
  header_info_bytes = bytes(header_info)
  header[0x400 : 0x400 + len(header_info_bytes)] = header_info_bytes

  # Generate the signature.
  signature = private_key.sign(header[0x200:], ec.ECDSA(hashes.SHA256()))
  r, s = utils.decode_dss_signature(signature)
  r_bytes = r.to_bytes(32, "big")
  s_bytes = s.to_bytes(32, "big")
  header[:32] = r_bytes
  header[32:64] = s_bytes

  with open(f"{args.output_path}/{args.nanoapp}", "wb") as output:
    output.write(header)
    output.write(binary_data)


if __name__ == "__main__":
  main()
