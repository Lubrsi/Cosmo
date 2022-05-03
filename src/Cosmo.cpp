#include "Cosmo.h"
#include <server_class.h>

#include "RemoveSourceSpecifics.h"
#include "Scripting/Command.h"
#include "Scripting/Game.h"
#include <AK/Format.h>
#include <LibCore/Stream.h>

#include <AK/JsonArray.h>
#include <AK/JsonObject.h>

namespace Cosmo
{
Plugin Plugin::s_the;
SpewOutputFunc_t Plugin::s_original_spew_output_func{};
// If a spew is using a default color (it's color is 255, 255, 255), then we use this color instead, based on it's type.
// These colors are from sys_dll.cpp
static Array<Optional<Color>, SPEW_TYPE_COUNT> s_default_spew_colors = {
    Optional<Color>(),
    Color(255, 90, 90),
    Color(255, 20, 20),
    Color(20, 70, 255),
};
PLUGIN_EXPOSE(CosmoPlugin, Plugin::s_the)

class BaseAccessor : public IConCommandBaseAccessor
{
public:
    bool RegisterConCommandBase(ConCommandBase* pCommandBase) override
    {
        /* Always call META_REGCVAR instead of going through the engine. */
        return META_REGCVAR(pCommandBase);
    }
} s_base_accessor;

JS::ThrowCompletionOr<JS::Value> Plugin::Console::printer(JS::Console::LogLevel log_level,
                                                          PrinterArguments printer_arguments)
{
    if (printer_arguments.has<AK::Vector<JS::Value>>())
    {
        // get_pointer to avoid has VERIFY check
        auto& values = *printer_arguments.get_pointer<AK::Vector<JS::Value>>();
        auto output = String::join(" ", values);

        switch (log_level)
        {
            case JS::Console::LogLevel::Debug:
                ConColorMsg({50, 170, 80}, "%s\n", output.characters());
                break;
            case JS::Console::LogLevel::Error:
            case JS::Console::LogLevel::Assert:
                ConColorMsg({255, 0, 0}, "%s\n", output.characters());
                break;
            case JS::Console::LogLevel::Info:
                ConColorMsg({170, 220, 255}, "%s\n", output.characters());
                break;
            case JS::Console::LogLevel::Log:
                Msg("%s\n", output.characters());
                break;
            case JS::Console::LogLevel::Warn:
            case JS::Console::LogLevel::CountReset:
                Warning("%s\n", output.characters());
                break;
            default:
                Msg("%s\n", output.characters());
                break;
        }
    }
    return JS::js_undefined();
}

bool Plugin::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
    PLUGIN_SAVEVARS()

    GET_V_IFACE_CURRENT(GetEngineFactory, m_cvar, ICvar, CVAR_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetServerFactory, m_server_game_dll, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
    GET_V_IFACE_CURRENT(GetEngineFactory, m_engine_server, IVEngineServer, INTERFACEVERSION_VENGINESERVER);
    GET_V_IFACE_CURRENT(GetServerFactory, m_server_tools, IServerTools, VSERVERTOOLS_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetServerFactory, m_server_game_ents, IServerGameEnts, INTERFACEVERSION_SERVERGAMEENTS);

    Msg("Loaded Cosmo\n");

#if SOURCE_ENGINE >= SE_ORANGEBOX
    g_pCVar = m_cvar;
    ConVar_Register(0, &s_base_accessor);
#else
    ConCommandBaseMgr::OneTimeInit(&s_BaseAccessor);
#endif

    // Huge props to Justin (aka. sigsegv) for this idea -- had no idea this existed in tier0.
    // We'll only turn on ANSI color if you're a dedicated server, and on POSIX.
    // Both these conditions are pretty useless though, as the nature of this plugin requires both of these :^)
    // FIXME: I'd rather not use this macro to determine POSIX, there must be some Engine function to do this!
#ifdef POSIX
    if (m_engine_server->IsDedicatedServer())
    {
        // Print this message _before_ replacing the spew func. If something goes wrong, at least we know the last thing
        // that happened.
        Msg("Running a dedicated server on posix, colorizing your spew!\n");
        s_original_spew_output_func = GetSpewOutputFunc();
        SpewOutputFunc(ansi_true_color_spew_output);
    }
#endif

    return true;
}

bool Plugin::Unload(char* error, size_t maxlen)
{
    if (s_original_spew_output_func)
    {
        SpewOutputFunc(s_original_spew_output_func);
        s_original_spew_output_func = nullptr;
    }

    auto& command_object = m_global_object.game_object().command_object();

    for (auto& kv : command_object.commands())
        g_SMAPI->UnregisterConCommandBase(g_PLAPI, kv.value.command);

    command_object.commands().clear();

    return true;
}

SpewRetval_t Plugin::ansi_true_color_spew_output(SpewType_t spew_type, const tchar* msg)
{
    // Let's NEVER touch anything meant for the log.
    if (spew_type == SPEW_LOG)
        return s_original_spew_output_func(spew_type, msg);

    // Copy it, so we can override it later if we need to
    auto output_color = GetSpewOutputColor();

    // This white color is s_DefaultOutputColor in dbg.cpp
    if (output_color.r() == 255 && output_color.g() == 255 && output_color.b() == 255)
    {
        // If the default color is used, and we don't have a spew color for it, then let's not do anything to it.
        // This will cover a ton of cases.
        auto maybe_spew_type_output_color = s_default_spew_colors[spew_type];
        if (!maybe_spew_type_output_color.has_value())
            return s_original_spew_output_func(spew_type, msg);

        output_color = maybe_spew_type_output_color.value();
    }

    // My terminal had issues with the ANSI reset being after a newline, and leaked color into the next spew. So
    // instead, let's replace the newline with our reset, and then re-add the newline.

    // All this string copying is pretty unfortunate :(
    StringView msg_view(msg);
    StringBuilder corrected_message_builder;
    corrected_message_builder.appendff("\u001b[38;2;{};{};{}m", output_color.r(), output_color.g(), output_color.b());

    // This will be 99% of spew, so try to put the ANSI color reset BEFORE the newline
    if (msg_view.ends_with('\n'))
    {
        corrected_message_builder.append(msg_view.substring_view(0, msg_view.length() - 1));
        corrected_message_builder.append("\u001B[0m");
        corrected_message_builder.append('\n');
    }
    else
    {
        corrected_message_builder.appendff(msg_view);
        corrected_message_builder.append("\u001B[0m");
    }

    return s_original_spew_output_func(spew_type, corrected_message_builder.to_string().characters());
}

CON_COMMAND(cosmo_run, "Run a script")
{
    if (args.ArgC() < 2)
        return;

    // FIXME: It would be nice if we could print all the Errors here. There's a formatter for it, not too hard.
    auto maybe_file_stream = Core::Stream::File::open(args.Arg(1), Core::Stream::OpenMode::Read);
    if (maybe_file_stream.is_error())
    {
        Warning("Unable to open script %s\n", args.Arg(1));
        return;
    }

    auto file_stream = maybe_file_stream.release_value();
    auto maybe_file_stream_size = file_stream->size();
    if (maybe_file_stream_size.is_error())
    {
        Warning("Failed to get script file size\n");
        return;
    }

    auto maybe_file_contents_buffer = ByteBuffer::create_uninitialized(maybe_file_stream_size.value());
    if (maybe_file_contents_buffer.is_error())
    {
        Warning("Unable to allocate buffer for script file contents\n");
        return;
    }

    auto maybe_file_contents_bytes = file_stream->read(maybe_file_contents_buffer.value());
    if (maybe_file_contents_bytes.is_error())
    {
        Warning("Unable to read script file contents\n");
        return;
    }
    auto file_contents_string = StringView(maybe_file_contents_bytes.value());

    auto& interpreter = Plugin::the().interpreter();
    auto maybe_script = JS::Script::parse(file_contents_string, interpreter.realm());
    if (maybe_script.is_error())
    {
        Warning("Script parsing failed:\n");
        for (auto& error : maybe_script.error())
            Warning("\t%s\n", error.message.characters());
    }
    else
    {
        auto maybe_return_value = interpreter.run(maybe_script.value());
        if (maybe_return_value.is_error())
        {
            VERIFY(maybe_return_value.throw_completion().value().has_value());
            Warning("Script execution threw an exception: %s\n",
                    MUST(maybe_return_value.throw_completion().value()->to_string(Plugin::the().global_object()))
                        .characters());
        }
    }
}

void find_and_add_children(HashMap<String, SendTable*>& send_tables, SendTable* send_table)
{
    for (auto i = 0; i < send_table->GetNumProps(); i++)
    {
        auto* prop = send_table->GetProp(i);
        if (prop->GetType() == DPT_DataTable)
        {
            auto* data_table = prop->GetDataTable();
            if (data_table)
            {
                send_tables.set(data_table->GetName(), data_table);
                find_and_add_children(send_tables, data_table);
            }
        }
    }
}

CON_COMMAND(cosmo_dump, "Dump some things (server classes, data tables)")
{
    // Let's try to open the file first, so we can avoid doing useless work if we can't even output it.
    auto maybe_output_file = Core::Stream::File::open("dump.json", Core::Stream::OpenMode::Write);
    if (maybe_output_file.is_error())
    {
        Warning("Failed to open dump.json for writing\n");
        return;
    }

    auto output_file = maybe_output_file.release_value();

    JsonObject output;
    HashMap<String, SendTable*> send_tables_found;

    JsonObject server_classes;
    auto* server_class = Plugin::the().server_game_dll().GetAllServerClasses();
    do
    {
        JsonObject server_class_json;
        server_class_json.set("ClassID", server_class->m_ClassID);
        server_class_json.set("InstanceBaselineIndex", server_class->m_InstanceBaselineIndex);

        auto* send_table = server_class->m_pTable;
        if (send_table)
        {
            server_class_json.set("DataTable", send_table->GetName());
            send_tables_found.set(send_table->GetName(), send_table);
            // Let's find all it's child DataTables now, instead of whilst we're iterating (or iterating a second time)
            find_and_add_children(send_tables_found, send_table);
        }

        server_classes.set(server_class->m_pNetworkName, move(server_class_json));
    } while ((server_class = server_class->m_pNext));

    JsonObject data_tables;
    for (auto& kv : send_tables_found)
    {
        JsonObject send_table;

        for (auto i = 0; i < kv.value->GetNumProps(); i++)
        {
            auto* prop = kv.value->GetProp(i);
            JsonObject send_table_property;

            send_table_property.set("Type", prop->GetType());
            send_table_property.set("Flags", prop->GetFlags());
            send_table_property.set("Offset", prop->GetOffset());
            if (prop->GetDataTable())
                send_table_property.set("DataTable", prop->GetDataTable()->GetName());

            send_table.set(prop->GetName(), move(send_table_property));
        }

        data_tables.set(kv.key, move(send_table));
    }

    output.set("ServerClasses", server_classes);
    output.set("DataTables", data_tables);
    if (output_file->write(output.to_string().bytes()).is_error())
    {
        Warning("Failed to write to dump.json\n");
        return;
    }

    Msg("Wrote dump to dump.json\n");
}
}