namespace cpp fuzzthrift

enum Kind {
	K_ZERO = 0,
	K_ONE = 1,
	K_TWO = 2
}

struct Inner {
	1: i64 id,
	2: string name,
	3: list<i32> vals
}

struct FuzzRequest {
	1: required i32 i32v,
	2: optional i64 i64v,
	3: double d,
	4: bool b,
	5: byte by,
	6: i16 i16v,
	7: string s,
	8: binary data,
	9: Kind kind,
	10: Inner inner,
	11: list<Inner> items,
	12: map<string, Inner> table,
	13: set<i64> ids,
	14: list<list<string>> nested
}

struct FuzzResponse {
	1: i32 code,
	2: string msg,
	3: list<Inner> items
}

service FuzzThrift {
	FuzzResponse Call(1: FuzzRequest req);
}
