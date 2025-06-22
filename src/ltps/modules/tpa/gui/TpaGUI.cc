#include "ltps/modules/tpa/gui/TpaGUI.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/form/FormBase.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/Bedrock.h"
#include "ltps/modules/tpa/TpaRequest.h"
#include "ltps/modules/tpa/event/TpaEvents.h"
#include "mc/world/level/Level.h"
#include <memory>


namespace ltps::tpa {


void TpaGUI::sendMainMenu(Player& player) {
    sendChooseTpaTypeMenu(player, [](Player& self, TpaRequest::Type type) { sendChooseTpaPlayerMenu(self, type); });
}

void TpaGUI::sendChooseTpaTypeMenu(Player& player, ChooseTpaTypeCallback callback) {
    auto const localeCode = player.getLocaleCode();

    ll::form::SimpleForm{"Tpa 菜单"_trl(localeCode), "你想如何传送？"_trl(localeCode)}
        .appendButton("传送到其他玩家"_trl(localeCode))
        .appendButton("让其他玩家传送过来"_trl(localeCode))
        .sendTo(player, [fn = std::move(callback)](Player& self, int index, ll::form::FormCancelReason) {
            if (index == -1) {
                return;
            }

            fn(self, static_cast<TpaRequest::Type>(index));
        });
}

void TpaGUI::sendChooseTpaPlayerMenu(Player& player, TpaRequest::Type type) {
    auto level = ll::service::getLevel();
    if (!level) {
        return;
    }

    auto const localeCode = player.getLocaleCode();

    auto fm = ll::form::SimpleForm{"Tpa - 发起传送请求"_trl(localeCode), "选择一个玩家"_trl(localeCode)};

    level->forEachPlayer([&fm, type](Player& target) {
        fm.appendButton(target.getRealName(), [&target, type](Player& self) {
            // clang-format off
            ll::event::EventBus::getInstance().publish(
                CreateTpaRequestEvent{
                    self,
                    target,
                    type,
                    [](std::shared_ptr<TpaRequest> request) {
                        if (request) {
                            request->sendFormToReceiver();
                        }
                    }
                }
            );
            // clang-format on
        });
        return true;
    });

    fm.sendTo(player);
}


} // namespace ltps::tpa