#include <v8.h>
#include <node.h>
#include <node_object_wrap.h>
#include <nan.h>
#include <uv.h>
#include <libresample.h>
#include "macros.h"

#define RS_SAMPLE_BYTES 4
#define RS_BUFFER_SAMPLES 1024
#define RS_BUFFER_BYTES 4096
#define RS_BUFFER_PAD 1024

using namespace v8;
using namespace node;
/*
*/

/*
class ResamplerWorker : public AsyncWorker {
  public:
    ResamplerWorker(Resampler *rs, Callback *callback, char* inBuffer, int inBufferLength, char* prefix_, int prefixLength) : AsyncWorker(callback), 
};
*/

class Resampler : public Nan::ObjectWrap {
public:
  static NAN_MODULE_INIT(Init) {
    Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);
    tpl->SetClassName(Nan::New("Resampler").ToLocalChecked());
    tpl->InstanceTemplate()->SetInternalFieldCount(1);

    tpl->PrototypeTemplate()->Set(Nan::New<String>("open").ToLocalChecked(),
        Nan::New<FunctionTemplate>(Open)->GetFunction());
    tpl->PrototypeTemplate()->Set(Nan::New<String>("close").ToLocalChecked(),
        Nan::New<FunctionTemplate>(Close)->GetFunction());
    tpl->PrototypeTemplate()->Set(Nan::New<String>("resample").ToLocalChecked(),
        Nan::New<FunctionTemplate>(Resample)->GetFunction());
    tpl->PrototypeTemplate()->Set(Nan::New<String>("flush").ToLocalChecked(),
        Nan::New<FunctionTemplate>(Flush)->GetFunction());

    //tpl->PrototypeTemplate()->SetAccessor(Nan::New<String>("opened"), OpenedGetter);

    //constructor().Reset(Nan::GetFunction(tpl).ToLocalChecked());
    Nan::Set(target, Nan::New("Resampler").ToLocalChecked(), Nan::GetFunction(tpl).ToLocalChecked());
  }

protected:
  Resampler() : ObjectWrap(),
    resampleHandle(NULL),
    opened(false),
    resampling(false),
    flushing(false),
    closing(false),
    factor(1),
    quality(1),
    leftovers(NULL),
    leftoversLength(0) {
  }

  ~Resampler() {
    resampleHandle = NULL;
    opened = false;
    resampling = false;
    flushing = false;
    closing = false;
    factor = 1;
    quality = 1;
    leftovers = NULL;
    leftoversLength = 0;
  }

  struct Baton {
    uv_work_t request;      // Work request
    Resampler* rs;   // Resampler instance to work on
    Nan::Callback callback;

    Baton(Resampler* rs_, Handle<Function> cb_) : rs(rs_) {
      rs->Ref();
      request.data = this;
      callback.SetFunction(cb_);
    }
    virtual ~Baton() {
      rs->Unref();
      //callback.Reset();
    }
  };

  struct ResampleBaton : Baton {
    char* inPtr;
    size_t inLength;

    Nan::Persistent<v8::Object> outBuffer;
    char* outPtr;
    size_t outLength;

    char* prefix;
    size_t prefixLength;

    ResampleBaton(Resampler* rs_, Handle<Function> cb_, char* inBuffer_, int inBufferLength_, char* prefix_, int prefixLength_) :
        Baton(rs_, cb_), inPtr(NULL), inLength(0), outPtr(NULL), outLength(0), prefix(NULL), prefixLength(0) {
      Nan::HandleScope scope;

      if (inBufferLength_ > 0 && inBuffer_ != NULL) {
        // Persist the input buffer and save pointer
        inLength = inBufferLength_;
        inPtr = (char*)malloc(inLength);
        memcpy(inPtr, inBuffer_, inLength);
      }

      if (prefixLength_ > 0 && prefix_ != NULL) {
        // Copy prefix
        prefixLength = prefixLength_;
        prefix = (char*)malloc(prefixLength);
        memcpy(prefix, prefix_, prefixLength);
      }

      // Calculate total length of input
      size_t totalLength = inLength + prefixLength;

      if (totalLength > 0) {
        // Create outBuffer based on factor + pad and save pointer
        outLength = totalLength * rs->factor + RS_BUFFER_PAD;
        v8::Local<v8::Object> localOutBuffer = Nan::NewBuffer(outLength).ToLocalChecked();
        outBuffer = Nan::Persistent<v8::Object>(localOutBuffer);
        outPtr = Buffer::Data(localOutBuffer);
      }
    }
    virtual ~ResampleBaton() {
      if (inPtr != NULL) free(inPtr);
      if (outPtr != NULL) outBuffer.Reset();
      if (prefix != NULL) free(prefix);
    }
  };

  struct FlushBaton : Baton {
    v8::Persistent<v8::Object, v8::NonCopyablePersistentTraits<v8::Object>> outBuffer;
    char* outPtr;
    size_t outLength;

    FlushBaton(Resampler* rs_, v8::Handle<v8::Function> cb_) :
        Baton(rs_, cb_), outPtr(NULL), outLength(0) {
      Nan::HandleScope scope;

      // Create outBuffer and save pointer
      outLength = rs->factor * RS_BUFFER_PAD;

      v8::Local<v8::Object> localOutBuffer = Nan::NewBuffer(outLength).ToLocalChecked();
      outBuffer = v8::Persistent<v8::Object, v8::NonCopyablePersistentTraits<v8::Object>>(v8::Isolate::GetCurrent(), localOutBuffer);
      outPtr = Buffer::Data(localOutBuffer);
    }
    virtual ~FlushBaton() {
      if (outPtr != NULL) outBuffer.Reset();
    }
  };

  static NAN_METHOD(New) {
    if (!info.IsConstructCall())
      return Nan::ThrowTypeError("Use the new operator");

    REQUIRE_ARGUMENTS(3);

    Resampler* rs = new Resampler();
    rs->Wrap(info.This());

    rs->factor = info[1]->NumberValue() / info[0]->NumberValue();
    rs->quality = info[2]->Int32Value();

    info.GetReturnValue().Set(info.This());
  }

  static NAN_METHOD(Open) {
    OPTIONAL_ARGUMENT_FUNCTION(0, callback);

    Resampler* rs = ObjectWrap::Unwrap<Resampler>(info.Holder());

    COND_ERR_CALL(rs->opened, callback, "Already open");
    COND_ERR_CALL(rs->closing, callback, "Still closing");

    rs->resampleHandle = resample_open(rs->quality, rs->factor, rs->factor);
    COND_ERR_CALL(rs->resampleHandle == 0, callback, "Couldn't open");
    rs->opened = true;

    if (!callback.IsEmpty() && callback->IsFunction())  {
      Local<Value> argv[0] = { };
      TRY_CATCH_CALL(info.Holder(), callback, 0, argv);
    }

    info.GetReturnValue().Set(info.Holder());
  }

  static NAN_METHOD(Close) {
    OPTIONAL_ARGUMENT_FUNCTION(0, callback);

    Resampler* rs = ObjectWrap::Unwrap<Resampler>(info.Holder());

    COND_ERR_CALL(!rs->opened, callback, "Not open");
    COND_ERR_CALL(rs->resampling, callback, "Still resampling");
    COND_ERR_CALL(rs->flushing, callback, "Still flushing");
    COND_ERR_CALL(rs->closing, callback, "Still closing");

    rs->closing = true;
    resample_close(rs->resampleHandle);
    rs->opened = false;
    rs->closing = false;

    if (!callback.IsEmpty() && callback->IsFunction())  {
      Local<Value> argv[0] = { };
      TRY_CATCH_CALL(info.Holder(), callback, 0, argv);
    }

    info.GetReturnValue().Set(info.Holder());
  }

  static NAN_METHOD(Resample) {
    REQUIRE_ARGUMENTS(2);
    REQUIRE_ARGUMENT_FUNCTION(1, callback);

    Resampler* rs = ObjectWrap::Unwrap<Resampler>(info.Holder());

    COND_ERR_CALL(!rs->opened, callback, "Not open");
    COND_ERR_CALL(rs->resampling, callback, "Already resampling");
    COND_ERR_CALL(!Buffer::HasInstance(info[0]), callback, "First arg must be a buffer");

    // Initialize leftovers if we haven't yet.
    if (rs->leftovers == NULL) {
      rs->leftovers = (char*)malloc(RS_SAMPLE_BYTES);
      rs->leftoversLength = 0;
    }

    char* chunkPtr = Buffer::Data(info[0]);
    size_t chunkLength = Buffer::Length(info[0]);

    int totalLength = chunkLength + rs->leftoversLength;
    int totalSamples = totalLength / RS_SAMPLE_BYTES;

    ResampleBaton* baton;

    if (totalSamples < 0) {
      // Copy entire chunk into leftovers
      memcpy(rs->leftovers + rs->leftoversLength, chunkPtr, chunkLength);
      rs->leftovers += chunkLength;
      baton = new ResampleBaton(rs, callback, chunkPtr, chunkLength, NULL, 0);
    } else {
      uint32_t newLeftoversLength = totalLength % RS_SAMPLE_BYTES;
      uint32_t inBufferLength = totalLength - rs->leftoversLength - newLeftoversLength;

      // Slice buffer to correct length
      //if (inBufferLength > 0) {
      //  Local<Function> slice = Local<Function>::Cast(args[0]->ToObject()->Get(Nan::New<String>("slice")));
      //  Local<Value> sliceArgs[2] = { Nan::New<Integer>(0), Nan::New<Integer>(inBufferLength) };
      //  inBuffer = slice->Call(args[0]->ToObject(), 2, sliceArgs)->ToObject();
      //}

      // Create baton and let it memcpy leftovers
      baton = new ResampleBaton(rs, callback, chunkPtr, inBufferLength, rs->leftovers, rs->leftoversLength);

      // Copy new leftovers
      if (newLeftoversLength > 0) memcpy(rs->leftovers, (chunkPtr + chunkLength) - newLeftoversLength, newLeftoversLength);
      rs->leftoversLength = newLeftoversLength;
    }

    rs->resampling = true;
    BeginResample(baton);

    info.GetReturnValue().Set(info.Holder());
  }

  static NAN_METHOD(Flush) {
    REQUIRE_ARGUMENTS(1);
    REQUIRE_ARGUMENT_FUNCTION(0, callback);

    Resampler* rs = ObjectWrap::Unwrap<Resampler>(info.Holder());

    COND_ERR_CALL(!rs->opened, callback, "Not open");
    COND_ERR_CALL(rs->resampling, callback, "Still resampling");
    COND_ERR_CALL(rs->flushing, callback, "Already flushing");

    rs->flushing = true;
    FlushBaton* baton = new FlushBaton(rs, callback);
    BeginFlush(baton);

    info.GetReturnValue().Set(info.Holder());
  }

  static NAN_GETTER(OpenedGetter) {
    Resampler* rs = ObjectWrap::Unwrap<Resampler>(info.Holder());
    info.GetReturnValue().Set(Nan::New<v8::Boolean>(rs->opened));
  }

  static void BeginResample(Baton* baton) {
    uv_queue_work(uv_default_loop(), &baton->request, DoResample, (uv_after_work_cb)AfterResample);
  }

  static void DoResample(uv_work_t* req) {
    Nan::HandleScope scope;

    ResampleBaton* baton = static_cast<ResampleBaton*>(req->data);
    Resampler* rs = baton->rs;

    // Write prefix
    if (baton->prefix != NULL) {
      size_t inBytes = RS_SAMPLE_BYTES - (baton->prefixLength % RS_SAMPLE_BYTES);
      size_t tmpBufferLength = baton->prefixLength + inBytes;
      char* tmpBuffer = (char*)malloc(tmpBufferLength);
      memcpy(tmpBuffer, baton->prefix, baton->prefixLength);

      if (inBytes > 0) {
        memcpy(tmpBuffer + baton->prefixLength, baton->inPtr, inBytes);
        baton->inPtr += inBytes;
        baton->inLength -= inBytes;
      }

      size_t tmpBufferUsed = 0;
      while (tmpBufferUsed < tmpBufferLength) {
        int samplesUsed;
        int result = resample_process(rs->resampleHandle, rs->factor,
          static_cast<float*>(static_cast<void*>(tmpBuffer + tmpBufferUsed)),
          ((tmpBufferLength - tmpBufferUsed) / RS_SAMPLE_BYTES), 0,
          &samplesUsed,
          static_cast<float*>(static_cast<void*>(baton->outPtr)),
          (baton->outLength / RS_SAMPLE_BYTES));

        baton->outPtr += result * RS_SAMPLE_BYTES;
        baton->outLength -= result * RS_SAMPLE_BYTES;
        tmpBufferUsed += samplesUsed * RS_SAMPLE_BYTES;
      }
      free(tmpBuffer);
    }

    // Do resample operating on inPtr
    while (baton->inLength > 0) {
      int samplesUsed = 0;
      int result = resample_process(rs->resampleHandle, rs->factor,
        static_cast<float*>(static_cast<void*>(baton->inPtr)),
        (baton->inLength / RS_SAMPLE_BYTES), 0,
        &samplesUsed,
        static_cast<float*>(static_cast<void*>(baton->outPtr)),
        (baton->outLength / RS_SAMPLE_BYTES));

      baton->inPtr += samplesUsed * RS_SAMPLE_BYTES;
      baton->inLength -= samplesUsed * RS_SAMPLE_BYTES;
      baton->outPtr += result * RS_SAMPLE_BYTES;
      baton->outLength -= result * RS_SAMPLE_BYTES;
    }
  }

  static void AfterResample(uv_work_t* req) {
    Nan::HandleScope scope;

    ResampleBaton* baton = static_cast<ResampleBaton*>(req->data);
    Resampler* rs = baton->rs;

    v8::Local<v8::Object> outBuffer;

    if (baton->outPtr != NULL) {
      //Local<Function> slice = Local<Function>::Cast(baton->outBuffer->Get(Nan::New<String>("slice")));
      //Local<Value> sliceArgs[2] = { Nan::New<Integer>(0), Nan::New<Integer>(origOutLength - baton->outLength) };
      //outBuffer = slice->Call(baton->outBuffer, 2, sliceArgs)->ToObject();
      v8::Local<v8::Object> batonOutBuffer = Nan::New(baton->outBuffer);
      size_t outLength = node::Buffer::Length(batonOutBuffer) - baton->outLength;
      char* data = node::Buffer::Data(batonOutBuffer);
      outBuffer = Nan::CopyBuffer(data, outLength).ToLocalChecked();
    } else {
      outBuffer = Nan::NewBuffer(0).ToLocalChecked();
    }

    Local<Value> argv[2] = { Nan::Null(), outBuffer };

    rs->resampling = false;
    TRY_CATCH_CALL(rs->handle(), baton->callback.GetFunction(), 2, argv);
    delete baton;
  }

  static void BeginFlush(Baton* baton) {
    uv_queue_work(uv_default_loop(), &baton->request, DoFlush, (uv_after_work_cb)AfterFlush);
  }

  static void DoFlush(uv_work_t* req) {
    Nan::HandleScope scope;

    FlushBaton* baton = static_cast<FlushBaton*>(req->data);
    Resampler* rs = baton->rs;

    int samplesUsed = 0;
    int result = resample_process(rs->resampleHandle, rs->factor,
      NULL, 0, 1,
      &samplesUsed,
      static_cast<float*>(static_cast<void*>(baton->outPtr)),
      (baton->outLength / RS_SAMPLE_BYTES));

    baton->outPtr += result * RS_SAMPLE_BYTES;
    baton->outLength -= result * RS_SAMPLE_BYTES;
  }

  static void AfterFlush(uv_work_t* req) {
    Nan::HandleScope scope;

    FlushBaton* baton = static_cast<FlushBaton*>(req->data);
    Resampler* rs = baton->rs;

    //Local<Function> slice = Local<Function>::Cast(baton->outBuffer->Get(Nan::New<String>("slice")));
    //Local<Value> sliceArgs[2] = { Nan::New<Integer>(0), Nan::New<Integer>(origOutLength - baton->outLength) };
    //Local<Object> outBuffer = slice->Call(baton->outBuffer, 2, sliceArgs)->ToObject();

    v8::Local<v8::Object> batonOutBuffer = Nan::New(baton->outBuffer);
    char* origBufferData = node::Buffer::Data(batonOutBuffer);
    size_t origOutLength = node::Buffer::Length(batonOutBuffer) - baton->outLength;
    v8::Local<v8::Object> outBuffer = Nan::CopyBuffer(origBufferData, origOutLength).ToLocalChecked();
    v8::Local<v8::Value> argv[2] = { Nan::Null(), outBuffer };

    rs->flushing = false;
    TRY_CATCH_CALL(rs->handle(), baton->callback.GetFunction(), 2, argv);
    delete baton;
  }

  void* resampleHandle;
  bool opened;
  bool resampling;
  bool flushing;
  bool closing;
  double factor;
  int quality;
  char* leftovers;
  int leftoversLength;
};

void NodeInit(Handle<Object> exports) {
  Resampler::Init(exports);
}

NODE_MODULE(resampler, NodeInit)
