#include "HomeGUI.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "ltps/Global.h"
#include "ltps/TeleportSystem.h"
#include "ltps/base/Config.h"
#include "ltps/common/BackSimpleForm.h"
#include "ltps/modules/home/HomeStorage.h"
#include "ltps/modules/home/event/HomeEvents.h"
#include "ltps/utils/McUtils.h"

#include <mc/world/level/dimension/VanillaDimensions.h>


namespace ltps::home {

using ll::form::CustomForm;
using ll::form::CustomFormResult;
using ll::form::SimpleForm;

void HomeGUI::sendMainMenu(Player& player, BackCB backCB) {
    auto localeCode = player.getLocaleCode();
    BackSimpleForm{std::move(backCB), BackSimpleForm::ButtonPos::Lower}
        .setTitle("Menu Nhà riêng"_trl(localeCode))
        .setContent(" · Vui lòng chọn một thao tác"_trl(localeCode))
        .appendButton(
            "Tạo nhà mới"_trl(localeCode),
            "textures/ui/color_plus",
            "path",
            [](Player& self) { sendAddHomeGUI(self); }
        )
        .appendButton(
            "Dịch chuyển đến nhà"_trl(localeCode),
            "textures/ui/send_icon",
            "path",
            [](Player& self) { sendGoHomeGUI(self); }
        )
        .appendButton(
            "Chỉnh sửa nhà"_trl(localeCode),
            "textures/ui/book_edit_default",
            "path",
            [](Player& self) { sendEditHomeGUI(self); }
        )
        .appendButton(
            "Xóa nhà"_trl(localeCode),
            "textures/ui/trash_default",
            "path",
            [](Player& self) { sendRemoveHomeGUI(self); }
        )
        .sendTo(player);
}

void HomeGUI::sendAddHomeGUI(Player& player) {
    auto localeCode = player.getLocaleCode();

    CustomForm fm{"Nhà - Thêm mới"_trl(localeCode)};
    fm.appendLabel("Nhập tên nhà muốn tạo, ví dụ: My Home\nLưu ý: Tên nhà không được vượt quá {} ký tự."_trl(
        localeCode,
        getConfig().modules.home.nameLength
    ));

    fm.appendInput(
        "name",
        "Nhập tên nhà",
        "string",
        "",
        "Không được vượt quá {} ký tự!"_trl(localeCode, getConfig().modules.home.nameLength)
    );

    fm.sendTo(player, [localeCode{std::move(localeCode)}](Player& self, CustomFormResult const& result, auto) {
        if (!result) return;

        auto name = std::get<std::string>(result->at("name"));
        if (name.empty()) {
            mc_utils::sendText<mc_utils::Error>(self, "Tên nhà không được để trống!"_trl(localeCode));
            return;
        }

        ll::event::EventBus::getInstance().publish(PlayerRequestAddHomeEvent(self, name));
    });
}

void HomeGUI::sendChooseHomeGUI(Player& player, ChooseHomeCallback chooseCB) {
    auto localeCode = player.getLocaleCode();

    auto fm = BackSimpleForm::make<HomeGUI::sendMainMenu>(BackCB{});
    fm.setTitle("Chọn nhà"_trl(localeCode)).setContent("Vui lòng chọn một nhà"_trl(localeCode));

    auto storage = TeleportSystem::getInstance().getStorageManager().getStorage<HomeStorage>();

    auto homes = storage->getHomes(player.getRealName());
    for (auto& home : homes) {
        auto _name = home.name;
        fm.appendButton(_name, [chooseCB, home = std::move(home)](Player& self) { chooseCB(self, home); });
    }

    fm.sendTo(player);
}
void HomeGUI::sendChooseHomeGUI(Player& player, ChooseNameCallBack chooseCB) {
    sendChooseHomeGUI(player, [cb = std::move(chooseCB)](Player& self, HomeStorage::Home home) {
        cb(self, home.name);
    });
}

void HomeGUI::sendGoHomeGUI(Player& player) {
    sendChooseHomeGUI(player, [](Player& self, std::string name) {
        ll::event::EventBus::getInstance().publish(PlayerRequestGoHomeEvent(self, std::move(name)));
    });
}

void HomeGUI::sendRemoveHomeGUI(Player& player) {
    sendChooseHomeGUI(player, [](Player& self, std::string name) {
        ll::event::EventBus::getInstance().publish(PlayerRequestRemoveHomeEvent(self, std::move(name)));
    });
}

void HomeGUI::sendEditHomeGUI(Player& player) {
    sendChooseHomeGUI(player, [](Player& self, HomeStorage::Home home) { _sendEditHomeGUI(self, std::move(home)); });
}
void HomeGUI::_sendEditHomeGUI(Player& player, HomeStorage::Home home) {
    auto localeCode = player.getLocaleCode();

    auto fm = BackSimpleForm::make<HomeGUI::sendEditHomeGUI>();
    fm.setTitle("Nhà - Chỉnh sửa"_trl(localeCode))
        .setContent("Tên: {}\nTọa độ: {}.{}.{}\nThế giới: {}\nTạo lúc: {}\nSửa lần cuối: {}"_trl(
            localeCode,
            home.name,
            home.x,
            home.y,
            home.z,
            VanillaDimensions::toString(home.dimid),
            home.createdTime,
            home.modifiedTime
        ))
        .appendButton(
            "Đổi tên nhà"_trl(localeCode),
            "textures/ui/book_edit_default",
            "path",
            [name = home.name](Player& self) { _sendEditHomeNameGUI(self, name); }
        )
        .appendButton(
            "Cập nhật vị trí"_trl(localeCode),
            "textures/ui/icon_import",
            "path",
            [name = home.name](Player& self) {
                ll::event::EventBus::getInstance().publish(PlayerRequestEditHomeEvent(
                    self,
                    name,
                    PlayerRequestEditHomeEvent::Type::Position,
                    self.getPosition()
                ));
            }
        )
        .sendTo(player);
}

void HomeGUI::_sendEditHomeNameGUI(Player& player, std::string const& name) {
    auto localeCode = player.getLocaleCode();
    CustomForm{"Đổi tên nhà"}
        .appendLabel("Đổi tên nhà, tên mới không vượt quá {} ký tự"_trl(localeCode, getConfig().modules.home.nameLength))
        .appendInput("newName", "Chỉnh sửa tên"_trl(localeCode), "string", name)
        .sendTo(player, [name](Player& self, CustomFormResult const& res, auto) {
            if (!res) return;

            auto newName = std::get<std::string>(res->at("newName"));
            if (newName.empty()) {
                mc_utils::sendText<mc_utils::Error>(self, "Tên không được để trống!"_trl(self.getLocaleCode()));
                return;
            }

            ll::event::EventBus::getInstance().publish(PlayerRequestEditHomeEvent{
                self,
                name,
                PlayerRequestEditHomeEvent::Type::Name,
                std::nullopt,
                std::move(newName)
            });
        });
}

} // namespace ltps::home