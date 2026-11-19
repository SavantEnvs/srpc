// Generates the starter seeds in mayhem/<target>/testsuite with srpc's own encoders (not built by
// build.sh). Build: clang++ -std=c++17 -Ifuzz.cmake/gen -I_include -Iworkflow/_include seedgen.cc
// fuzz.cmake/gen/fuzz_pb.pb.cc _lib/libsrpc.a workflow/_lib/libworkflow.a -lprotobuf -lssl -lcrypto -lz -lpthread
// Run: ./seedgen <outdir>  (protocol seeds get a leading chunk-size byte; see fuzz_message.cc)
#include <stdio.h>
#include <string>
#include <sys/uio.h>
#include <sys/stat.h>
#include "srpc/rpc_basic.h"
#include "srpc/rpc_message_srpc.h"
#include "srpc/rpc_message_brpc.h"
#include "srpc/rpc_message_trpc.h"
#include "srpc/rpc_message_thrift.h"
#include "srpc/rpc_compress.h"
#include "srpc/rpc_buffer.h"
#include "fuzz_pb.pb.h"
#include "fuzz_thrift.thrift.h"
using namespace srpc;
static void out(const std::string& dir, const std::string& name, unsigned char prefix, const std::string& body)
{
	mkdir(dir.c_str(), 0755);
	FILE *f = fopen((dir + "/" + name).c_str(), "wb");
	fputc(prefix, f); fwrite(body.data(), 1, body.size(), f); fclose(f);
}
template<class M> struct HE : M { int encode(struct iovec *v, int max) override { return this->protocol::HttpRequest::encode(v, max); } };
template<class M> static std::string enc(M& m)
{
	struct iovec v[256]; int n = m.encode(v, 256);
	std::string s; for (int i = 0; i < n; i++) s.append((char*)v[i].iov_base, v[i].iov_len);
	return s;
}
static void fill(fuzzpb::FuzzRequest& r)
{
	r.set_i32(-7); r.set_u64(1234567890123ULL); r.set_d(3.5); r.set_f(1.25f); r.set_b(true);
	r.set_s("hello"); r.set_data(std::string("\x00\x01\xff", 3)); r.set_kind(fuzzpb::K_TWO);
	r.mutable_inner()->set_id(42); r.mutable_inner()->set_name("in"); r.mutable_inner()->add_vals(-3);
	auto *it = r.add_items(); it->set_id(1); it->add_vals(5);
	(*r.mutable_table())["k"].set_name("v"); r.set_text("choice");
}
static void tfill(fuzzthrift::FuzzRequest& r)
{
	r.i32v = 7; r.i64v = 99; r.__isset.i64v = true; r.d = 2.5; r.b = true; r.by = 3; r.i16v = -2;
	r.s = "hello"; r.data = "bin"; r.kind = fuzzthrift::Kind::K_ONE; r.inner.id = 5; r.inner.name = "in";
	r.inner.vals.push_back(1); fuzzthrift::Inner x; x.id = 9; r.items.push_back(x);
	r.table["k"] = x; r.ids.insert(77); r.nested.push_back({"a", "b"});
	r.__isset.d = r.__isset.b = r.__isset.by = r.__isset.i16v = r.__isset.s = r.__isset.data = true;
	r.__isset.kind = r.__isset.inner = r.__isset.items = r.__isset.table = r.__isset.ids = r.__isset.nested = true;
}
template<class M> static void pbreq(const std::string& dir, const char *tag, int ctype, int dtype)
{
	M m; fuzzpb::FuzzRequest r; fill(r);
	m.set_service_name("FuzzService"); m.set_method_name("Call");
	m.set_data_type(dtype); m.set_compress_type(ctype);
	if (m.serialize(&r) != RPCStatusOK) { fprintf(stderr, "ser fail %s\n", tag); return; }
	if (m.compress() != RPCStatusOK) { fprintf(stderr, "cmp fail %s\n", tag); return; }
	if (!m.serialize_meta()) { fprintf(stderr, "meta fail %s\n", tag); return; }
	std::string e = enc(m);
	out(dir, tag, 255, e); out(dir, std::string(tag) + "-chunk7", 6, e);
}
template<class M> static void pbresp(const std::string& dir, const char *tag, int ctype, int status)
{
	M m; fuzzpb::FuzzResponse r; r.set_code(3); r.set_msg("ok"); r.add_items()->set_id(4);
	m.set_compress_type(ctype); m.set_status_code(status);
	if (m.serialize(&r) != RPCStatusOK || m.compress() != RPCStatusOK || !m.serialize_meta()) { fprintf(stderr, "resp fail %s\n", tag); return; }
	out(dir, tag, 255, enc(m));
}
int main(int argc, char **argv)
{
	std::string D = argv[1];
	const char *cn[] = {"none", "snappy", "gzip", "zlib", "lz4"};
	for (int c = 0; c < 5; c++) {
		pbreq<SRPCStdRequest>(D + "/srpc_request", (std::string("pb-") + cn[c]).c_str(), c, RPCDataProtobuf);
		pbreq<SRPCStdRequest>(D + "/srpc_request", (std::string("json-") + cn[c]).c_str(), c, RPCDataJson);
		pbreq<BRPCStdRequest>(D + "/brpc_request", (std::string("pb-") + cn[c]).c_str(), c, RPCDataProtobuf);
		pbreq<TRPCStdRequest>(D + "/trpc_request", (std::string("pb-") + cn[c]).c_str(), c, RPCDataProtobuf);
		pbreq<TRPCStdRequest>(D + "/trpc_request", (std::string("json-") + cn[c]).c_str(), c, RPCDataJson);
		pbresp<TRPCStdResponse>(D + "/trpc_response", (std::string("ok-") + cn[c]).c_str(), c, RPCStatusOK);
		pbresp<BRPCStdResponse>(D + "/brpc_response", (std::string("ok-") + cn[c]).c_str(), c, RPCStatusOK);
	}
	pbreq<HE<SRPCHttpRequest>>(D + "/srpc_http_request", "json", 0, RPCDataJson);
	pbreq<HE<SRPCHttpRequest>>(D + "/srpc_http_request", "pb-gzip", 2, RPCDataProtobuf);
	pbreq<HE<TRPCHttpRequest>>(D + "/trpc_http_request", "json", 0, RPCDataJson);
	pbreq<HE<TRPCHttpRequest>>(D + "/trpc_http_request", "pb-gzip", 2, RPCDataProtobuf);
	{
		ThriftStdRequest m; fuzzthrift::FuzzThrift::CallRequest r; tfill(r.req); 
		m.set_method_name("Call");
		if (m.serialize(&r) == RPCStatusOK && m.serialize_meta()) { std::string e = enc(m); out(D + "/thrift_request", "call", 255, e); out(D + "/thrift_request", "call-chunk7", 6, e); }
		else fprintf(stderr, "thrift fail\n");
		HE<ThriftHttpRequest> h; h.set_method_name("Call");
		if (h.serialize(&r) == RPCStatusOK && h.serialize_meta()) out(D + "/thrift_http_request", "call", 255, enc(h));
		else fprintf(stderr, "thrift http fail\n");
		ThriftStdResponse s; fuzzthrift::FuzzThrift::CallResponse rr; rr.result.code = 1; rr.result.msg = "m";
		if (s.serialize(&rr) == RPCStatusOK && s.serialize_meta()) out(D + "/thrift_response", "ok", 255, enc(s));
		ThriftStdResponse e; e.set_status_code(RPCStatusMethodNotFound); 
		if (e.serialize_meta()) out(D + "/thrift_response", "exception", 255, enc(e));
	}
	std::string plain = "srpc compress seed: hello hello hello hello world 0123456789";
	for (int c = 1; c <= 4; c++) {
		std::string buf(1024, 0);
		int n = RPCCompressor::get_instance()->serialize_to_compressed(plain.data(), plain.size(), &buf[0], buf.size(), c);
		if (n > 0) { std::string b = std::string(1, (char)(c - 1)) + std::string(1, 16) + buf.substr(0, n);
			mkdir((D + "/compress").c_str(), 0755);
			FILE *g = fopen((D + "/compress/flat-" + cn[c]).c_str(), "wb"); fwrite(b.data(), 1, b.size(), g); fclose(g);
			b[0] |= 4; g = fopen((D + "/compress/iovec-" + cn[c]).c_str(), "wb"); fwrite(b.data(), 1, b.size(), g); fclose(g); }
		else fprintf(stderr, "compress fail %d\n", c);
	}
	return 0;
}
