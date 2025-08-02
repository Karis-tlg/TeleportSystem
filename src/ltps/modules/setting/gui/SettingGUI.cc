#include "SettingGUI.h"

#include "ltps/TeleportSystem.h"
#include "ltps/modules/setting/SettingStorage.h"
#include "ltps/utils/McUtils.h"

#include <ll/api/form/CustomForm.h>

namespace ltps::setting {


void SettingGUI::sendMainGUI(Player& player) {
    auto localeCode = player.getLocaleCode();
    auto setting    = TeleportSystem::getInstance().getStorageManager().getStorage<SettingStorage>()->getSettingData(
        player.getRealName()
    );
    if (!setting) {
        mc_utils::sendText<mc_utils::Error>(player, "Đã xảy ra lỗi, vui lòng thử lại sau"_trl(localeCode));
        return;
    }

    ll::form::CustomForm fm{"Cài đặt - Thiết lập cá nhân"_trl(localeCode)};
    fm.appendToggle("allowTpa", "Cho phép người khác gửi yêu cầu TPA đến tôi"_tr(), setting->allowTpa);
    fm.appendToggle("deathPopup", "Hiển thị cửa sổ quay lại điểm chết sau khi chết"_tr(), setting->deathPopup);
    fm.appendToggle("tpaPopup", "Hiển thị hộp thoại khi nhận yêu cầu TPA"_tr(), setting->tpaPopup);

    fm.sendTo(player, [](Player& self, ll::form::CustomFormResult const& res, auto) {
        if (!res) return;

        auto realName   = self.getRealName();
        auto localeCode = self.getLocaleCode();

        bool allowTpa   = std::get<uint64>(res->at("allowTpa"));
        bool deathPopup = std::get<uint64>(res->at("deathPopup"));
        bool tpaPopup   = std::get<uint64>(res->at("tpaPopup"));

        auto resp = TeleportSystem::getInstance().getStorageManager().getStorage<SettingStorage>()->setSettingData(
            realName,
            {.deathPopup = deathPopup, .allowTpa = allowTpa, .tpaPopup = tpaPopup}
        );

        if (!resp.has_value()) {
            mc_utils::sendText<mc_utils::Error>(self, "Đã xảy ra lỗi, vui lòng thử lại sau"_trl(localeCode));
            TeleportSystem::getInstance().getSelf().getLogger().error(
                "Không thể lưu cài đặt cho người chơi: {}, lỗi: {}",
                realName,
                resp.error()
            );
            return;
        }

        mc_utils::sendText(self, "Cài đặt đã được lưu"_trl(localeCode));
    });
}

} // namespace ltps::setting