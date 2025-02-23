// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_CPP_STARTUP_CONTEXT_H_
#define LIB_SYS_CPP_STARTUP_CONTEXT_H_

#include <memory>

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/sys/cpp/outgoing.h>
#include <lib/sys/cpp/service_directory.h>

namespace sys {

// Context information that this component received at startup.
//
// Upon creation, components are given a namespace, which is file system local
// to the component. A components namespace lets the component interact with
// other components and the system at large. One important part of this
// namespace is the directory of services, typically located at "/svc" in the
// components namespace. The |StartupContext| provides an ergonomic interface
// to this service bundle through its |svc()| property.
//
// In addition to receiving services, components can also publish services and
// data to other components through their outgoing namespace, which is also a
// directory. The |StartupContext| provides an ergonomic interface for
// services and other file system objects through its |outgoing()| property.
//
// Instances of this class are thread-safe.
//
// # Example
//
// The |StartupContext| object is typically created early in the startup
// sequence for components, typically after creating the |async::Loop| for the
// main thread.
//
// ```
// int main(int argc, const char** argv) {
//   async::Loop loop(&kAsyncLoopConfigAttachToThread);
//   auto context = sys::StartupContext::CreateFromStartupInfo();
//   my::App app(std::move(context))
//   loop.Run();
//   return 0;
// }
// ```
class StartupContext {
 public:
  // Create a startup context.
  //
  // This constructor is rarely used directly. Instead, most clients create a
  // startup context using the |CreateFromStartupInfo()| static method.
  StartupContext(std::shared_ptr<ServiceDirectory> svc,
                 zx::channel directory_request,
                 async_dispatcher_t* dispatcher = nullptr);

  // Subclasses are typically created for testing.
  virtual ~StartupContext();

  // StartupContext objects cannot be copied.
  StartupContext(const StartupContext&) = delete;
  StartupContext& operator=(const StartupContext&) = delete;

  // Creates a startup context from the process startup info.
  //
  // Call this function once during process initialization to retrieve the
  // handles supplied to the component by the component manager. This function
  // consumes some of those handles, which means subsequent calls to this
  // function will not return a functional startup context.
  //
  // Prefer creating the |StartupContext| in the |main| function for a component
  // and passing the object to any |App| class. This pattern makes testing
  // easier because tests can pass a |FakeStartupContext| to the |App| class to
  // inject dependencies.
  //
  // The returned unique_ptr is never null.
  //
  // # Example
  //
  // ```
  // int main(int argc, const char** argv) {
  //   async::Loop loop(&kAsyncLoopConfigAttachToThread);
  //   auto context = sys::StartupContext::CreateFromStartupInfo();
  //   my::App app(std::move(context))
  //   loop.Run();
  //   return 0;
  // }
  // ```
  static std::unique_ptr<StartupContext> CreateFromStartupInfo();

  // Creates a startup context from |fuchsia::sys::StartupInfo|.
  //
  // Typically used by implementations of |fuchsia::sys::Runner| to obtain the
  // |StartupContext| for components being run by the runner.
  static std::unique_ptr<StartupContext> CreateFrom(
      fuchsia::sys::StartupInfo startup_info);

  // The directory of services.
  //
  // Use this object to connect to services offered by other components.
  //
  // The directory of services is thread-safe and is commonly used on multiple
  // threads. Rather than creating a separate |ServiceDirectory| object for each
  // thread, which costs a kernel handle, consider sharing the same
  // |ServiceDirectory| using this |std::shared_ptr|.
  //
  // # Example
  //
  // ```
  // auto controller = context.svc()->Connect<fuchsia::foo::Controller>();
  // ```
  const std::shared_ptr<ServiceDirectory>& svc() const { return svc_; }

  // The outgoing namespace.
  //
  // Use this object to publish services and data to the component manager and
  // other components.
  //
  // # Example
  //
  // ```
  // class App : public fuchsia::foo::Controller {
  //  public:
  //   App(std::unique_ptr<StartupContext> context)
  //     : context_(std::move(context) {
  //     context_.outging().AddPublicService(bindings_.GetHandler(this));
  //   }
  //
  //   // fuchsia::foo::Controller implementation:
  //   [...]
  //
  //  private:
  //   fidl::BindingSet<fuchsia::foo::Controller> bindings_;
  // }
  // ```
  const Outgoing& outgoing() const { return outgoing_; }

 private:
  std::shared_ptr<ServiceDirectory> svc_;
  Outgoing outgoing_;
};

}  // namespace sys

#endif  // LIB_SYS_CPP_STARTUP_CONTEXT_H_
