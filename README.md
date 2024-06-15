# Pcap L7 Mutator

Pl7m is a custom mutator (used for [structure aware fuzzing](https://github.com/google/fuzzing/blob/master/docs/structure-aware-fuzzing.md)) for network traffic packet captures (i.e. pcap files).

The output of the mutator is always a valid pcap file, containing the same flows/sessions of the input file.
That's it: the mutator only changes the packet payload after the TCP/UDP header, keeping all the original L2/L3 information (IP addresses and L4 ports).

This might be useful if you are dealing with pcap files and you want to fuzz some applications at the protocol layer, for example in the deep packet inspection engines or for protocol analysis.

Mutations happens at two different levels:
* packet level: each packet might be dropped, duplicated, swapped or its own direction might be swapped (i.e. from client->server to server->client)
* payload level: only the packet payload (i.e. Layer 7 data, i.e. data after TCP/UDP header) is changed

There are some configuration options for some fine tuning: take a look at the beginning of `pl7m.c`

## How to use

Integrating pl7m mutator into your own code is very easy:
1) Copy the two files `pl7m.h` and `pl7m.c` into your own source code directory
2) Add a custum mutator into your own fuzzer, calling directly `pl7m_mutator()`

```
size_t LLVMFuzzerCustomMutator(uint8_t *Data, size_t Size,
                               size_t MaxSize, unsigned int Seed)
{
    return pl7m_mutator(Data, Size, MaxSize, Seed);
}

```

3) Compile your own fuzzer, linking to libpcap

For a complete example, see `fuzz/fuzzer_example.c`

Note that, even if the documentation/code only cites libfuzzer, you can easily use any other fuzzing library.

## Known limitations

* Better support for TCP flows: we might need to update sequence/ack numbers in the TCP header
