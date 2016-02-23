#ifndef MACROS_H
#define MACROS_H

#define REQUIRE_ARGUMENTS(n)                                                   \
  if (info.Length() < (n)) {                                                   \
    return Nan::ThrowTypeError("Expected " #n "arguments");                    \
  }

#define COND_ERR_CALL_VOID(condition, callback, message, context)              \
  if (condition) {                                                             \
    if ((callback).IsEmpty() || !(callback)->IsFunction()) {                   \
      Nan::ThrowTypeError((message));                                          \
      return;                                                                  \
    }                                                                          \
    Local<Value> argv[1] = { Nan::Error((message)) };                          \
    TRY_CATCH_CALL((context), (callback), 1, argv);                            \
    return;                                                                    \
  }

#define COND_ERR_CALL(condition, callback, message)                            \
  if (condition) {                                                             \
    if ((callback).IsEmpty() || !(callback)->IsFunction())                     \
      return Nan::ThrowTypeError(message);                                     \
    Local<Value> argv[1] = { Nan::Error(message) };                            \
    TRY_CATCH_CALL(info.Holder(), (callback), 1, argv);                        \
    info.GetReturnValue().SetUndefined();                                      \
  }

#define OPTIONAL_ARGUMENT_FUNCTION(i, var)                                     \
  Local<Function> var;                                                         \
  if (info.Length() > i && !info[i]->IsUndefined()) {                          \
    if (!info[i]->IsFunction()) {                                              \
      return Nan::ThrowTypeError("Argument " #i " must be a function");        \
    }                                                                          \
    var = Local<Function>::Cast(info[i]);                                      \
  }

#define REQUIRE_ARGUMENT_FUNCTION(i, var)                                      \
  if (info.Length() <= (i) || !info[i]->IsFunction()) {                        \
    return Nan::ThrowTypeError("Argument " #i " must be a function");          \
  }                                                                            \
  Local<Function> var = Local<Function>::Cast(info[i]);

#define TRY_CATCH_CALL(context, callback, argc, argv)                          \
{   Nan::TryCatch try_catch;                                                   \
    v8::Local<v8::Function>::New(v8::Isolate::GetCurrent(), (callback))        \
      ->Call((context), (argc), (argv));                                       \
    if (try_catch.HasCaught()) {                                               \
        Nan::FatalException(try_catch);                                        \
    }                                                                          \
}

#endif
