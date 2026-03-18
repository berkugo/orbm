# Complex CORBA demo (TAO)

This example suite is designed to stress-test ORBM’s GIOP/CDR decoding with a more complex IDL set:

- 5–6 IDL files
- 10+ operations across multiple interfaces
- any / union / arrays / bounded strings+wstrings / exceptions / big payload (fragment) scenarios

## Build

This demo expects a working **TAO** installation (ACE/TAO) providing:

- `tao_idl`
- TAO/ACE headers and libraries

In this repo’s environment, TAO is typically under:

- `/root/project_x/corba_Viewer/ACE_wrappers`

You may need:

```bash
export ACE_ROOT=/root/project_x/corba_Viewer/ACE_wrappers
export TAO_ROOT=$ACE_ROOT/TAO
export LD_LIBRARY_PATH=$ACE_ROOT/lib:$LD_LIBRARY_PATH
```

Build from this directory:

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

Start the server (writes an IOR to a file):

```bash
./build/complex_corba_server -ORBEndpoint iiop://127.0.0.1:4501 -o server.ior
```

Run the client (reads the IOR and executes the full call suite):

```bash
./build/complex_corba_client -k file://server.ior
```

Alternatively, use a stable corbaloc key (recommended in container environments):

```bash
./build/complex_corba_client -k corbaloc:iiop:127.0.0.1:4501/DemoRoot
```

## What this tests (high level)

- **MathService**: scalars, `LongVec[16]`, `LongSeq`, `Record` + user exception
- **StoreService**: struct storage, `RecordList`, unions with `long` / `boolean` / `enum` discriminators + default branch
- **AnyService**: `any` roundtrip + nested any (`tk_any`) + typecode kind inspection string
- **BigPayload**: large `sequence<octet>` + large nested `Record` payload (fragment stress)

## ORBM test hint

Run server+client, then capture traffic (example):

```bash
sudo tcpdump -i lo -w complex_corba.pcap port 2809
```

Point ORBM at the produced pcap (or live capture) and the IDL folder:

- IDL dir: `examples/complex_corba/idl/`

