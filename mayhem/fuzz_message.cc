// libFuzzer harness for srpc's wire-protocol message parsers.
//
// One binary per message class (MSG_KIND, set by mayhem/build.sh). The input is the raw byte
// stream a peer sends over the connection. It is delivered to the message's append() in chunks,
// as workflow's Communicator does for socket reads, and then processed in the order
// RPCServer::server_process (server side) / RPCClientTask (client side) use:
//   append() == 1 -> deserialize_meta() -> service/method or status -> decompress()
//   -> get_meta_module_data() -> get_attachment_nocopy() -> deserialize(idl message).
// The first input byte selects the chunk size (1..256 bytes) so the fuzzer also explores how
// a message split across reads is reassembled. Nothing is kept across inputs: every input
// gets a fresh message object.
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <type_traits>

#include "srpc/rpc_basic.h"
#include "srpc/rpc_message_srpc.h"
#include "srpc/rpc_message_brpc.h"
#include "srpc/rpc_message_trpc.h"
#include "srpc/rpc_message_thrift.h"
#include "srpc/rpc_module.h"
#include "fuzz_pb.pb.h"
#include "fuzz_thrift.thrift.h"

using namespace srpc;

// Std (TCP) messages: append() is the public ProtocolMessage entry the Communicator calls.
template<class M> struct Std : M
{
	int feed(const void *buf, size_t *n) { return this->append(buf, n); }
};

// HTTP messages: protocol::HttpRequest/HttpResponse::append is the parser the Communicator
// calls (the plain name is ambiguous with the SRPC message's own append). HttpMessage::append
// may answer the peer through feedback() (100-continue, 413, 400); there is no connection in
// this harness, so the reply is accepted and dropped, as a socket would.
template<class M, class H> struct Http : M
{
	int feed(const void *buf, size_t *n) { return this->H::append(buf, n); }
	int feedback(const void *buf, size_t size) override { return (int)size; }
};

using PbReq = fuzzpb::FuzzRequest;
using PbResp = fuzzpb::FuzzResponse;
// Thrift call arguments / result of `FuzzResponse Call(1: FuzzRequest req)` (mayhem/idl).
using ThriftReq = fuzzthrift::FuzzThrift::CallRequest;
using ThriftResp = fuzzthrift::FuzzThrift::CallResponse;

template<class M, class IDL>
static void run(const uint8_t *data, size_t size)
{
	size_t chunk = (size_t)data[0] + 1;
	data++;
	size--;

	M msg;
	// The limit RPCServer applies by default (RPC_BODY_SIZE_LIMIT is 2GB there; bounded here
	// so a length field cannot make one input allocate gigabytes before parsing starts).
	msg.set_size_limit(1 << 20);

	int ret = 0;
	size_t off = 0;
	while (off < size && ret == 0)
	{
		size_t n = size - off < chunk ? size - off : chunk;
		ret = msg.feed(data + off, &n);
		off += n;
		if (n == 0 && ret == 0)
			break;
	}

	if (ret != 1)
		return;

	if (!msg.deserialize_meta())
		return;

	if constexpr (std::is_base_of_v<RPCRequest, M>)
	{
		(void)msg.get_service_name().size();
		(void)msg.get_method_name().size();
	}
	else
	{
		(void)msg.get_status_code();
		(void)msg.get_error();
		const char *err = msg.get_errmsg();
		if (err)
			(void)strlen(err);
	}

	if constexpr (std::is_base_of_v<TRPCRequest, M>)
	{
		(void)msg.get_caller_name().size();
		msg.trim_method_prefix();
	}

	if (msg.decompress() != RPCStatusOK)
		return;

	RPCModuleData module_data;
	msg.get_meta_module_data(module_data);

	const char *attachment;
	size_t attachment_len;
	if (msg.get_attachment_nocopy(&attachment, &attachment_len) && attachment_len)
		(void)((volatile const char *)attachment)[attachment_len - 1];

	IDL idl;
	msg.deserialize(&idl);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size < 1)
		return -1;

#if MSG_KIND == 1
	run<Std<SRPCStdRequest>, PbReq>(data, size);
#elif MSG_KIND == 2
	run<Std<BRPCStdRequest>, PbReq>(data, size);
#elif MSG_KIND == 3
	run<Std<TRPCStdRequest>, PbReq>(data, size);
#elif MSG_KIND == 4
	run<Std<ThriftStdRequest>, ThriftReq>(data, size);
#elif MSG_KIND == 5
	run<Http<SRPCHttpRequest, protocol::HttpRequest>, PbReq>(data, size);
#elif MSG_KIND == 6
	run<Http<TRPCHttpRequest, protocol::HttpRequest>, PbReq>(data, size);
#elif MSG_KIND == 7
	run<Http<ThriftHttpRequest, protocol::HttpRequest>, ThriftReq>(data, size);
#elif MSG_KIND == 8
	run<Std<TRPCStdResponse>, PbResp>(data, size);
#elif MSG_KIND == 9
	run<Std<BRPCStdResponse>, PbResp>(data, size);
#elif MSG_KIND == 10
	run<Std<ThriftStdResponse>, ThriftResp>(data, size);
#else
#error "MSG_KIND must be 1..10"
#endif
	return 0;
}
