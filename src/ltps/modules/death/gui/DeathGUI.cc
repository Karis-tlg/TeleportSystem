#include "DeathGUI.h"

#include "ltps/TeleportSystem.h"
#include "ltps/modules/death/DeathStorage.h"
#include "ltps/modules/death/event/DeathEvents.h"
#include "ltps/utils/McUtils.h"

#include <ll/api/event/EventBus.h>
#include <mc/world/level/dimension/VanillaDimensions.h>

namespace ltps ::death {


void DeathGUI::sendMainMenu(Player& player, BackCB backCb) {
    auto localeCode = player.getLocaleCode();

    auto infos =
        TeleportSystem::getInstance().getStorageManager().getStorage<DeathStorage>()->getDeathInfos(player.getRealName()
        );

    if (!infos || infos->empty()) {
        mc_utils::sendText(player, "Bạn chưa có thông tin về lần chết nào"_trl(localeCode));
        return;
    }

    auto fm = BackSimpleForm{std::move(backCb)};
    fm.setTitle("Death - Danh sách vị trí chết"_trl(localeCode));
    fm.setContent("Bạn có {0} bản ghi vị trí chết"_trl(localeCode, infos->size()));

    int index = 0;
    for (auto& info : *infos) {
        fm.appendButton("{}\n{}"_tr(info.time, info.toPosString()), [index](Player& self) {
            sendBackGUI(self, index, BackSimpleForm::makeCallback<sendMainMenu>(nullptr));
        });
        index++;
    }

    fm.sendTo(player);
}


void DeathGUI::sendBackGUI(Player& player, int index, BackCB backCb) {
    auto localeCode = player.getLocaleCode();

    auto info = TeleportSystem::getInstance().getStorageManager().getStorage<DeathStorage>()->getSpecificDeathInfo(
        player.getRealName(),
        index
    );
    if (!info) {
        mc_utils::sendText(player, "Bạn chưa có thông tin về lần chết nào"_trl(localeCode));
        return;
    }

    BackSimpleForm{std::move(backCb)}
        .setTitle("Death - Thông tin vị trí chết"_trl(localeCode))
        .setContent("Thời gian chết: {0}\nTọa độ chết: {1}"_trl(localeCode, info->time, info->toPosString()))
        .appendButton(
            "Dịch chuyển đến điểm chết"_trl(localeCode),
            [](Player& self) { ll::event::EventBus::getInstance().publish(PlayerRequestBackDeathPointEvent{self}); }
        )
        .appendButton("Hủy"_trl(localeCode))
        .sendTo(player);
}


} // namespace ltps::death