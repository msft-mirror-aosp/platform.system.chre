# flatbuffers

This folder contains a modified version of the FlatBuffers implementation header
file (flatbuffers.h) which customizes it for running in the CHRE environment.
When upgrading to a newer FlatBuffers release, be sure to manually merge the
changes described in the comment at the top of flatbuffers.h, and apply them to
new additions as well (e.g. removal of std::string usage) to maintain support.
The FlatBuffers IDL compiler (flatc) can be used without modification, but must
match the version of the Flatbuffers library used here.

The FlatBuffers project is hosted at https://github.com/google/flatbuffers/

## Current version

The version currently supported is [v1.12.0](https://github.com/google/flatbuffers/releases/tag/v1.12.0).

### Building flatc

Official build instructions: https://flatbuffers.dev/flatbuffers_guide_building.html

Instructions updated May 29, 2024.

```shell
mkdir /tmp/flatbuffer-v1.12.0
cd /tmp/flatbuffer-v1.12.0
wget https://github.com/google/flatbuffers/archive/refs/tags/v1.12.0.tar.gz -O flatbuffers-1.12.0.tar.gz
tar -xzvf flatbuffers-1.12.0.tar.gz
cd flatbuffers-1.12.0
cmake .
make flatc
```

Adding flatc to your PATH
```shell
export PATH=$PATH:/tmp/flatbuffer-v1.12.0/flatbuffers-1.12.0
```
