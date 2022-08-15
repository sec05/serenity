/*
 * Copyright (c) 2021, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/AsyncFunctionDriverWrapper.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/NativeFunction.h>
#include <LibJS/Runtime/PromiseReaction.h>
#include <LibJS/Runtime/VM.h>

namespace JS {

ThrowCompletionOr<Value> AsyncFunctionDriverWrapper::create(Realm& realm, GeneratorObject* generator_object)
{
    auto wrapper = realm.heap().allocate<AsyncFunctionDriverWrapper>(realm, realm, generator_object);
    return wrapper->react_to_async_task_completion(realm.vm(), realm.global_object(), js_undefined(), true);
}

AsyncFunctionDriverWrapper::AsyncFunctionDriverWrapper(Realm& realm, GeneratorObject* generator_object)
    : Promise(*realm.global_object().promise_prototype())
    , m_generator_object(generator_object)
    , m_on_fulfillment(NativeFunction::create(realm, "async.on_fulfillment"sv, [this](VM& vm, GlobalObject& global_object) {
        return react_to_async_task_completion(vm, global_object, vm.argument(0), true);
    }))
    , m_on_rejection(NativeFunction::create(realm, "async.on_rejection"sv, [this](VM& vm, GlobalObject& global_object) {
        return react_to_async_task_completion(vm, global_object, vm.argument(0), false);
    }))
{
}

ThrowCompletionOr<Value> AsyncFunctionDriverWrapper::react_to_async_task_completion(VM& vm, GlobalObject& global_object, Value value, bool is_successful)
{
    auto& realm = *global_object.associated_realm();

    auto generator_result = is_successful
        ? m_generator_object->next_impl(vm, global_object, value, {})
        : m_generator_object->next_impl(vm, global_object, {}, value);

    if (generator_result.is_throw_completion()) {
        VERIFY(generator_result.throw_completion().type() == Completion::Type::Throw);
        auto promise = Promise::create(realm);
        promise->reject(*generator_result.throw_completion().value());
        return promise;
    }

    auto result = generator_result.release_value();
    VERIFY(result.is_object());

    auto promise_value = TRY(result.get(global_object, vm.names.value));
    if (!promise_value.is_object() || !is<Promise>(promise_value.as_object())) {
        auto promise = Promise::create(realm);
        promise->fulfill(promise_value);
        return promise;
    }

    auto* promise = static_cast<Promise*>(&promise_value.as_object());
    if (TRY(result.get(global_object, vm.names.done)).to_boolean())
        return promise;

    return promise->perform_then(m_on_fulfillment, m_on_rejection, PromiseCapability { promise, m_on_fulfillment, m_on_rejection });
}

void AsyncFunctionDriverWrapper::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_generator_object);
    visitor.visit(m_on_fulfillment);
    visitor.visit(m_on_rejection);
}

}
