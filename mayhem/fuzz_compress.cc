// libFuzzer harness for srpc's decompressors (RPCCompressor::parse_from_compressed), the code
// every protocol's decompress() runs on a peer's compressed body: snappy, gzip, zlib and lz4,
// in both the flat-buffer API and the RPCBuffer (iovec) API that the message classes use.
//
// Input layout: byte 0 = compress type (bits 0-1 -> 1..4) and API (bit 2: RPCBuffer);
// byte 1 = chunk size used to append the compressed bytes into the source RPCBuffer, so the
// decompressor sees the body split across several buffer pieces as it is after socket reads.
#include <stddef.h>
#include <stdint.h>
#include <vector>

#include "srpc/rpc_basic.h"
#include "srpc/rpc_buffer.h"
#include "srpc/rpc_compress.h"

using namespace srpc;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size < 2)
		return -1;

	int type = (data[0] & 3) + 1; // RPCCompressSnappy .. RPCCompressLz4
	bool iovec = data[0] & 4;
	size_t chunk = (size_t)data[1] + 1;
	const char *body = (const char *)data + 2;
	size_t len = size - 2;
	RPCCompressor *compressor = RPCCompressor::get_instance();

	if (iovec)
	{
		RPCBuffer src;
		RPCBuffer dst;
		for (size_t off = 0; off < len; off += chunk)
		{
			size_t n = len - off < chunk ? len - off : chunk;
			src.append(body + off, n, BUFFER_MODE_COPY);
		}

		if (compressor->parse_from_compressed(&src, &dst, type) > 0)
		{
			const void *p;
			size_t total = 0;
			size_t n;
			while ((n = dst.fetch(&p)) > 0)
			{
				(void)((const volatile char *)p)[n - 1];
				total += n;
			}
			(void)total;
		}
	}
	else
	{
		// Output bounded like a size-limited message body.
		std::vector<char> out(64 * 1024);
		int ret = compressor->parse_from_compressed(body, len, out.data(), out.size(), type);
		if (ret > 0 && (size_t)ret > out.size())
			__builtin_trap(); // reported more output than the buffer holds
	}

	return 0;
}
