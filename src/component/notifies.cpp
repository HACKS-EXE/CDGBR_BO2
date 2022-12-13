#include <stdinc.hpp>
#include "loader/component_loader.hpp"

#include "gsc.hpp"

#include <utils/io.hpp>
#include <utils/hook.hpp>
#include <utils/concurrency.hpp>

namespace notifies
{
	namespace
	{
        struct notify_group
        {
            unsigned int owner_id;
            unsigned int notify_target;
            unsigned int main_notify;
            std::unordered_set<unsigned int> sub_notifies;
        };

        using notify_groups_t = std::vector<notify_group>;
        utils::concurrency::container<notify_groups_t> notify_groups_queue;

        utils::hook::detour vm_notify_hook;

        void vm_notify_stub(int inst, unsigned int notify_list_owner_id, unsigned int string_value, game::VariableValue* top)
        {
            notify_groups_queue.access([&](notify_groups_t& groups)
            {
                for (auto i = groups.begin(); i != groups.end(); )
                {
                    if (i->owner_id == notify_list_owner_id &&
                        i->sub_notifies.find(string_value) != i->sub_notifies.end())
                    {
                        vm_notify_hook.invoke<void>(inst, i->notify_target, i->main_notify, top);
                        i = groups.erase(i);
                    }
                    else
                    {
                        ++i;
                    }
                }
            });

            vm_notify_hook.invoke<void>(inst, notify_list_owner_id, string_value, top);
        }
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
            if (!game::environment::t6zm())
            {
                return;
            }

            vm_notify_hook.create(0x8F3620, vm_notify_stub);

            gsc::function::add("createnotifygroup", [](const gsc::function_args& args) 
                -> scripting::script_value
            {
                std::unordered_set<unsigned int> notifies;

                const auto entity = args[0].get_raw();
                if (entity.type != game::SCRIPT_OBJECT)
                {
                    throw std::runtime_error("argument 1 must be a script object");
                }

                const auto entity_target = args[1].get_raw();
                if (entity_target.type != game::SCRIPT_OBJECT)
                {
                    throw std::runtime_error("argument 1 must be a script object");
                }

                const auto main_notify_str = args[2].as<std::string>();
                const auto main_notify = game::SL_GetString(main_notify_str.data(), 0);

                for (auto i = 3u; i < args.size(); i++)
                {
                    const auto& arg = args[i];
                    if (arg.is<scripting::array>())
                    {
                        const auto arr = arg.as<scripting::array>();
                        for (auto o = 0u; o < arr.size(); o++)
                        {
                            if (!arr[o].is<std::string>())
                            {
                                continue;
                            }

                            const auto str = arr[o].as<std::string>();
                            notifies.insert(game::SL_GetString(str.data(), 0));
                        }
                    }
                    else
                    {
                        const auto str = arg.as<std::string>();
                        notifies.insert(game::SL_GetString(str.data(), 0));
                    }
                }

                if (notifies.size() <= 0)
                {
                    throw std::runtime_error("must pass atleast 1 notify");
                }

                notify_groups_queue.access([&](notify_groups_t& groups)
                {
                    groups.emplace_back(entity.u.uintValue, entity_target.u.uintValue, 
                        main_notify, notifies);
                });

                return {};
            });
		}
	};
}

REGISTER_COMPONENT(notifies::component)