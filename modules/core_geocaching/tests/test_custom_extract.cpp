#include "chat/infra/lxmf/lxmf_wire.h"
#include <cassert>

int main()
{
    using namespace chat::lxmf;
    DecodedTextPayload payload;
    ByteSpan type, data;
    assert(extractCustomData(payload, &type, &data) == CustomDataResult::NotCustom);
    payload.fields.push_back({kFieldCustomType, {0xa1, 'x'}});
    assert(extractCustomData(payload, &type, &data) == CustomDataResult::Invalid);
    payload.fields.push_back({kFieldCustomData, {0xc4, 1, 42}});
    assert(extractCustomData(payload, &type, &data) == CustomDataResult::Valid);
    assert(type.size == 1 && data.size == 1 && data.data[0] == 42);
    payload.fields.push_back(payload.fields[0]);
    assert(extractCustomData(payload, &type, &data) == CustomDataResult::Invalid);
    assert(!type.data && !data.data);
    payload.fields.pop_back();
    payload.fields[1].encoded_value = {0xa1, 'x'};
    assert(extractCustomData(payload, &type, &data) == CustomDataResult::Invalid);
}
