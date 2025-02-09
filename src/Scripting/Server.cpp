#include "../Cosmo.h"

#include "../RemoveSourceSpecifics.h"

#include "Entity.h"
#include "Server.h"
#include <LibJS/Runtime/GlobalObject.h>

namespace Cosmo::Scripting
{
Server::Server(GlobalObject& global_object) : Object(*global_object.object_prototype()) {}

void Server::initialize(JS::GlobalObject& global_object)
{
    Object::initialize(global_object);

    define_native_function("createEntityByName", create_entity_by_name, 1, 0);
    define_native_function("createFakeClient", create_fake_client, 1, 0);
    define_native_function("getEntityByIndex", get_entity_by_index, 1, 0);
}

JS_DEFINE_NATIVE_FUNCTION(Server::create_entity_by_name)
{
    auto entity_name = vm.argument(0);
    if (!entity_name.is_string())
        return vm.throw_completion<JS::TypeError>(global_object, JS::ErrorType::NotAString, entity_name);

    auto* entity = Plugin::the().server_tools().CreateEntityByName(entity_name.as_string().string().characters());
    if (!entity)
        return JS::js_undefined();

    return Entity::create(verify_cast<GlobalObject>(global_object), entity);
}

JS_DEFINE_NATIVE_FUNCTION(Server::create_fake_client)
{
    auto client_name = vm.argument(0);
    if (!client_name.is_string())
        return vm.throw_completion<JS::TypeError>(global_object, JS::ErrorType::NotAString, client_name);

    auto* entity = Plugin::the().server_game_ents().EdictToBaseEntity(
        Plugin::the().engine_server().CreateFakeClient(client_name.as_string().string().characters()));
    if (!entity)
        return JS::js_undefined();

    return Entity::create(verify_cast<GlobalObject>(global_object), entity);
}

JS_DEFINE_NATIVE_FUNCTION(Server::get_entity_by_index)
{
    auto index = vm.argument(0);
    if (!index.is_number())
        return vm.throw_completion<JS::TypeError>(global_object, String::formatted("{} is not a number", index));

    auto* maybe_edict = Plugin::the().engine_server().PEntityOfEntIndex(index.as_i32());
    if (!maybe_edict)
        return JS::js_undefined();

    auto* maybe_entity = Plugin::the().server_game_ents().EdictToBaseEntity(maybe_edict);
    if (!maybe_entity)
        return JS::js_undefined();

    return Entity::create(verify_cast<GlobalObject>(global_object), maybe_entity);
}
}