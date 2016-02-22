#include <node.h>
#include "resampler.h"

using namespace v8;
using namespace node;

namespace resampler {

void Init(Local<Object> exports) {
  Resampler::Init(exports);
}

}

NODE_MODULE(binding, resampler::Init)
