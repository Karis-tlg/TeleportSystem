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

    ll::form::SimpleForm{"Menu TPA"_trl(localeCode), "Chọn chế độ TPA?"_trl(localeCode)}
        .appendButton("TPA"_trl(localeCode))
        .appendButton("TPA Here"_trl(localeCode))
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

    auto fm = ll::form::SimpleForm{"Tpa - Gửi yêu cầu dịch chuyển"_trl(localeCode), "Chọn một người chơi"_trl(localeCode)};

    level->forEachPlayer([&fm, type](Player& target) {
        if (&target == &player) return true;
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
