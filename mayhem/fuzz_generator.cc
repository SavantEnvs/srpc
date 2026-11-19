// libFuzzer harness for srpc_generator, the IDL compiler (`srpc_generator thrift|protobuf
// <idl_file> <output_dir>`, src/generator/compiler.cc). The input is the IDL file: it is
// written to a fresh scratch directory under $TMPDIR as `fuzz.thrift` (GEN_THRIFT=1) or
// `fuzz.proto`, and Generator::generate parses it and writes the generated headers and
// skeletons into a fresh output directory, as the CLI does. Both directories are removed
// after every input. The one process-global the generator keeps is SGenUtil::get_enum_set()
// (src/generator/descriptor.h): Parser adds every enum it parses (parser.cc) and later lookups
// read it back, and nothing upstream clears it, so it is cleared before each input. With that,
// nothing carries over between inputs.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ftw.h>
#include <sys/stat.h>
#include <string>

#include "generator.h"

static int rm_entry(const char *path, const struct stat *, int, struct FTW *)
{
	remove(path);
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	const char *tmp = getenv("TMPDIR");
	std::string base = std::string(tmp && *tmp ? tmp : "/tmp") + "/srpcgen.XXXXXX";
	if (!mkdtemp(&base[0]))
		return 0;

	std::string in_dir = base + "/in/";
	std::string out_dir = base + "/out";
	// The CLI passes the IDL file name relative to input_dir (compiler.cc parse_prefix_dir).
	std::string idl_name = GEN_THRIFT ? "fuzz.thrift" : "fuzz.proto";
	std::string idl = in_dir + idl_name;

	if (mkdir(in_dir.c_str(), 0700) == 0 && mkdir(out_dir.c_str(), 0700) == 0)
	{
		FILE *fp = fopen(idl.c_str(), "wb");
		if (fp)
		{
			bool ok = fwrite(data, 1, size, fp) == size;
			fclose(fp);
			if (ok)
			{
				struct GeneratorParams params;
				params.out_dir = out_dir.c_str();
				params.idl_file = idl_name;
				params.input_dir = in_dir;
				SGenUtil::get_enum_set()->clear();
				Generator gen(GEN_THRIFT ? true : false);
				gen.generate(params);
			}
		}
	}

	nftw(base.c_str(), rm_entry, 16, FTW_DEPTH | FTW_PHYS);
	return 0;
}
